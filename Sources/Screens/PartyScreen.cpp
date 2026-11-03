#include "Screens/PartyScreen.hpp"

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
#include "Screens/Bag.hpp"
#include "Screens/PartyMenu.hpp"
#include "UI/Device.hpp"
#include "UI/Assets.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"

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
    // Following a Use / Give: give up (and step aside for the panel) after
    // this long without progress in the Bag, or in another scene
    // (an evolution plays there).
    static const u32 FollowStuckFrames = 900, FollowOtherFrames = 3600;
    // The summary's party balls (bottom screen, x 0-35): the current one has a
    // green ring. Centres 16 + 32 * position; screenshots p23-summary,
    // p23-sum-down, p23-s0 (docs/PARTY-UI.md).
    static const s32 RingX = 5, RingY0 = 16, RingPitch = 32, RingHalf = 10, RingHits = 8;

    enum class Stage : u8
    {
        Idle,
        Summary,    // party-summary running
        Swap,       // party-swap running
        Take,       // party-take running
        BagOpen,    // party-restore / party-held-bag running (to the Bag)
        BagSeek,    // in the Bag: bringing the item into reach
        BagRow,     // party-bag-row running (opens the item menu)
        BagMenu,    // waiting for the item menu, then Use / Give
        Follow,     // following the game after Use / Give
        FollowAct,  // an action started while following runs
        Asking,     // the native Ask page waits for the player
        BagBack,    // party-bag-back running (giving up)
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
    // The layer draws into the native screens' shared buffer (W7-M,
    // UI/ScreenBuffer.hpp), borrowed while the layer shows and given back
    // when it doesn't; nothing is allocated for it here.
    static const char *const BufferOwner = "party";

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

    // A Use / Give / Take in progress.
    struct ItemJob
    {
        enum Kind : u8 { Use, Give, Take } kind = Use;
        int pos = -1;
        PartyPocket pocket = PartyPocket::Medicine;
        int index = -1;
        PartyBagItem item;
        u32 listCount = 0;
        PartyMonView before;            // the Pokémon when it started
        std::vector<PartyBagItem> pocketBefore;
        int steps = 0;
        int tabTries = 0;
        u32 seekWait = 0, menuWait = 0;
        // Following the game.
        PartyGame game = PartyGame::Gap;
        u32 signature = 0, stable = 0, sinceProgress = 0;
        int actions = 0;
        // The Ask page: what it answers.
        enum AskKind : u8 { AskNone, AskYesNo, AskMenu, AskTeach } ask = AskNone;
        int menuEntries = 0;            // AskMenu: entries of the game's menu
        u16 teachMove = 0;
        std::string learned;            // "Torracat learned Work Up" style, from the teaching screen
    };
    static ItemJob s_job;
    static PartyAskView s_ask;
    static int s_askCancel = -1;
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
        if (!s_showLayer)
        {
            UI::ScreenBuffer::GiveBack(BufferOwner);
            return false;
        }
        bool fresh = false;
        u8 *pixels = UI::ScreenBuffer::Borrow(BufferOwner, fresh);
        if (pixels == nullptr)
            return false;       // another screen holds it: next frame

        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        bool redraw = s_viewVersion != s_drawnVersion;
        if (fresh || !s_layer.Matches(fb))
        {
            // fresh: another screen drew in the buffer meanwhile.
            if (!s_layer.Setup(fb, PartyArea, PartyRadius, pixels, UI::ScreenBuffer::Bytes))
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

    // A name from the summary's word loader; the loader is only looked up
    // (a walk over the CRO headers) when the name isn't cached yet.
    static std::string LoaderName(std::map<u16, std::string> &cache, u32 sceneManager, u32 &loader, u32 dat, u16 id)
    {
        auto it = cache.find(id);
        if (it != cache.end())
            return it->second;
        if (loader == 0)
            loader = SummaryWordLoader(sceneManager);
        const std::string name = LoaderText(loader, dat, id);
        if (!name.empty())
            cache[id] = name;
        return name;
    }

    // A move's name outside the summary: seen there before, or the game's
    // own text (the shared assets, once loaded), else its number.
    static std::string MoveNameOf(u16 move)
    {
        auto it = s_moveNames.find(move);
        if (it != s_moveNames.end() && !it->second.empty())
            return it->second;
        return UI::AssetMoveName(move, Utils::Format("Move %u", move));
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
        v.gender = mon.gender;
        v.hp = mon.hp;
        v.maxHp = mon.maxHp;
        v.status = mon.status;
        v.itemId = mon.heldItem;
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
        static const Pocket pockets[(int)PartyPocket::Count] = { Pocket::Medicine, Pocket::Items, Pocket::TMs,
                                                                  Pocket::Berries };
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

    std::vector<PartyMonView> PartyScreenMons(u32 sceneManager)
    {
        const PartyState party = ReadParty(sceneManager);
        std::vector<PartyMonView> mons;
        for (int slot = 0; party.valid && slot < party.count; slot++)
            mons.push_back(MonView(party.slots[slot].mon));
        return mons;
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
            // Mid-way through the game's Bag: leave the rest to the player.
            if (s_stage >= Stage::BagSeek && stage != Stage::BagBack)
            {
                log.push_back("party-ui: can't go on natively; the native screen steps aside (L+R panel)");
                s_yield = true;
            }
            s_note = "Couldn't do that now";
            s_stage = Stage::Idle;
            s_busy.clear();
            return false;
        }
        s_stage = stage;
        s_busy = busy;
        return true;
    }

    // Starts a Use or Give of item `index` of `pocket` on position `pos`.
    static void StartItem(ItemJob::Kind kind, int pos, PartyPocket pocket, int index, u32 sceneManager,
                          std::vector<std::string> &log)
    {
        const std::vector<PartyBagItem> &items = s_items[(int)pocket];
        if (index < 0 || index >= (int)items.size() || pos < 0 || pos >= Count())
            return;
        s_job = ItemJob();
        s_job.kind = kind;
        s_job.pos = pos;
        s_job.pocket = pocket;
        s_job.index = index;
        s_job.item = items[index];
        s_job.listCount = items.size();
        s_job.before = s_mons[pos];
        s_job.pocketBefore = items;
        const bool give = kind == ItemJob::Give;
        log.push_back(Utils::Format("party-ui: %s %s (%s %d of %u) %s %d (%s)", give ? "give" : "use",
                                    s_job.item.name.c_str(), PartyPocketName(pocket), index + 1, s_job.listCount,
                                    give ? "to" : "on", pos + 1, s_job.before.name.c_str()));
        if (!give && PartyUseViaRestore(pocket))
            MakePartyRestoreAction(s_action, pos, pocket);
        else
            MakePartyHeldBagAction(s_action, pos, pocket);
        Start(Stage::BagOpen, sceneManager, (give ? "Giving " + s_job.item.name + " to "
                                                  : "Using " + s_job.item.name + " on ") + s_job.before.name + "...", log);
    }

    static void RunCommand(const PartyCommand &cmd, u32 sceneManager, std::vector<std::string> &log);

    static void HandleInput(u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 keys = PressedKeys();
        if (keys == 0)
            return;
        PartyMenuInput in;
        in.count = Count();
        in.items = &s_items[(int)s_menu.Pocket()];
        in.holding = s_menu.Cursor() < Count() && s_mons[s_menu.Cursor()].itemId != 0;
        in.choices = (int)s_ask.choices.size();
        in.cancelChoice = s_askCancel;
        const PartyCommand cmd = s_menu.Press(keys, in);
        if (s_menu.Cursor() != s_lastCursorLogged)
        {
            s_lastCursorLogged = s_menu.Cursor();
            log.push_back(Utils::Format("party-ui: cursor %d (%s)", s_menu.Cursor() + 1, NameAt(s_menu.Cursor()).c_str()));
        }
        if (s_menu.Page() != s_lastPageLogged || (keys & (PartyKey::Up | PartyKey::Down | PartyKey::L | PartyKey::R
                                                          | PartyKey::Left | PartyKey::Right)))
        {
            static const char *const pages[] = { "list", "menu", "swap", "items", "ask" };
            std::string line = std::string("party-ui: page ") + pages[(int)s_menu.Page()];
            if (s_menu.Page() == PartyPage::Menu)
            {
                const std::vector<PartyMenuItem> entries = PartyMenuEntries(in.holding, Count());
                const int i = s_menu.MenuCursor() < (int)entries.size() ? s_menu.MenuCursor() : (int)entries.size() - 1;
                line += std::string(", ") + PartyMenuLabel(entries[i]);
            }
            else if (s_menu.Page() == PartyPage::Items)
            {
                const std::vector<PartyBagItem> &items = s_items[(int)s_menu.Pocket()];
                line += std::string(s_menu.Giving() ? " (give), " : ", ") + PartyPocketName(s_menu.Pocket());
                line += items.empty() ? std::string(" (empty)")
                        : Utils::Format(" %d of %u: %s x%u", s_menu.ItemCursor() + 1, items.size(),
                                        items[s_menu.ItemCursor()].name.c_str(), items[s_menu.ItemCursor()].count);
            }
            else if (s_menu.Page() == PartyPage::Ask && s_menu.AskCursor() < (int)s_ask.choices.size())
                line += ", " + s_ask.choices[s_menu.AskCursor()].label;
            if (s_menu.Page() != s_lastPageLogged || s_menu.Page() != PartyPage::List)
                log.push_back(line);
            s_lastPageLogged = s_menu.Page();
        }
        if (cmd.kind != PartyCommand::None)
            RunCommand(cmd, sceneManager, log);
    }

    // --- Results ---

    static u16 CountOf(const std::vector<PartyBagItem> &list, u16 id)
    {
        for (const PartyBagItem &i : list)
            if (i.id == id)
                return i.count;
        return 0;
    }

    static std::string MovesText(const PartyMonView &m)
    {
        std::string s;
        for (int i = 0; i < 4; i++)
            if (m.moves[i] != 0)
                s += (s.empty() ? "" : ", ") + MoveNameOf(m.moves[i]);
        return s.empty() ? std::string("-") : s;
    }

    static bool SameMoves(const PartyMonView &a, const PartyMonView &b)
    {
        for (int i = 0; i < 4; i++)
            if (a.moves[i] != b.moves[i])
                return false;
        return true;
    }

    // After a Use / Give / Take: what changed, for the log and the footer.
    static void FinishItem(u32 sceneManager, bool ok, std::vector<std::string> &log)
    {
        ReadPartyNow(sceneManager, log);
        s_busy.clear();
        const ItemJob &j = s_job;
        const PartyMonView after = j.pos < Count() ? s_mons[j.pos] : PartyMonView();
        const PartyMonView &b = j.before;
        const char *fail = ok ? "" : " (did not get back to the list)";
        if (j.kind == ItemJob::Take)
        {
            const u16 countBefore = CountOf(j.pocketBefore, b.itemId);
            u16 countAfter = 0;
            for (int p = 0; p < (int)PartyPocket::Count; p++)
                countAfter = countAfter ? countAfter : CountOf(s_items[p], b.itemId);
            const bool took = after.itemId == 0 && b.itemId != 0;
            log.push_back(Utils::Format("party-ui: %s %s from %s: held %s -> %s, %s x%u -> x%u%s",
                                        took ? "took" : "take FAILED:", b.item.c_str(), b.name.c_str(), b.item.c_str(),
                                        after.item.empty() ? "-" : after.item.c_str(), b.item.c_str(), countBefore,
                                        countAfter, fail));
            s_note = took ? "Received the " + b.item + " from " + b.name : "Couldn't take the item";
            return;
        }
        const u16 countAfter = CountOf(s_items[(int)j.pocket], j.item.id);
        if (j.kind == ItemJob::Give)
        {
            const bool gave = after.itemId == j.item.id && countAfter != j.item.count;
            log.push_back(Utils::Format("party-ui: %s %s to %s: held %s -> %s, %s x%u -> x%u%s",
                                        gave ? "gave" : ok ? "kept:" : "give FAILED:", j.item.name.c_str(),
                                        b.name.c_str(), b.item.empty() ? "-" : b.item.c_str(),
                                        after.item.empty() ? "-" : after.item.c_str(), j.item.name.c_str(),
                                        j.item.count, countAfter, fail));
            s_note = gave ? after.name + " is now holding the " + j.item.name
                          : ok ? b.name + " kept its item" : "Something went wrong (see the log)";
            return;
        }
        // A TM stays in the Bag: a changed move set counts as used too.
        const bool changed = countAfter != j.item.count || !SameMoves(after, b) || after.level != b.level
                             || after.hp != b.hp || after.status != b.status || after.species != b.species;
        std::string line = Utils::Format("party-ui: %s %s on %s: hp %u/%u -> %u/%u, status %lX -> %lX, %s x%u -> x%u",
                                         changed ? "used" : ok ? "no effect:" : "FAILED", j.item.name.c_str(),
                                         b.name.c_str(), b.hp, b.maxHp, after.hp, after.maxHp, b.status, after.status,
                                         j.item.name.c_str(), j.item.count, countAfter);
        if (after.level != b.level)
            line += Utils::Format(", level %u -> %u", b.level, after.level);
        if (after.species != b.species)
            line += ", species " + b.species + " -> " + after.species;
        if (!SameMoves(after, b))
            line += ", moves " + MovesText(b) + " -> " + MovesText(after);
        log.push_back(line + fail);
        s_note = changed ? (j.learned.empty() ? j.item.name + " used on " + b.name : j.learned)
                         : ok ? "It won't have any effect" : "Something went wrong (see the log)";
    }

    static void BackOut(u32 sceneManager, const std::string &why, std::vector<std::string> &log)
    {
        log.push_back(std::string("party-ui: ") + (s_job.kind == ItemJob::Give ? "give" : "use") + " FAILED: " + why
                      + "; back to the Party screen");
        s_note = "Couldn't " + std::string(s_job.kind == ItemJob::Give ? "give " : "use ") + s_job.item.name;
        MakePartyBagBackAction(s_action);
        Start(Stage::BagBack, sceneManager, "Going back...", log);
    }

    static void Seek(u32 sceneManager, std::vector<std::string> &log)
    {
        // The Bag opens on the pocket it showed last; the tab tap is
        // sometimes not seen while it slides in: tap again.
        const BagGame game = ReadBagGame(sceneManager);
        if (game.Valid() && game.tab >= 0 && std::string(BagTabAt(game.tab).button) != PartyPocketButton(s_job.pocket))
        {
            if (++s_job.tabTries > 3)
            {
                BackOut(sceneManager, std::string("the Bag stays on ") + BagTabAt(game.tab).name, log);
                return;
            }
            log.push_back(std::string("party-ui: the Bag shows ") + BagTabAt(game.tab).name + ", tapping "
                          + PartyPocketName(s_job.pocket) + " again");
            MakePartyBagTabAction(s_action, s_job.pocket);
            Start(Stage::BagSeek, sceneManager, s_busy, log);
            return;
        }
        // Where the item is in the pocket as the Bag lists it (the native
        // Bag's reader: the Held Item bag's list object at scene +0x88 kept
        // counting 9 in a 10-item pocket, so P2-3's ReadBagList isn't used).
        // Found by ID: a taken item goes to the end of its pocket.
        if (!game.Valid() || game.mode != BagMode::List)
        {
            if (++s_job.seekWait > 90)
                BackOut(sceneManager, "the Bag's list doesn't read", log);
            return;
        }
        int target = -1;
        for (int i = 0; i < (int)game.items.size() && target < 0; i++)
            if (game.items[i].id == s_job.item.id)
                target = i;
        if (target < 0)
        {
            if (++s_job.seekWait <= 60)
                return;     // the pocket may still be changing
            BackOut(sceneManager, Utils::Format("%s isn't in the Bag's %s list (%u items)", s_job.item.name.c_str(),
                                                PartyPocketName(s_job.pocket), game.items.size()), log);
            return;
        }
        // The list scrolls smoothly; wait for it to settle on a row.
        if (game.scroll < 0)
            return;
        const int count = (int)game.items.size();
        const PartyBagPlan plan = PlanPartyBag(target, game.scroll, game.visible, count);
        if (plan.row >= 0)
        {
            log.push_back(Utils::Format("party-ui: bag item %d of %d, scroll %d row %d, tapping row %d", target + 1,
                                        count, game.scroll, game.cursorRow + 1, plan.row + 1));
            MakePartyBagRowAction(s_action, plan.row);
            Start(Stage::BagRow, sceneManager, s_busy, log);
        }
        else if (plan.dir != 0 && ++s_job.steps <= 2 * count)
        {
            MakePartyBagStepAction(s_action, plan.dir < 0);
            Start(Stage::BagSeek, sceneManager, s_busy, log);
        }
        else
            BackOut(sceneManager, "the item didn't come into view", log);
    }

    // The item menu is up for our item: tap Use / Give.
    static void ChooseInMenu(u32 sceneManager, std::vector<std::string> &log)
    {
        const BagGame g = ReadBagGame(sceneManager);
        if (g.mode != BagMode::Menu || g.item != s_job.item.id)
        {
            if (++s_job.menuWait > 90)
                BackOut(sceneManager, Utils::Format("the item menu for %s didn't open", s_job.item.name.c_str()), log);
            return;
        }
        std::string entries;
        for (u8 c : g.commands)
            entries += Utils::Format(" %02X", c);
        const bool give = s_job.kind == ItemJob::Give;
        const int at = PartyItemMenuEntry(g.commands, give);
        if (at < 1 || at > 5)
        {
            BackOut(sceneManager, Utils::Format("%s can't be %s here (menu%s)", s_job.item.name.c_str(),
                                                give ? "given" : "used", entries.c_str()), log);
            s_note = give ? s_job.item.name + " can't be held" : s_job.item.name + " can't be used on " + s_job.before.name;
            return;
        }
        log.push_back(Utils::Format("party-ui: item menu%s, %s is %d from the bottom", entries.c_str(),
                                    give ? "Give" : "Use", at));
        MakePartyBagMenuAction(s_action, at);
        s_job.game = PartyGame::Gap;
        s_job.stable = s_job.sinceProgress = 0;
        Start(Stage::FollowAct, sceneManager, s_busy, log);
    }

    // --- Following the game after Use / Give ---

    static PartyGame ReadGame(u32 sceneManager, const std::string &scene, u32 &signature, BagGame &bag,
                              TeachScreenState &teach)
    {
        signature = 0;
        if (scene.empty())
            return PartyGame::Gap;
        if (scene == SceneParty)
            return (ActionProbeFlags() & WorldPartyList) ? PartyGame::PartyList : PartyGame::PartyOther;
        if (scene == SceneTeach)
        {
            teach = ReadTeachScreen(sceneManager);
            signature = teach.newMove;
            return !teach.valid ? PartyGame::Other : teach.confirm ? PartyGame::TeachPage : PartyGame::Teach;
        }
        if (scene != SceneBag)
            return PartyGame::Other;
        bag = ReadBagGame(sceneManager);
        signature = bag.Signature();
        switch (bag.mode)
        {
        case BagMode::List:    return PartyGame::BagList;
        case BagMode::Menu:    return bag.YesNo() ? PartyGame::BagYesNo : PartyGame::BagMenu;
        case BagMode::Message: return PartyGame::BagMessage;
        default:               return PartyGame::BagBusy;
        }
    }

    // The PP items that ask which move (Ether, Max Ether, PP Up, PP Max).
    static bool AsksForMove(u16 item)
    {
        return item == 38 || item == 39 || item == 51 || item == 53;
    }

    static void ShowAsk(const BagGame &bag, const TeachScreenState &teach, PartyGame game, std::vector<std::string> &log)
    {
        ItemJob &j = s_job;
        s_ask = PartyAskView();
        s_askCancel = -1;
        const PartyMonView &mon = j.pos < Count() ? s_mons[j.pos] : j.before;
        if (game == PartyGame::Teach)
        {
            j.ask = ItemJob::AskTeach;
            j.teachMove = teach.newMove;
            const std::string move = MoveNameOf(teach.newMove);
            s_ask.title = mon.name + " wants to learn " + move + ".\nForget which move?";
            s_ask.moves = true;
            for (int i = 0; i < 4; i++)
            {
                PartyChoice c;
                c.move = mon.moves[i];
                c.pp = mon.pp[i];
                c.label = mon.moves[i] ? MoveNameOf(mon.moves[i]) : std::string("-");
                s_ask.choices.push_back(c);
            }
            PartyChoice skip;
            skip.label = "Don't learn " + move;
            s_ask.choices.push_back(skip);
            log.push_back(Utils::Format("party-ui: teach screen: %s wants %s (%u, param %u); knows ", mon.name.c_str(),
                                        move.c_str(), teach.newMove, teach.pokeId) + MovesText(mon));
        }
        else if (game == PartyGame::BagYesNo)
        {
            j.ask = ItemJob::AskYesNo;
            s_ask.title = j.kind == ItemJob::Give && !j.before.item.empty()
                          ? mon.name + " is already holding " + j.before.item + ".\nSwitch it for the " + j.item.name + "?"
                          : "The game asks about the " + j.item.name + ":\nYes or No? (L+R shows it)";
            s_ask.choices = { PartyChoice{ "Yes" }, PartyChoice{ "No" } };
            s_askCancel = 1;
            log.push_back("party-ui: ask yes/no: " + s_ask.title.substr(0, s_ask.title.find('\n')));
        }
        else
        {
            // A menu we didn't open: which move (PP items), or something new.
            j.ask = ItemJob::AskMenu;
            const int n = (int)bag.commands.size();
            j.menuEntries = n;
            int moves = 0;
            for (int i = 0; i < 4; i++)
                moves += mon.moves[i] != 0;
            std::string entries;
            for (u8 c : bag.commands)
                entries += Utils::Format(" %02X", c);
            if (AsksForMove(j.item.id) && n == moves + 1)
            {
                s_ask.title = "Restore PP of which move?";
                s_ask.moves = true;
                for (int i = 0; i < moves; i++)
                {
                    PartyChoice c;
                    c.move = mon.moves[i];
                    c.pp = mon.pp[i];
                    c.label = MoveNameOf(mon.moves[i]);
                    s_ask.choices.push_back(c);
                }
                s_ask.choices.push_back(PartyChoice{ "Cancel" });
            }
            else
            {
                s_ask.title = "The game asks about the " + j.item.name + "\n(L+R shows the bottom screen)";
                for (int i = 0; i < n; i++)
                    s_ask.choices.push_back(PartyChoice{ bag.commands[i] == 0x11 ? std::string("Cancel")
                                                         : Utils::Format("Option %d", i + 1) });
            }
            if (n > 0 && bag.commands[n - 1] == 0x11)
                s_askCancel = n - 1;
            log.push_back(Utils::Format("party-ui: ask menu (%d entries:%s): ", n, entries.c_str()) + s_ask.title.substr(0, s_ask.title.find('\n')));
        }
        s_menu.Ask(0);
        s_stage = Stage::Asking;
        s_busy.clear();
    }

    static void Answer(int choice, u32 sceneManager, std::vector<std::string> &log)
    {
        ItemJob &j = s_job;
        if (choice < 0 || choice >= (int)s_ask.choices.size())
            return;
        log.push_back(Utils::Format("party-ui: answer %d (%s)", choice + 1, s_ask.choices[choice].label.c_str()));
        const PartyMonView &mon = j.pos < Count() ? s_mons[j.pos] : j.before;
        switch (j.ask)
        {
        case ItemJob::AskTeach:
            if (choice < 4 && mon.moves[choice] == 0)
            {
                s_menu.Ask(choice);     // an empty slot: nothing to forget
                return;
            }
            j.learned = choice < 4 ? mon.name + " forgot " + MoveNameOf(mon.moves[choice]) + " and learned "
                                     + MoveNameOf(j.teachMove)
                                   : mon.name + " did not learn " + MoveNameOf(j.teachMove);
            MakePartyTeachAction(s_action, choice);
            break;
        case ItemJob::AskYesNo:
            MakePartyYesNoAction(s_action, choice == 0);
            break;
        default:
            MakePartyBagMenuAction(s_action, j.menuEntries - choice);
            break;
        }
        j.ask = ItemJob::AskNone;
        j.stable = j.sinceProgress = 0;
        Start(Stage::FollowAct, sceneManager, "Telling the game...", log);
    }

    static void Follow(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        ItemJob &j = s_job;
        u32 signature = 0;
        BagGame bag;
        TeachScreenState teach;
        const PartyGame game = ReadGame(sceneManager, scene, signature, bag, teach);
        if (game != j.game || signature != j.signature)
        {
            j.game = game;
            j.signature = signature;
            j.stable = 0;
            j.sinceProgress = 0;
        }
        j.stable++;
        j.sinceProgress++;

        if (s_stage == Stage::Asking)
        {
            // The game moved on by itself (or the player used the panel).
            if (j.stable == 1)
            {
                s_menu.EndAsk();
                s_stage = Stage::Follow;
                log.push_back("party-ui: the game moved on; ask closed");
            }
            return;
        }

        const PartyFollowDo next = PartyFollow(game, j.stable);
        switch (next)
        {
        case PartyFollowDo::Done:
            s_stage = Stage::Idle;
            FinishItem(sceneManager, true, log);
            return;
        case PartyFollowDo::Advance:
            MakePartyMessageAction(s_action, game == PartyGame::BagMessage);
            break;
        case PartyFollowDo::LeaveBag:
            MakePartyBagBackAction(s_action);
            break;
        case PartyFollowDo::Ask:
        case PartyFollowDo::Teach:
            if (game == PartyGame::BagMenu && (bag.commands.empty() || bag.commands.size() > 5))
                break;      // nothing the plugin can tap; waits, then steps aside
            ShowAsk(bag, teach, game, log);
            return;
        default:
            break;
        }
        if (next == PartyFollowDo::Advance || next == PartyFollowDo::LeaveBag)
        {
            if (++j.actions > 40)
            {
                log.push_back("party-ui: follow FAILED: too many steps; the panel (L+R) can finish it");
                s_stage = Stage::Idle;
                s_yield = true;
                s_busy.clear();
                return;
            }
            j.stable = 0;
            Start(Stage::FollowAct, sceneManager, s_busy, log);
            return;
        }
        const u32 limit = game == PartyGame::Other || game == PartyGame::Gap ? FollowOtherFrames : FollowStuckFrames;
        if (j.sinceProgress > limit)
        {
            static const char *const names[] = { "gap", "party list", "party", "bag list", "bag menu", "bag yes/no",
                                                 "bag message", "bag busy", "teach", "teach page", "other" };
            log.push_back(std::string("party-ui: follow FAILED: stuck on ") + names[(int)game] + " (scene "
                          + (scene.empty() ? "(none)" : scene) + "); the native screen steps aside (L+R panel)");
            s_stage = Stage::Idle;
            s_yield = true;
            s_busy.clear();
            s_note = "Finish on the bottom screen (L+R)";
        }
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
            s_job.pos = cmd.pos;
            s_job.index = cmd.other;
            s_swapBefore = s_mons;
            Start(Stage::Swap, sceneManager, "Moving " + NameAt(cmd.pos) + "...", log);
            break;

        case PartyCommand::UseItem:
        case PartyCommand::GiveItem:
            StartItem(cmd.kind == PartyCommand::GiveItem ? ItemJob::Give : ItemJob::Use, cmd.pos, cmd.pocket, cmd.item,
                      sceneManager, log);
            break;

        case PartyCommand::TakeItem:
            if (cmd.pos < 0 || cmd.pos >= Count() || s_mons[cmd.pos].itemId == 0)
                break;
            s_job = ItemJob();
            s_job.kind = ItemJob::Take;
            s_job.pos = cmd.pos;
            s_job.before = s_mons[cmd.pos];
            for (int p = 0; p < (int)PartyPocket::Count; p++)
                if (CountOf(s_items[p], s_job.before.itemId) != 0)
                    s_job.pocketBefore = s_items[p];
            log.push_back(Utils::Format("party-ui: take %s from %d (%s)", s_job.before.item.c_str(), cmd.pos + 1,
                                        s_job.before.name.c_str()));
            MakePartyTakeAction(s_action, cmd.pos);
            Start(Stage::Take, sceneManager, "Taking the " + s_job.before.item + " from " + s_job.before.name + "...", log);
            break;

        case PartyCommand::Answer:
            Answer(cmd.choice, sceneManager, log);
            break;

        default:
            break;
        }
    }

    // --- Stages after an action ---

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
            const int a = s_job.pos, b = s_job.index;
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

        case Stage::Take:
            FinishItem(sceneManager, onList, log);
            break;

        case Stage::BagOpen:
            if (scene != SceneBag)
            {
                s_busy.clear();
                s_note = "Couldn't open the Bag";
                log.push_back("party-ui: item FAILED: the Bag didn't open (scene " + scene + ")");
                break;
            }
            s_stage = Stage::BagSeek;
            break;

        case Stage::BagSeek:
            s_stage = Stage::BagSeek;   // keep going (Seek runs every frame)
            break;

        case Stage::BagRow:
            s_job.menuWait = 0;
            s_stage = Stage::BagMenu;
            break;

        case Stage::FollowAct:
            s_stage = Stage::Follow;
            break;

        case Stage::BagBack:
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
        view.giving = s_menu.Giving();
        view.itemCursor = s_menu.ItemCursor();
        if (view.page == PartyPage::Items)
            view.items = s_items[(int)view.pocket];
        if (view.page == PartyPage::Ask)
        {
            view.ask = s_ask;
            view.askCursor = s_menu.AskCursor();
        }
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
            u32 loader = 0;
            const int slot = s_order[view.pos] < s_party.count ? s_order[view.pos] : view.pos;
            const Pk7 &pk = s_party.slots[slot].mon;
            for (int i = 0; i < 4; i++)
                if (view.mon.moves[i] != 0)
                    view.mon.moveNames[i] = LoaderName(s_moveNames, sceneManager, loader, SummaryMoveDat, view.mon.moves[i]);
            view.mon.ability = LoaderName(s_abilityNames, sceneManager, loader, SummaryAbilityDat, pk.ability);
            view.mon.nature = LoaderName(s_natureNames, sceneManager, loader, SummaryNatureDat, pk.nature);

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

    bool PartyScreenUsingBag(void)
    {
        return s_stage >= Stage::BagOpen;
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
        const bool following = s_stage >= Stage::Follow && s_stage <= Stage::Asking;
        const bool inBag = scene == SceneBag && ours;
        const bool inTeach = scene == SceneTeach && following;
        // Another scene while following a Use (an evolution): keep the panel
        // off but let the game's own top screen show.
        const bool elsewhere = following && !scene.empty() && !inParty && !inBag && !inTeach;
        // The loading gap between our scenes (Party <-> Status / Bag / teaching).
        const bool gap = scene.empty() && (s_lastScene == SceneParty || (s_lastScene == SceneStatus && s_fromParty)
                                           || ((s_lastScene == SceneBag || following) && ours));
        const bool expecting = s_frames < s_expectUntil;
        const bool here = inParty || inStatus || inBag || inTeach || elsewhere || gap;
        if (!here && !expecting)
            s_yield = false;

        HoldPanel(s_enabled && !s_yield && (here || expecting), scene != s_prevScene, log);

        const bool shown = s_enabled && !s_yield && here;
        if (shown && !s_shown)
        {
            // The layer is hidden here, so the OSD callback isn't using the
            // buffer: (re)allocate it if it is missing or too small.
            UI::RequestAssets();    // the game's font, icons and move table for the drawing
            s_menu.Reset(6);
            s_note.clear();
            s_repeatKey = 0;
            s_lastCursorLogged = -1;
            s_lastPageLogged = PartyPage::List;
            s_listKey.clear();
            ReadPartyNow(sceneManager, log);
            if (s_party.screenCursor >= 0)
                s_menu.SetCursor(s_party.screenCursor, Count());
            log.push_back(Utils::Format("party-ui: shown (%s, cursor %d); ", scene.empty() ? "(none)" : scene.c_str(),
                                        s_menu.Cursor() + 1) + UI::ScreenBuffer::HeapStatus());
        }
        else if (!shown && s_shown)
        {
            log.push_back("party-ui: hidden; " + UI::ScreenBuffer::HeapStatus());
            if (s_stage != Stage::Idle && !ActionRunning())
                s_stage = Stage::Idle;
            s_menu.EndAsk();
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
            if (s_stage == Stage::BagSeek && scene == SceneBag)
                Seek(sceneManager, log);
            else if (s_stage == Stage::BagMenu && scene == SceneBag)
                ChooseInMenu(sceneManager, log);
            else if (s_stage == Stage::Follow || s_stage == Stage::Asking)
                Follow(sceneManager, scene, log);
            else if (s_stage != Stage::Idle && s_stage != Stage::BagSeek && s_stage != Stage::BagMenu)
                AfterAction(sceneManager, log);
            else if ((s_stage == Stage::BagSeek || s_stage == Stage::BagMenu) && !scene.empty())
            {
                s_stage = Stage::Idle;
                s_busy.clear();
                log.push_back("party-ui: item FAILED: left the Bag (scene " + scene + ")");
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
            const bool owning = !ActionRunning()
                                && ((inParty && s_stage == Stage::Idle && (ActionProbeFlags() & WorldPartyList))
                                    || s_stage == Stage::Asking);
            if (owning)
            {
                SetCapture(s_menu.Keys() | (u32)Key::CPad);
                HandleInput(sceneManager, log);
                SetCapture(s_menu.Keys() | (u32)Key::CPad);
            }
            else if (!ActionRunning() && (s_stage == Stage::Idle || elsewhere))
                SetCapture(0);
            PublishParty();
            s_showLayer = !elsewhere;
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
        static const char *const stages[] = { "idle", "summary", "swap", "take", "bag-open", "bag-seek", "bag-row",
                                              "bag-menu", "follow", "follow-act", "asking", "bag-back" };
        static const char *const pages[] = { "list", "menu", "swap", "items", "ask" };
        return Utils::Format("party-ui: %s, %s, page %s, cursor %d, stage %s%s%s; buffer %s, ", s_enabled ? "on" : "off",
                             s_shown ? "shown" : "hidden", pages[(int)s_menu.Page()], s_menu.Cursor() + 1,
                             stages[(int)s_stage], s_yield ? ", panel on by hand" : "",
                             s_captureKeys ? ", input captured" : "",
                             UI::ScreenBuffer::Holder() == BufferOwner ? "held" : "not held") + UI::ScreenBuffer::HeapStatus();
    }
}
