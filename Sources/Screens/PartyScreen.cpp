#include "Screens/PartyScreen.hpp"

#include <cmath>
#include <map>
#include <new>

#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Framebuffer.hpp"
#include "Panel.hpp"
#include "Party.hpp"
#include "Scene.hpp"
#include "Screens/PartyMenu.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static_assert(PartyKey::A == (u32)Key::A && PartyKey::Y == (u32)Key::Y && PartyKey::Down == (u32)Key::DPadDown,
                  "PartyKey bits are HID bits");

    // Held direction: first repeat after RepeatDelay frames, then every RepeatEvery.
    static const u32 RepeatDelay = 15, RepeatEvery = 5;
    // After the X menu opens the Party screen, hold the panel off this long
    // at most while the scene changes.
    static const u32 ExpectFrames = 240;
    // Party data is re-read this often while shown (and after every action).
    static const u32 ReadEvery = 10;
    // The summary's party balls (bottom screen, x 0-35): the current one has a
    // green ring. Centres 16 + 32 * position; screenshots p23-summary,
    // p23-sum-down, p23-s0 (docs/PARTY-UI.md).
    static const s32 RingX = 5, RingY0 = 16, RingPitch = 32, RingHalf = 10, RingHits = 8;

    enum class Stage : u8
    {
        Idle,
        Summary,    // party-summary running
        Swap,       // party-swap running
        UseOpen,    // party-restore running (to the Restore bag)
        UseSeek,    // in the Restore bag: bringing the item into reach
        UseRow,     // party-bag-row running (opens the item menu)
        UseUse,     // party-use running
        UseBack,    // party-bag-back running (giving up)
    };

    // --- Shared with the OSD callback (game thread) ---
    static Mutex s_viewLock;
    static PartyView s_drawParty;           // under s_viewLock
    static SummaryView s_drawSummary;
    static volatile bool s_drawIsSummary = false;
    static volatile bool s_showLayer = false;
    static volatile u32 s_viewVersion = 0;
    static volatile bool s_ringWanted = false;
    static volatile int s_ringPos = -1;
    static volatile u32 s_ringCount = 0;
    static u8 *s_pixels = nullptr;
    static const size_t PixelBytes = PartyArea.w * PartyArea.h * 4;

    // --- Plugin thread ---
    static bool s_enabled = true;
    static PartyMenuState s_menu;
    static Stage s_stage = Stage::Idle;
    static PartyAction s_action;
    static std::string s_busy, s_note;
    static bool s_shown = false;
    static bool s_forcedOff = false, s_yield = false;
    static u32 s_captureKeys = 0;       // keys we asked to capture, 0 = none
    static u32 s_ourRequest = 0;        // InputCaptureRequest() right after we did
    static u32 s_frames = 0, s_expectUntil = 0, s_readAt = 0;
    static u32 s_repeatKey = 0, s_repeatAt = 0;
    static std::string s_prevScene, s_lastScene;     // last frame's, and the last non-empty one
    static bool s_fromParty = false;    // the summary was opened from the Party screen

    static PartyState s_party;
    static u8 s_order[6] = {0, 1, 2, 3, 4, 5};     // screen position -> party slot (from the Party screen)
    static std::vector<PartyMonView> s_mons;        // screen order
    static std::string s_listKey;
    static int s_lastCursorLogged = -1;
    static PartyPage s_lastPageLogged = PartyPage::List;

    static bool s_summaryHidden = false;
    static int s_summaryPos = -1;           // the Pokémon the summary shows (screen position)
    static int s_ringSeen = -1;             // latest ring reading and how many in a row
    static u32 s_ringSeenCount = 0, s_ringLastCount = 0;
    static const u32 RingStable = 3;
    static std::string s_summaryLogged;

    // Use item in progress.
    struct UseState
    {
        int pos = -1;
        PartyPocket pocket = PartyPocket::Medicine;
        int index = -1;
        PartyBagItem item;
        u32 listCount = 0;
        u16 hpBefore = 0, maxHp = 0;
        u32 statusBefore = 0;
        int steps = 0;
        std::string monName;
    };
    static UseState s_use;
    static std::vector<PartyMonView> s_swapBefore;      // the list when a swap started
    static std::vector<PartyBagItem> s_items[(int)PartyPocket::Count];

    static std::map<u16, std::string> s_speciesNames, s_itemNames, s_moveNames, s_abilityNames, s_natureNames;

    // --- OSD callback ---

    static UI::Layer s_layer;
    static u32 s_drawnVersion = ~0u;

    static void ScanRing(const Screen &screen)
    {
        const FbLayout fb = GetLayout(screen);
        int found = -1;
        for (int pos = 0; pos < 6 && found != -2; pos++)
        {
            const s32 cy = RingY0 + RingPitch * pos;
            int hits = 0;
            for (s32 y = cy - RingHalf; y <= cy + RingHalf; y++)
            {
                if (y < 0 || y >= 240)
                    continue;
                u8 r, g, b;
                DecodePixel(fb.origin + RingX * fb.stepX + y * fb.stepY, fb.format, r, g, b);
                hits += r < 0xA0 && g > 0xD0;
            }
            if (hits >= RingHits)
                found = found == -1 ? pos : -2;
        }
        s_ringPos = found >= 0 ? found : -1;
        s_ringCount = s_ringCount + 1;
    }

    static bool PartyOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
        {
            if (s_ringWanted)
                ScanRing(screen);
            return false;
        }
        if (!s_showLayer || s_pixels == nullptr)
            return false;

        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        bool redraw = s_viewVersion != s_drawnVersion;
        if (!s_layer.Matches(fb))
        {
            if (!s_layer.Setup(fb, PartyArea, PartyRadius, s_pixels, PixelBytes))
                return false;
            redraw = true;
        }
        if (redraw && s_viewLock.TryLock() == 0)
        {
            UI::Canvas canvas = s_layer.Begin();
            if (s_drawIsSummary)
                DrawSummary(canvas, s_drawSummary);
            else
                DrawParty(canvas, s_drawParty);
            s_drawnVersion = s_viewVersion;
            s_viewLock.Unlock();
        }
        s_layer.Present(fb);
        return true;
    }

    void StartPartyScreen(void)
    {
        OSD::Run(PartyOnScreen);
    }

    // --- Names (cached: decoding a message line is not free) ---

    static const std::string &Cached(std::map<u16, std::string> &cache, u16 id, std::string (*read)(u16))
    {
        auto it = cache.find(id);
        if (it == cache.end() || it->second.empty())
            it = cache.insert_or_assign(id, read(id)).first;
        return it->second;
    }

    static std::string ReadSpecies(u16 id) { return SpeciesName(id); }
    static std::string ReadItem(u16 id)    { return ItemName(id); }

    static std::string LoaderName(std::map<u16, std::string> &cache, u32 loader, u32 dat, u16 id)
    {
        auto it = cache.find(id);
        if (it != cache.end())
            return it->second;
        const std::string name = LoaderText(loader, dat, id);
        if (!name.empty())
            cache[id] = name;
        return name;
    }

    // --- Party data ---

    static PartyMonView MonView(const Pk7 &mon)
    {
        PartyMonView v;
        if (mon.form == Pk7Form::Invalid)
            return v;
        v.valid = mon.calcValid;
        v.egg = mon.isEgg;
        char nick[3 * 13 + 1];
        Utf16ToUtf8(mon.nickname, 13, nick, sizeof(nick));
        v.name = nick;
        v.species = Cached(s_speciesNames, mon.species, ReadSpecies);
        if (v.name.empty())
            v.name = v.species;
        v.level = mon.level;
        v.hp = mon.hp;
        v.maxHp = mon.maxHp;
        v.status = mon.status;
        if (mon.heldItem != 0)
        {
            v.item = Cached(s_itemNames, mon.heldItem, ReadItem);
            if (v.item.empty())
                v.item = Utils::Format("Item %u", mon.heldItem);
        }
        for (int i = 0; i < 4; i++)
        {
            v.moves[i] = mon.moves[i];
            v.pp[i] = mon.pp[i];
        }
        return v;
    }

    static const char *MonText(const PartyMonView &m, std::string &out)
    {
        if (!m.valid)
            out = "?";
        else if (m.egg)
            out = "Egg";
        else
        {
            const char *status = PartyStatusName(m.status);
            out = Utils::Format("%s L%u %u/%u %s %s", m.name.c_str(), m.level, m.hp, m.maxHp,
                                status[0] ? status : "-", m.item.empty() ? "-" : m.item.c_str());
        }
        return out.c_str();
    }

    static void ReadPocketItems(void)
    {
        const BagState bag = ReadBag();
        static const Pocket pockets[(int)PartyPocket::Count] = { Pocket::Medicine, Pocket::Berries };
        for (int p = 0; p < (int)PartyPocket::Count; p++)
        {
            std::vector<PartyBagItem> &list = s_items[p];
            list.clear();
            if (!bag.valid)
                continue;
            const u32 cap = bag.capacity[(int)pockets[p]];
            for (u32 i = 0; i < cap; i++)
            {
                const BagSlot slot = ReadBagSlot(bag, pockets[p], i);
                // The Bag lists the non-zero slots in array order.
                if (slot.item == 0 || slot.count == 0)
                    continue;
                PartyBagItem item;
                item.id = slot.item;
                item.count = slot.count;
                item.name = Cached(s_itemNames, slot.item, ReadItem);
                if (item.name.empty())
                    item.name = Utils::Format("Item %u", slot.item);
                list.push_back(item);
            }
        }
    }

    // Reads the party; on the Party screen also its order and cursor.
    static void ReadPartyNow(u32 sceneManager, std::vector<std::string> &log)
    {
        s_readAt = s_frames;
        s_party = ReadParty(sceneManager);
        if (!s_party.valid)
        {
            s_mons.clear();
            return;
        }
        if (s_party.partyScreen)
            for (int i = 0; i < 6; i++)
                s_order[i] = s_party.order[i];
        s_mons.clear();
        for (int pos = 0; pos < s_party.count; pos++)
        {
            const int slot = s_order[pos] < s_party.count ? s_order[pos] : pos;
            s_mons.push_back(MonView(s_party.slots[slot].mon));
        }
        std::string key, text;
        for (const PartyMonView &m : s_mons)
            key += std::string(key.empty() ? "" : ", ") + MonText(m, text);
        if (key != s_listKey)
        {
            s_listKey = key;
            log.push_back(Utils::Format("party-ui: party %u: ", s_party.count) + key);
        }
        ReadPocketItems();
    }

    static int Count(void)
    {
        return (int)s_mons.size();
    }

    static std::string NameAt(int pos)
    {
        return pos >= 0 && pos < Count() ? s_mons[pos].name : std::string("?");
    }

    // --- Panel ---

    // While the native screen is (about to be) up the panel is held off; a
    // panel turned on meanwhile is the player's doing (L+R): step aside.
    // AutoPanel drops the override on a scene change and sets that scene's
    // mode (Party: full) in the same frame, so a change then is not the player.
    static void HoldPanel(bool hold, bool sceneChanged, std::vector<std::string> &log)
    {
        if (hold && s_forcedOff && !sceneChanged && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            hold = false;
            log.push_back("party-ui: panel turned on by hand, native screen hidden until the Party screen closes");
        }
        if (hold)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("party-ui: native screen up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }
    }

    // --- Input ---

    // Our capture request. While an action runs the action layer owns it
    // (and gives ours back afterwards). Ending only ends a request that is
    // still ours, so the X menu's (taken as the Party screen closes) stays.
    static void SetCapture(u32 keys)
    {
        if (ActionRunning())
            return;
        if (keys == 0)
        {
            if (s_ourRequest != 0 && InputCaptureRequest() == s_ourRequest)
                InputCaptureEnd();
            s_captureKeys = s_ourRequest = 0;
            return;
        }
        if (keys == s_captureKeys && InputCaptureRequest() == s_ourRequest)
            return;
        InputCaptureBegin(keys, (keys & (u32)Key::CPad) != 0, false);
        s_captureKeys = keys;
        s_ourRequest = InputCaptureRequest();
    }

    // Pressed keys with the circle pad folded into the D-pad, one direction
    // at most, with key repeat for a held direction.
    static u32 PressedKeys(void)
    {
        const u32 held = CaptureRealHeld();
        u32 pressed = CaptureRealPressed();
        static const u32 pad[4][2] =
        {
            { (u32)Key::DPadUp, (u32)Key::CPadUp }, { (u32)Key::DPadDown, (u32)Key::CPadDown },
            { (u32)Key::DPadLeft, (u32)Key::CPadLeft }, { (u32)Key::DPadRight, (u32)Key::CPadRight },
        };
        u32 dir = 0, heldDir = 0;
        for (const auto &d : pad)
        {
            if (pressed & (d[0] | d[1]))
                dir = d[0];
            if (held & (d[0] | d[1]))
                heldDir |= d[0];
        }
        pressed &= ~(u32)(Key::DPadUp | Key::DPadDown | Key::DPadLeft | Key::DPadRight | Key::CPad);
        if (dir != 0)
        {
            s_repeatKey = dir;
            s_repeatAt = s_frames + RepeatDelay;
        }
        else if (s_repeatKey != 0 && (heldDir & s_repeatKey) && s_frames >= s_repeatAt)
        {
            dir = s_repeatKey;
            s_repeatAt = s_frames + RepeatEvery;
        }
        else if (!(heldDir & s_repeatKey))
            s_repeatKey = 0;
        return pressed | dir;
    }

    static bool Start(Stage stage, u32 sceneManager, const std::string &busy, std::vector<std::string> &log)
    {
        if (!ActionStart(s_action.def, sceneManager, log))
        {
            s_note = "Couldn't do that now";
            s_stage = Stage::Idle;
            s_busy.clear();
            return false;
        }
        s_stage = stage;
        s_busy = busy;
        return true;
    }

    static void RunCommand(const PartyCommand &cmd, u32 sceneManager, std::vector<std::string> &log)
    {
        s_note.clear();
        switch (cmd.kind)
        {
        case PartyCommand::Summary:
            log.push_back(Utils::Format("party-ui: summary %d (%s)", cmd.pos + 1, NameAt(cmd.pos).c_str()));
            MakePartySummaryAction(s_action, cmd.pos);
            s_summaryPos = cmd.pos;
            Start(Stage::Summary, sceneManager, "Opening the summary of " + NameAt(cmd.pos) + "...", log);
            break;

        case PartyCommand::Swap:
            log.push_back(Utils::Format("party-ui: swap %d (%s) and %d (%s)", cmd.pos + 1, NameAt(cmd.pos).c_str(),
                                        cmd.other + 1, NameAt(cmd.other).c_str()));
            MakePartySwapAction(s_action, cmd.pos, cmd.other, s_party.screenCursor);
            s_use.pos = cmd.pos;
            s_use.index = cmd.other;
            s_swapBefore = s_mons;
            Start(Stage::Swap, sceneManager, "Moving " + NameAt(cmd.pos) + "...", log);
            break;

        case PartyCommand::UseItem:
        {
            const std::vector<PartyBagItem> &items = s_items[(int)cmd.pocket];
            if (cmd.item < 0 || cmd.item >= (int)items.size() || cmd.pos < 0 || cmd.pos >= Count())
                break;
            s_use = UseState();
            s_use.pos = cmd.pos;
            s_use.pocket = cmd.pocket;
            s_use.index = cmd.item;
            s_use.item = items[cmd.item];
            s_use.listCount = items.size();
            s_use.hpBefore = s_mons[cmd.pos].hp;
            s_use.maxHp = s_mons[cmd.pos].maxHp;
            s_use.statusBefore = s_mons[cmd.pos].status;
            s_use.monName = s_mons[cmd.pos].name;
            log.push_back(Utils::Format("party-ui: use %s (%s %d of %u) on %d (%s)", s_use.item.name.c_str(),
                                        PartyPocketName(cmd.pocket), cmd.item + 1, s_use.listCount, cmd.pos + 1,
                                        s_use.monName.c_str()));
            MakePartyRestoreAction(s_action, cmd.pos, cmd.pocket);
            Start(Stage::UseOpen, sceneManager, "Using " + s_use.item.name + " on " + s_use.monName + "...", log);
            break;
        }

        default:
            break;
        }
    }

    static void HandleInput(u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 keys = PressedKeys();
        if (keys == 0)
            return;
        const PartyCommand cmd = s_menu.Press(keys, Count(), &s_items[(int)s_menu.Pocket()]);
        if (s_menu.Cursor() != s_lastCursorLogged)
        {
            s_lastCursorLogged = s_menu.Cursor();
            log.push_back(Utils::Format("party-ui: cursor %d (%s)", s_menu.Cursor() + 1, NameAt(s_menu.Cursor()).c_str()));
        }
        if (s_menu.Page() != s_lastPageLogged || (keys & (PartyKey::Up | PartyKey::Down | PartyKey::L | PartyKey::R
                                                          | PartyKey::Left | PartyKey::Right)))
        {
            static const char *const pages[] = { "list", "menu", "swap", "items" };
            std::string line = std::string("party-ui: page ") + pages[(int)s_menu.Page()];
            if (s_menu.Page() == PartyPage::Menu)
                line += std::string(", ") + PartyMenuLabel((PartyMenuItem)s_menu.MenuCursor());
            else if (s_menu.Page() == PartyPage::Items)
            {
                const std::vector<PartyBagItem> &items = s_items[(int)s_menu.Pocket()];
                line += std::string(", ") + PartyPocketName(s_menu.Pocket());
                line += items.empty() ? std::string(" (empty)")
                        : Utils::Format(" %d of %u: %s x%u", s_menu.ItemCursor() + 1, items.size(),
                                        items[s_menu.ItemCursor()].name.c_str(), items[s_menu.ItemCursor()].count);
            }
            if (s_menu.Page() != s_lastPageLogged || s_menu.Page() != PartyPage::List)
                log.push_back(line);
            s_lastPageLogged = s_menu.Page();
        }
        if (cmd.kind != PartyCommand::None)
            RunCommand(cmd, sceneManager, log);
    }

    // --- Stages after an action ---

    static void FinishUse(u32 sceneManager, bool ok, std::vector<std::string> &log)
    {
        ReadPartyNow(sceneManager, log);
        const PartyMonView after = s_use.pos < Count() ? s_mons[s_use.pos] : PartyMonView();
        u16 countAfter = 0;
        for (const PartyBagItem &i : s_items[(int)s_use.pocket])
            if (i.id == s_use.item.id)
                countAfter = i.count;
        const bool used = countAfter != s_use.item.count;
        log.push_back(Utils::Format("party-ui: %s %s on %s: hp %u/%u -> %u/%u, status %lX -> %lX, %s x%u -> x%u%s",
                                    used ? "used" : ok ? "no effect:" : "FAILED", s_use.item.name.c_str(),
                                    s_use.monName.c_str(), s_use.hpBefore, s_use.maxHp, after.hp, after.maxHp,
                                    s_use.statusBefore, after.status, s_use.item.name.c_str(), s_use.item.count,
                                    countAfter, ok ? "" : " (did not get back to the list)"));
        s_note = used ? s_use.item.name + " used on " + s_use.monName
                      : ok ? "It won't have any effect" : "Something went wrong (see the log)";
    }

    static void BackOut(u32 sceneManager, const std::string &why, std::vector<std::string> &log)
    {
        log.push_back("party-ui: use FAILED: " + why + "; back to the Party screen");
        s_note = "Couldn't use " + s_use.item.name;
        MakePartyBagBackAction(s_action);
        Start(Stage::UseBack, sceneManager, "Going back...", log);
    }

    static void Seek(u32 sceneManager, std::vector<std::string> &log)
    {
        const BagListState list = ReadBagList(sceneManager);
        if (!list.valid)
        {
            BackOut(sceneManager, "the Bag's list doesn't read", log);
            return;
        }
        if (list.count != s_use.listCount)
        {
            BackOut(sceneManager, Utils::Format("the Bag lists %lu items, %s has %lu", list.count,
                                                PartyPocketName(s_use.pocket), s_use.listCount), log);
            return;
        }
        // The list scrolls smoothly; wait for it to settle on a row.
        const float whole = std::floor(list.scroll + 0.5f);
        if (std::fabs(list.scroll - whole) > 0.01f)
            return;
        const PartyBagPlan plan = PlanPartyBag(s_use.index, (int)whole, (int)list.rows, (int)list.count);
        if (plan.row >= 0)
        {
            log.push_back(Utils::Format("party-ui: bag scroll %d row %lu, tapping row %d", (int)whole, list.row + 1,
                                        plan.row + 1));
            MakePartyBagRowAction(s_action, plan.row);
            Start(Stage::UseRow, sceneManager, s_busy, log);
        }
        else if (plan.dir != 0 && ++s_use.steps <= 2 * (int)list.count)
        {
            MakePartyBagStepAction(s_action, plan.dir < 0);
            Start(Stage::UseSeek, sceneManager, s_busy, log);
        }
        else
            BackOut(sceneManager, "the item didn't come into view", log);
    }

    static void AfterAction(u32 sceneManager, std::vector<std::string> &log)
    {
        const std::string scene = SceneModule(sceneManager);
        const bool onList = scene == SceneParty && (ActionProbeFlags() & WorldPartyList);
        const Stage stage = s_stage;
        s_stage = Stage::Idle;
        switch (stage)
        {
        case Stage::Summary:
            s_busy.clear();
            if (scene != SceneStatus)
            {
                s_note = "The summary didn't open";
                log.push_back("party-ui: summary FAILED (scene " + scene + ")");
            }
            break;

        case Stage::Swap:
        {
            s_busy.clear();
            const std::vector<PartyMonView> &before = s_swapBefore;
            ReadPartyNow(sceneManager, log);
            const int a = s_use.pos, b = s_use.index;
            const bool swapped = onList && a < Count() && b < Count() && a < (int)before.size()
                                 && b < (int)before.size() && s_mons[a].name == before[b].name
                                 && s_mons[b].name == before[a].name;
            std::string order;
            for (int i = 0; i < s_party.count; i++)
                order += Utils::Format(" %u", s_order[i] + 1);
            log.push_back(Utils::Format("party-ui: swap %s, screen order%s", swapped ? "ok" : "FAILED", order.c_str()));
            s_note = swapped ? "Moved " + NameAt(b) : "The move didn't work";
            s_menu.SetCursor(b, Count());
            break;
        }

        case Stage::UseOpen:
            if (scene != SceneBag)
            {
                s_busy.clear();
                s_note = "Couldn't open the Bag";
                log.push_back("party-ui: use FAILED: the Restore bag didn't open (scene " + scene + ")");
                break;
            }
            s_stage = Stage::UseSeek;
            break;

        case Stage::UseSeek:
            s_stage = Stage::UseSeek;   // keep going (Seek runs every frame)
            break;

        case Stage::UseRow:
            MakePartyUseAction(s_action);
            Start(Stage::UseUse, sceneManager, s_busy, log);
            break;

        case Stage::UseUse:
            s_busy.clear();
            FinishUse(sceneManager, onList, log);
            break;

        case Stage::UseBack:
            s_busy.clear();
            break;

        default:
            break;
        }
    }

    // --- Views ---

    static void PublishParty(void)
    {
        PartyView view;
        view.mons = s_mons;
        view.page = s_menu.Page();
        view.cursor = s_menu.Cursor();
        view.menuCursor = s_menu.MenuCursor();
        view.swapFrom = s_menu.SwapFrom();
        view.pocket = s_menu.Pocket();
        view.itemCursor = s_menu.ItemCursor();
        if (view.page == PartyPage::Items)
            view.items = s_items[(int)view.pocket];
        view.busy = s_busy;
        view.note = s_note;
        static std::string last;
        const std::string key = PartyViewKey(view);
        if (key == last && !s_drawIsSummary)
            return;
        last = key;
        s_viewLock.Lock();
        s_drawParty = view;
        s_drawIsSummary = false;
        s_viewVersion = s_viewVersion + 1;
        s_viewLock.Unlock();
    }

    static void PublishSummary(u32 sceneManager, std::vector<std::string> &log)
    {
        // Follow the game's ring once it reads the same RingStable times; it
        // is not seen while the game slides to the next Pokémon.
        if (s_ringCount != s_ringLastCount)
        {
            s_ringLastCount = s_ringCount;
            const int ring = s_ringPos;
            s_ringSeenCount = ring == s_ringSeen ? s_ringSeenCount + 1 : 1;
            s_ringSeen = ring;
            if (ring >= 0 && ring < Count() && s_ringSeenCount >= RingStable)
                s_summaryPos = ring;
        }
        SummaryView view;
        view.count = Count();
        view.pos = s_summaryPos;
        view.hidden = s_summaryHidden;
        if (view.pos >= 0 && view.pos < Count())
        {
            view.mon = s_mons[view.pos];
            const u32 loader = SummaryWordLoader(sceneManager);
            const int slot = s_order[view.pos] < s_party.count ? s_order[view.pos] : view.pos;
            const Pk7 &pk = s_party.slots[slot].mon;
            for (int i = 0; i < 4; i++)
                if (view.mon.moves[i] != 0)
                    view.mon.moveNames[i] = LoaderName(s_moveNames, loader, MoveNameDat, view.mon.moves[i]);
            view.mon.ability = LoaderName(s_abilityNames, loader, AbilityNameDat, pk.ability);
            view.mon.nature = LoaderName(s_natureNames, loader, NatureNameDat, pk.nature);

            std::string text;
            std::string line = Utils::Format("party-ui: summary %d of %d: ", view.pos + 1, view.count)
                               + MonText(view.mon, text) + ", ability " + (view.mon.ability.empty() ? "?" : view.mon.ability)
                               + ", nature " + (view.mon.nature.empty() ? "?" : view.mon.nature) + ", moves";
            for (int i = 0; i < 4; i++)
                if (view.mon.moves[i] != 0)
                    line += Utils::Format(" %s %u,", view.mon.moveNames[i].empty()
                                          ? Utils::Format("#%u", view.mon.moves[i]).c_str()
                                          : view.mon.moveNames[i].c_str(), view.mon.pp[i]);
            line.pop_back();
            if (line != s_summaryLogged)
            {
                s_summaryLogged = line;
                log.push_back(line);
            }
        }
        static std::string last;
        const std::string key = SummaryViewKey(view);
        if (key == last && s_drawIsSummary)
            return;
        last = key;
        s_viewLock.Lock();
        s_drawSummary = view;
        s_drawIsSummary = true;
        s_viewVersion = s_viewVersion + 1;
        s_viewLock.Unlock();
    }

    // --- Per frame ---

    void PartyScreenExpect(void)
    {
        s_expectUntil = s_frames + ExpectFrames;
    }

    void PartyScreenUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        s_frames++;
        const bool inParty = scene == SceneParty;
        if (!scene.empty() && scene != s_lastScene)
        {
            // The summary counts as ours only when it came from the Party screen.
            if (scene == SceneStatus)
                s_fromParty = s_lastScene == SceneParty;
            else if (scene != SceneParty && scene != SceneBag)
                s_fromParty = false;
            if (scene != SceneParty && scene != SceneStatus && scene != SceneBag)
                s_expectUntil = 0;
            // Back from the summary, the game's cursor is on the Pokémon it showed last.
            if (scene == SceneParty && s_lastScene == SceneStatus && s_summaryPos >= 0)
                s_menu.SetCursor(s_summaryPos, Count());
            s_lastScene = scene;
        }
        if (inParty)
            s_expectUntil = 0;
        const bool inStatus = scene == SceneStatus && s_fromParty;
        const bool ours = s_stage != Stage::Idle;
        const bool inBag = scene == SceneBag && ours;
        // The loading gap between our scenes (Party <-> Status / Bag).
        const bool gap = scene.empty() && (s_lastScene == SceneParty || (s_lastScene == SceneStatus && s_fromParty)
                                           || (s_lastScene == SceneBag && ours));
        const bool expecting = s_frames < s_expectUntil;
        const bool here = inParty || inStatus || inBag || gap;
        if (!here && !expecting)
            s_yield = false;

        HoldPanel(s_enabled && !s_yield && (here || expecting), scene != s_prevScene, log);

        const bool shown = s_enabled && !s_yield && here;
        if (shown && !s_shown)
        {
            if (s_pixels == nullptr)
                s_pixels = new (std::nothrow) u8[PixelBytes];
            s_menu.Reset(6);
            s_note.clear();
            s_repeatKey = 0;
            s_lastCursorLogged = -1;
            s_lastPageLogged = PartyPage::List;
            s_listKey.clear();
            ReadPartyNow(sceneManager, log);
            if (s_party.screenCursor >= 0)
                s_menu.SetCursor(s_party.screenCursor, Count());
            log.push_back(Utils::Format("party-ui: shown (%s, cursor %d)%s", scene.empty() ? "(none)" : scene.c_str(),
                                        s_menu.Cursor() + 1, s_pixels == nullptr ? ", NO MEMORY for the layer" : ""));
        }
        else if (!shown && s_shown)
        {
            log.push_back("party-ui: hidden");
            if (s_stage != Stage::Idle && !ActionRunning())
                s_stage = Stage::Idle;
            s_busy.clear();
        }
        s_shown = shown;

        if (!shown)
        {
            s_showLayer = false;
            s_ringWanted = false;
            SetCapture(0);
            s_prevScene = scene;
            return;
        }

        if (s_frames - s_readAt >= ReadEvery)
            ReadPartyNow(sceneManager, log);

        // Stages: one step whenever no action runs.
        if (!ActionRunning())
        {
            if (s_stage == Stage::UseSeek && scene == SceneBag)
                Seek(sceneManager, log);
            else if (s_stage != Stage::Idle && s_stage != Stage::UseSeek)
                AfterAction(sceneManager, log);
            else if (s_stage == Stage::UseSeek && !scene.empty())
            {
                s_stage = Stage::Idle;
                s_busy.clear();
                log.push_back("party-ui: use FAILED: left the Bag (scene " + scene + ")");
            }
        }

        // Input.
        if (inStatus)
        {
            s_ringWanted = true;
            SetCapture((u32)Key::Y);
            if (!ActionRunning() && (CaptureRealPressed() & Key::Y))
            {
                s_summaryHidden = !s_summaryHidden;
                log.push_back(std::string("party-ui: summary ") + (s_summaryHidden ? "hidden (game page)" : "shown"));
            }
            PublishSummary(sceneManager, log);
            s_showLayer = !s_summaryHidden;
        }
        else
        {
            s_ringWanted = false;
            s_summaryHidden = false;
            s_summaryLogged.clear();
            const bool owning = inParty && s_stage == Stage::Idle && !ActionRunning()
                                && (ActionProbeFlags() & WorldPartyList);
            if (owning)
            {
                SetCapture(s_menu.Keys() | (u32)Key::CPad);
                HandleInput(sceneManager, log);
                SetCapture(s_menu.Keys() | (u32)Key::CPad);
            }
            else if (s_stage == Stage::Idle && !ActionRunning())
                SetCapture(0);
            PublishParty();
            s_showLayer = true;
        }
        s_prevScene = scene;
    }

    bool PartyScreenCommand(const std::string &args, std::vector<std::string> &log)
    {
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(PartyScreenStatus());
        }
        else if (args == "status")
            log.push_back(PartyScreenStatus());
        else
            return false;
        return true;
    }

    std::string PartyScreenStatus(void)
    {
        static const char *const stages[] = { "idle", "summary", "swap", "use-open", "use-seek", "use-row", "use-use",
                                              "use-back" };
        static const char *const pages[] = { "list", "menu", "swap", "items" };
        return Utils::Format("party-ui: %s, %s, page %s, cursor %d, stage %s%s%s", s_enabled ? "on" : "off",
                             s_shown ? "shown" : "hidden", pages[(int)s_menu.Page()], s_menu.Cursor() + 1,
                             stages[(int)s_stage], s_yield ? ", panel on by hand" : "",
                             s_captureKeys ? ", input captured" : "");
    }
}
