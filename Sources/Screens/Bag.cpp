#include "Usum.hpp"
#include "Screens/Bag.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Framebuffer.hpp"
#include "Panel.hpp"
#include "Party.hpp"
#include "Scene.hpp"
#include "Screens/PartyScreen.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // --- The game's field Bag (USUM v1.2; found and checked as described in
    // docs/BAG.md) ---

    // Scene object (main image), top of the scene manager's stack.
    static const u32 VtBagScene = Usum::VtBagScene;
    static const u32 SceneList = 0x88, SceneFrame = 0x90;
    // Item list window: rows shown, cursor row, first row (float, animates).
    static const u32 ListVisible = 0x14, ListCursor = 0x18, ListScroll = 0x1C;
    // Bag frame (Bag CRO): the open pocket as pointers to MyItem slots, mode bytes, param.
    static const u32 VtBagFrame = Usum::BagFieldFrame;
    static const u32 FrameItems = 0x24, FrameItemCount = 0x28, FrameParam = 0x2C, FrameFlags = 0x4C, FrameMode = 0x54;
    // Bag param: MyItem, the item the menu is about, tab, party column, menu.
    static const u32 ParamMyItem = 0x24, ParamItem = 0x4C, ParamTab = 0x54, ParamPartyList = 0x64;
    static const u32 ParamMenu = 0x80, ParamCommands = 0x84, ParamState = 0xEC;
    static const u32 VtMyItem = Usum::VtMyItem, MyItemEnd = 0xE2C;
    // Party column (Bag CRO): cursor; menu (main image): entry count.
    static const u32 VtPartyList = Usum::BagPartyList, PartyListCursor = 0x1C6;
    static const u32 VtMenu = Usum::BagMenu, MenuCount = 0x158;
    static const u32 MaxItems = 430, MaxCommands = 8;

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        Process::Read32(address, v);
        return v;
    }

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    // The Bag CRO's load address, for the module-relative vtables.
    static u32 BagModule(u32 vtable)
    {
        static u32 s_vtable = 0, s_module = 0;
        if (vtable != s_vtable)
        {
            s_vtable = vtable;
            const u32 module = CroModuleOf(vtable);
            s_module = CroModuleName(module) == "Bag" ? module : 0;
        }
        return s_module;
    }

    BagGame ReadBagGame(u32 sceneManager)
    {
        BagGame g;
        const u32 manager = Read32(sceneManager);
        const u32 scene = manager ? Read32(manager) : 0;
        if (scene == 0 || Read32(scene) != VtBagScene)
            return g;
        const u32 frame = Read32(scene + SceneFrame);
        const u32 frameVt = frame ? Read32(frame) : 0;
        const u32 module = frameVt ? BagModule(frameVt) : 0;
        if (module == 0 || frameVt != module + VtBagFrame)
            return g;
        const u32 param = Read32(frame + FrameParam);
        const u32 myItem = param ? Read32(param + ParamMyItem) : 0;
        if (myItem == 0 || Read32(myItem) != VtMyItem)
            return g;

        // Mode: frame +0x54 byte 0 = a menu or message is up, byte 1 = the
        // party pick; frame +0x4C byte 3 is 0 while a message waits. The
        // param's state byte is 0 only in the list (2 / 3 while a Use / Give
        // runs, also during the HP bar animation, when +0x54 is all 0).
        const u32 flags = Read32(frame + FrameFlags), mode = Read32(frame + FrameMode);
        const bool up = (mode & 0xFF) != 0, pick = ((mode >> 8) & 0xFF) != 0, input = (flags >> 24) != 0;
        if (pick)
            g.mode = BagMode::Pick;
        else if (up)
            g.mode = input ? BagMode::Menu : BagMode::Message;
        else if ((mode & 0xFFFF) == 0 && Read8(param + ParamState) == 0)
            g.mode = BagMode::List;
        else
            g.mode = BagMode::Busy;

        g.tab = Read8(param + ParamTab);
        if (g.tab >= BagTabCount)
            g.tab = -1;
        g.item = (u16)Read32(param + ParamItem);

        const u32 items = Read32(frame + FrameItems);
        u32 count = Read32(frame + FrameItemCount);
        if (count > MaxItems)
            count = 0;
        g.items.reserve(count);
        for (u32 i = 0; i < count && items != 0; i++)
        {
            const u32 slot = Read32(items + i * 4);
            if (slot < myItem + 4 || slot >= myItem + MyItemEnd)
                break;
            const BagSlot s = DecodeBagSlot(Read32(slot));
            BagItem item;
            item.id = s.item;
            item.count = s.count;
            item.isNew = s.isNew;
            g.items.push_back(item);
        }

        const u32 list = Read32(scene + SceneList);
        if (list != 0)
        {
            g.visible = (int)Read32(list + ListVisible);
            g.cursorRow = (int)Read32(list + ListCursor);
            float scroll = 0;
            const u32 bits = Read32(list + ListScroll);
            std::memcpy(&scroll, &bits, sizeof(scroll));
            const int whole = (int)(scroll + 0.5f);
            const float off = scroll - (float)whole;
            g.scroll = off > -0.001f && off < 0.001f && whole >= 0 ? whole : -1;
            if (g.visible < 0 || g.visible > 7 || g.cursorRow < 0 || g.cursorRow > 7)
                g.scroll = -1;
        }

        const u32 partyList = Read32(param + ParamPartyList);
        if (partyList != 0 && Read32(partyList) == module + VtPartyList)
        {
            const int c = Read8(partyList + PartyListCursor);
            g.partyCursor = c < 6 ? c : -1;
        }

        const u32 menu = Read32(param + ParamMenu);
        if (g.mode == BagMode::Menu && menu != 0 && Read32(menu) == VtMenu)
        {
            u16 n = 0;
            Process::Read16(menu + MenuCount, n);
            for (u32 i = 0; i < n && i < MaxCommands; i++)
                g.commands.push_back(Read8(param + ParamCommands + i));
        }
        return g;
    }

    // --- Shared with the OSD callback (game thread) ---
    // Handover without a lock: the plugin thread writes s_view only while
    // the callback has drawn the latest version, then bumps s_viewVersion;
    // the callback reads s_view only while the versions differ.
    static BagView       s_view;
    static volatile u32  s_viewVersion = 0; // bumped whenever s_view changes
    static volatile u32  s_drawnVersion = 0;
    static volatile bool s_shown = false;

    // --- Plugin thread ---
    static const u32 MoveUp    = Key::DPadUp | Key::CPadUp;
    static const u32 MoveDown  = Key::DPadDown | Key::CPadDown;
    static const u32 MoveLeft  = Key::DPadLeft | Key::CPadLeft;
    static const u32 MoveRight = Key::DPadRight | Key::CPadRight;
    // Everything the game's Bag reacts to except Select (the plugin menu):
    // its X (sort) and Y (move) would open screens the native Bag doesn't know.
    static const u32 CaptureKeys = Key::A | Key::B | Key::X | Key::Y | Key::L | Key::R | Key::Start
                                 | Key::DPadUp | Key::DPadDown | Key::DPadLeft | Key::DPadRight;
    static const u32 RepeatDelay = 15, RepeatEvery = 4;
    // After the X menu taps Bag, hold the panel off this long for the Bag to show.
    static const u32 ExpectFrames = 240;
    static const u32 PartyEvery = 30;
    static const u32 StatusFrames = 300;

    static bool s_enabled = true;
    static bool s_forcedOff = false;
    static bool s_yield = false;
    static bool s_capturing = false;
    static u32  s_frames = 0;
    static u32  s_expectUntil = 0;
    static std::string s_prevScene;
    static u32  s_repeatKey = 0, s_repeatAt = 0;
    static BagDriver s_driver;
    static BagGame   s_game;
    static BagMode   s_prevMode = BagMode::None;
    static int  s_prevTab = -1;
    static int  s_cursor = 0, s_top = 0;
    static int  s_wantTab = -1;
    static int  s_menuCursor = 0;
    static u32  s_menuSignature = 0;
    static int  s_pickCursor = 0;
    static u32  s_partyAt = 0;
    static std::vector<BagMon> s_party;
    static PartyState s_partyState;
    static std::string s_status;
    static u32  s_statusUntil = 0;
    static std::map<u16, std::string> s_names;

    // A choice from the item menu in progress (Use, Give, Free Space,
    // On / Off), for the result line.
    struct Flow
    {
        bool active = false;
        u8 command = 0;
        u16 item = 0;
        int countBefore = 0;
        int slot = -1;
        PartyState before;
    };
    static Flow s_flow;
    // The game left the Bag screen during a flow (a TM's move-forget screen,
    // an evolution): that scene, until the Bag is back.
    static std::string s_handedTo;
    // Stepped aside during a flow (a screen the plugin doesn't read): the
    // panel shows until the game is back in its list.
    static bool s_aside = false;
    static u32  s_busyFrames = 0;

    // --- Pocket icons and descriptions (loaded once, in a Task) ---

    // Current text language (0-9, 2 = English), as in Screens/Map.cpp.
    static const u32 LanguageByte = Usum::LanguageByte;
    static BagAssets *s_assets = nullptr;       // set once loaded, never freed or changed
    static std::atomic<int> s_assetsState(0);   // 0 not started, 1 loading, 2 done, 3 logged
    static std::vector<std::string> s_assetsLog;
    static Task *s_assetsTask = nullptr;
    static u32 s_assetsMs = 0;
    static Handle s_romfs = 0;                  // the RomFS, open for descriptions
    static std::map<u16, std::string> s_descriptions;

    static s32 LoadAssetsTask(void *)
    {
        const u64 start = svcGetSystemTick();
        u8 lang = 0xFF;
        Process::Read8(LanguageByte, lang);
        static const u32 lowPath[3] = { 0, 0, 0 };
        const FS_Path archive = { PATH_EMPTY, 1, "" };
        const FS_Path file = { PATH_BINARY, sizeof(lowPath), lowPath };
        Handle handle = 0;
        const Result res = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
        BagAssets *assets = R_SUCCEEDED(res) ? new (std::nothrow) BagAssets() : nullptr;
        if (R_FAILED(res))
            s_assetsLog.push_back(Utils::Format("bag assets: can't open the RomFS (0x%08lX), drawn icons", (u32)res));
        else if (assets == nullptr)
            s_assetsLog.push_back("bag assets: no memory, drawn icons");
        else
        {
            const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
                u32 got = 0;
                return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
            };
            LoadBagAssets(read, lang, *assets, s_assetsLog);
        }
        // Kept open for the descriptions, read one at a time when shown.
        if (assets != nullptr && assets->descriptionsSize != 0)
            s_romfs = handle;
        else if (R_SUCCEEDED(res))
            FSFILE_Close(handle);
        s_assetsMs = (u32)((svcGetSystemTick() - start) / CPU_TICKS_PER_MSEC);
        s_assets = assets;
        __sync_synchronize();   // complete before the plugin thread sees it
        s_assetsState.store(2, std::memory_order_release);
        return 0;
    }

    // The load needs about 120 KB while it runs (the RomFS tables, the
    // archive's header, the icons). The plugin is built without exceptions,
    // so running out of heap in it would abort the plugin: it only starts
    // with this much free (probed, then given back).
    static const size_t AssetsHeadroom = 192 * 1024;

    // Starts the load the first time the Bag shows, if there is room
    // and room to spare; writes its log once done.
    static void AssetsUpdate(bool shown, std::vector<std::string> &log)
    {
        const int state = s_assetsState.load(std::memory_order_acquire);
        if (state == 0 && shown)
        {
            u8 *probe = new (std::nothrow) u8[AssetsHeadroom];
            if (probe == nullptr)
            {
                log.push_back("bag assets: not loaded, less than 192 KB of heap free; drawn icons, no descriptions");
                s_assetsState.store(3);
                return;
            }
            delete[] probe;
            s_assetsTask = new (std::nothrow) Task(LoadAssetsTask, nullptr, -1);
            if (s_assetsTask == nullptr)
            {
                log.push_back("bag assets: no memory for the load task, drawn icons");
                s_assetsState.store(3);
                return;
            }
            s_assetsState.store(1);
            s_assetsTask->Start();
        }
        else if (state == 2)
        {
            for (const std::string &line : s_assetsLog)
                log.push_back(line);
            s_assetsLog = std::vector<std::string>();
            log.push_back(Utils::Format("bag assets: loaded in %lu ms", s_assetsMs));
            s_assetsState.store(3);
        }
    }

    static const BagAssets *Assets(void)
    {
        return s_assetsState.load(std::memory_order_acquire) >= 2 ? s_assets : nullptr;
    }

    // An item's description, read from the RomFS the first time (a few
    // hundred bytes) and kept.
    static const std::string &Description(u16 item)
    {
        auto it = s_descriptions.find(item);
        if (it != s_descriptions.end())
            return it->second;
        const BagAssets *assets = Assets();
        std::string text;
        if (assets != nullptr && s_romfs != 0)
        {
            const Handle handle = s_romfs;
            const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
                u32 got = 0;
                return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
            };
            text = BagReadDescription(read, *assets, item);
        }
        return s_descriptions.emplace(item, text).first->second;
    }

    static const std::string &Name(u16 item)
    {
        auto it = s_names.find(item);
        if (it == s_names.end())
            it = s_names.emplace(item, ItemName(item)).first;
        return it->second;
    }

    static std::string NameOr(u16 item)
    {
        const std::string &name = Name(item);
        return name.empty() ? Utils::Format("#%u", item) : name;
    }

    static std::string MonName(const Pk7 &mon)
    {
        char buf[64];
        Utf16ToUtf8(mon.nickname, 13, buf, sizeof(buf));
        return buf;
    }

    static void ReadPartyNow(u32 sceneManager)
    {
        s_partyState = ReadParty(sceneManager);
        s_partyAt = s_frames;
        s_party.clear();
        if (!s_partyState.valid)
            return;
        for (u32 i = 0; i < s_partyState.count && i < 6; i++)
        {
            const Pk7 &mon = s_partyState.slots[i].mon;
            BagMon m;
            if (mon.form != Pk7Form::Invalid)
            {
                m.name = MonName(mon);
                m.level = mon.level;
                m.hp = mon.hp;
                m.maxHp = mon.maxHp;
                m.egg = mon.isEgg;
                if (mon.heldItem != 0)
                    m.held = NameOr(mon.heldItem);
            }
            else
                m.name = "?";
            s_party.push_back(m);
        }
    }

    static int CountOf(const BagGame &g, u16 item)
    {
        for (const BagItem &i : g.items)
            if (i.id == item)
                return i.count;
        return 0;
    }

    static void SetStatus(const std::string &text, u32 frames = StatusFrames)
    {
        s_status = text;
        s_statusUntil = s_frames + frames;
    }

    static const char *FlowName(u8 command)
    {
        switch (command)
        {
        case BagCommand::Use:            return "use";
        case BagCommand::Give:           return "give";
        case BagCommand::FreeSpace:      return "free-space";
        case BagCommand::ReturnToPocket: return "return";
        case BagCommand::On:             return "on";
        case BagCommand::Off:            return "off";
        }
        return "other";
    }

    static std::string MoveNameOr(u16 move)
    {
        return UI::AssetMoveName(move, Utils::Format("move %u", move));
    }

    static BagResult BaseResult(int countAfter)
    {
        BagResult r;
        r.command = s_flow.command;
        r.kind = BagItemKindOf(s_flow.item);
        r.item = NameOr(s_flow.item);
        r.countBefore = s_flow.countBefore;
        r.countAfter = countAfter;
        return r;
    }

    // The flow got back to the list (or to the party pick, after "It won't
    // have any effect"): say what changed.
    static void FinishFlow(u32 sceneManager, std::vector<std::string> &log)
    {
        if (!s_flow.active)
            return;
        s_flow.active = false;
        ReadPartyNow(sceneManager);
        const int countAfter = CountOf(s_game, s_flow.item);
        const std::string item = NameOr(s_flow.item);
        const int slot = s_flow.slot;
        if (slot < 0 || slot >= (int)s_partyState.count || slot >= (int)s_flow.before.count)
        {
            log.push_back(Utils::Format("bag: result %s %s(%u): count %d -> %d, no Pokémon chosen",
                                        FlowName(s_flow.command), item.c_str(), s_flow.item, s_flow.countBefore,
                                        countAfter));
            const std::string text = BagResultText(BaseResult(countAfter));
            if (!text.empty())
                SetStatus(text);
            return;
        }
        const Pk7 &a = s_flow.before.slots[slot].mon, &b = s_partyState.slots[slot].mon;
        BagResult r = BaseResult(countAfter);
        r.picked = true;
        r.mon = MonName(b);
        r.hpBefore = a.hp;
        r.hpAfter = b.hp;
        r.maxHp = b.maxHp;
        r.levelBefore = a.level;
        r.levelAfter = b.level;
        r.speciesBefore = a.species;
        r.speciesAfter = b.species;
        if (a.species != b.species)
            r.speciesName = SpeciesName(b.species);
        std::string movesA, movesB;
        for (int m = 0; m < 4; m++)
        {
            r.ppBefore += a.pp[m];
            r.ppAfter += b.pp[m];
            movesA += Utils::Format("%s%u", m ? "," : "", a.moves[m]);
            movesB += Utils::Format("%s%u", m ? "," : "", b.moves[m]);
            bool known = b.moves[m] == 0;
            for (int k = 0; k < 4 && !known; k++)
                known = a.moves[k] == b.moves[m];
            if (!known)
                r.learned.push_back(MoveNameOr(b.moves[m]));
        }
        r.heldBefore = a.heldItem ? NameOr(a.heldItem) : "";
        r.heldAfter = b.heldItem ? NameOr(b.heldItem) : "";
        r.holdsItem = b.heldItem == s_flow.item;
        log.push_back(Utils::Format("bag: result %s %s(%u) on slot %d %s: count %d -> %d, hp %u/%u -> %u/%u, held %s -> %s, "
                                    "level %u -> %u, moves %s -> %s, pp %d -> %d",
                                    FlowName(s_flow.command), item.c_str(), s_flow.item, slot + 1, r.mon.c_str(),
                                    s_flow.countBefore, countAfter, a.hp, a.maxHp, b.hp, b.maxHp,
                                    r.heldBefore.empty() ? "-" : r.heldBefore.c_str(),
                                    r.heldAfter.empty() ? "-" : r.heldAfter.c_str(), a.level, b.level, movesA.c_str(),
                                    movesB.c_str(), r.ppBefore, r.ppAfter));
        SetStatus(BagResultText(r));
    }

    // --- OSD callback ---

    static UI::Layer s_layer;
    static const char *const Owner = "bag";    // of the shared screen buffer

    static bool BagOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
            return false;
        if (!s_shown)
        {
            s_layer.Hide();
            UI::ScreenBuffer::GiveBack(Owner);
            return false;
        }
        bool fresh = false;
        u8 *const pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
        if (pixels == nullptr)
        {
            s_layer.Hide();
            return false;   // another screen still has it: next frame
        }
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        const u32 version = s_viewVersion;
        bool redraw = version != s_drawnVersion;
        if (fresh || !s_layer.Matches(fb))
        {
            if (!s_layer.SetupHiRes(fb, BagArea, BagRadius, UI::MenuSlot, pixels, UI::ScreenBuffer::Bytes))
                return false;
            redraw = true;
        }
        if (redraw)
        {
            __sync_synchronize();
            UI::Canvas canvas = s_layer.Begin();
            DrawBag(canvas, s_view);
            __sync_synchronize();
            s_drawnVersion = version;
        }
        return s_layer.Present(fb);
    }

    void StartBag(void)
    {
        OSD::Run(BagOnScreen);
    }

    // --- Plugin thread ---

    static bool SameMons(const std::vector<BagMon> &a, const std::vector<BagMon> &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].name != b[i].name || a[i].level != b[i].level || a[i].hp != b[i].hp || a[i].maxHp != b[i].maxHp
                || a[i].held != b[i].held || a[i].egg != b[i].egg)
                return false;
        return true;
    }

    static bool SameItems(const std::vector<BagItem> &a, const std::vector<BagItem> &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].id != b[i].id || a[i].count != b[i].count || a[i].isNew != b[i].isNew)
                return false;
        return true;
    }

    static void Publish(void)
    {
        BagView v;
        v.mode = s_game.mode;
        v.tab = s_game.tab;
        v.wantTab = s_driver.Goal() == BagGoal::Tab ? s_wantTab : -1;
        v.items = s_game.items;
        for (const BagItem &i : s_game.items)
            v.names.push_back(Name(i.id));
        v.cursor = s_cursor;
        v.top = s_top;
        v.commands = s_game.commands;
        v.menuCursor = s_menuCursor;
        if (s_game.YesNo())
            v.menuTitle = s_flow.active && s_flow.command == BagCommand::Give ? "Would you like to switch the two items?"
                                                                             : NameOr(s_game.item) + ": Yes or No?";
        else if (v.mode == BagMode::Menu)
            v.menuTitle = "What do you want to do with the " + NameOr(s_game.item) + "?";
        if (v.mode == BagMode::Pick)
            v.pickQuestion = s_flow.active && s_flow.command == BagCommand::Give
                             ? "Which Pokémon do you want to give it to?" : BagPickQuestion(s_game.item);
        v.assets = Assets();
        if (v.assets != nullptr && s_cursor >= 0 && s_cursor < (int)s_game.items.size())
            v.description = Description(s_game.items[s_cursor].id);
        v.party = s_party;
        v.partyCursor = s_pickCursor;
        v.busy = s_driver.Busy() || s_game.mode == BagMode::Message || s_game.mode == BagMode::Busy;
        if (s_frames < s_statusUntil)
            v.status = s_status;
        else if (s_driver.Busy())
            v.status = "...";

        const BagView &o = s_view;
        const bool same = o.mode == v.mode && o.tab == v.tab && o.wantTab == v.wantTab && SameItems(o.items, v.items)
                          && o.names == v.names && o.cursor == v.cursor && o.top == v.top && o.commands == v.commands
                          && o.menuCursor == v.menuCursor && o.menuTitle == v.menuTitle && o.pickQuestion == v.pickQuestion
                          && o.description == v.description && o.assets == v.assets && SameMons(o.party, v.party)
                          && o.partyCursor == v.partyCursor && o.status == v.status && o.busy == v.busy;
        // Not while the callback may still be drawing the last one: try
        // again next frame.
        if (same || s_drawnVersion != s_viewVersion)
            return;
        __sync_synchronize();
        s_view = v;
        __sync_synchronize();
        s_viewVersion = s_viewVersion + 1;
    }

    static u32 RepeatDir(u32 held, u32 pressed)
    {
        const u32 dirs[] = { MoveUp, MoveDown, MoveLeft, MoveRight };
        u32 dir = 0;
        for (u32 d : dirs)
            if (pressed & d)
                dir = d;
        if (dir != 0)
        {
            s_repeatKey = dir;
            s_repeatAt = s_frames + RepeatDelay;
        }
        else if (s_repeatKey != 0 && (held & s_repeatKey) && s_frames >= s_repeatAt)
        {
            dir = s_repeatKey;
            s_repeatAt = s_frames + RepeatEvery;
        }
        else if (!(held & s_repeatKey))
            s_repeatKey = 0;
        return dir;
    }

    static void MoveCursor(int delta, std::vector<std::string> &log)
    {
        const int count = (int)s_game.items.size();
        if (count == 0)
            return;
        s_cursor = BagMoveCursor(s_cursor, delta, count);
        s_top = BagListTop(s_top, s_cursor, count);
        log.push_back(Utils::Format("bag: cursor %d %s", s_cursor, NameOr(s_game.items[s_cursor].id).c_str()));
    }

    static void SwitchTab(int delta, std::vector<std::string> &log)
    {
        const int from = s_wantTab >= 0 ? s_wantTab : s_game.tab;
        const int to = (from + delta + BagTabCount) % BagTabCount;
        s_wantTab = to;
        s_driver.Tab(to);
        log.push_back(std::string("bag: pocket -> ") + BagTabAt(to).name);
    }

    static void ChooseEntry(u32 sceneManager, std::vector<std::string> &log)
    {
        if (s_menuCursor < 0 || s_menuCursor >= (int)s_game.commands.size())
            return;
        const u8 command = s_game.commands[s_menuCursor];
        if (!BagEntryEnabled(s_game.commands, s_menuCursor))
        {
            SetStatus(std::string(BagCommandLabel(command)) + ": use the bottom screen (L+R)", 180);
            log.push_back(std::string("bag: ") + BagCommandLabel(command) + " is not native");
            return;
        }
        if (command != BagCommand::Cancel)
        {
            s_flow = Flow();
            s_flow.active = true;
            s_flow.command = command;
            s_flow.item = s_game.item;
            s_flow.countBefore = CountOf(s_game, s_game.item);
            ReadPartyNow(sceneManager);
            s_flow.before = s_partyState;
        }
        else
            s_flow.active = false;
        log.push_back(Utils::Format("bag: choose %s (%s, %s)", BagCommandLabel(command), NameOr(s_game.item).c_str(),
                                    BagItemKindName(BagItemKindOf(s_game.item))));
        s_driver.Choose(s_menuCursor);
    }

    static void HandleInput(u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 held = CaptureRealHeld();
        const u32 pressed = CaptureRealPressed();
        const u32 dir = RepeatDir(held, pressed);
        if (s_driver.Busy())
            return;
        if (pressed != 0 && s_frames < s_statusUntil)
            s_statusUntil = 0;

        switch (s_game.mode)
        {
        case BagMode::List:
            if (dir == MoveUp)
                MoveCursor(-1, log);
            else if (dir == MoveDown)
                MoveCursor(1, log);
            else if (dir == MoveLeft || (pressed & Key::L))
                SwitchTab(-1, log);
            else if (dir == MoveRight || (pressed & Key::R))
                SwitchTab(1, log);
            else if ((pressed & Key::A) && s_cursor < (int)s_game.items.size())
            {
                log.push_back(Utils::Format("bag: open %d %s", s_cursor, NameOr(s_game.items[s_cursor].id).c_str()));
                s_driver.Open(s_cursor, s_game.items[s_cursor].id);
            }
            else if (pressed & Key::B)
            {
                log.push_back("bag: close");
                s_driver.Close();
            }
            break;

        case BagMode::Menu:
        {
            const int n = (int)s_game.commands.size();
            if (n > 0 && (dir == MoveUp || dir == MoveDown))
            {
                s_menuCursor = (s_menuCursor + (dir == MoveUp ? n - 1 : 1)) % n;
                log.push_back(std::string("bag: menu cursor ") + BagCommandLabel(s_game.commands[s_menuCursor]));
            }
            else if (pressed & Key::A)
            {
                if (s_game.YesNo())
                {
                    log.push_back(std::string("bag: answer ") + (s_menuCursor == 0 ? "yes" : "no"));
                    s_driver.Answer(s_menuCursor == 0);
                }
                else
                    ChooseEntry(sceneManager, log);
            }
            else if (pressed & Key::B)
            {
                if (s_game.YesNo())
                {
                    log.push_back("bag: answer no");
                    s_driver.Answer(false);
                }
                else
                {
                    log.push_back("bag: back to the list");
                    s_flow.active = false;
                    s_driver.Cancel();
                }
            }
            break;
        }

        case BagMode::Pick:
        {
            const int n = s_party.empty() ? 6 : (int)s_party.size();
            if (dir == MoveUp || dir == MoveDown)
            {
                s_pickCursor = (s_pickCursor + (dir == MoveUp ? n - 1 : 1)) % n;
                log.push_back(Utils::Format("bag: pick cursor %d %s", s_pickCursor + 1,
                                            s_pickCursor < (int)s_party.size() ? s_party[s_pickCursor].name.c_str() : "?"));
            }
            else if (pressed & Key::A)
            {
                const std::string mon = s_pickCursor < (int)s_party.size() ? s_party[s_pickCursor].name : "?";
                log.push_back(Utils::Format("bag: pick slot %d %s", s_pickCursor + 1, mon.c_str()));
                s_flow.slot = s_pickCursor;
                s_driver.Pick(s_pickCursor);
            }
            else if (pressed & Key::B)
            {
                log.push_back("bag: back to the list");
                s_flow.active = false;
                s_driver.Cancel();
            }
            break;
        }

        default:
            break;
        }
    }

    // Follow what the game shows: new pocket, menu, pick, message.
    static void FollowGame(u32 sceneManager, std::vector<std::string> &log)
    {
        const BagGame &g = s_game;
        if (g.tab != s_prevTab && g.tab >= 0 && g.mode == BagMode::List && g.scroll >= 0)
        {
            s_prevTab = g.tab;
            s_cursor = g.Index() >= 0 && g.Index() < (int)g.items.size() ? g.Index() : 0;
            s_top = BagListTop(0, s_cursor, (int)g.items.size());
            log.push_back(Utils::Format("bag: pocket %s, %u items, game cursor %d", BagTabAt(g.tab).name,
                                        (unsigned)g.items.size(), g.Index()));
        }
        // The list can shrink under the cursor (an item used up).
        if (s_cursor >= (int)g.items.size())
            s_cursor = g.items.empty() ? 0 : (int)g.items.size() - 1;
        s_top = BagListTop(s_top, s_cursor, (int)g.items.size());

        if (g.mode == BagMode::Menu)
        {
            u32 sig = 0x811C9DC5u;
            for (u8 c : g.commands)
                sig = (sig ^ c) * 16777619u;
            sig ^= g.item;
            if (sig != s_menuSignature)
            {
                s_menuSignature = sig;
                s_menuCursor = 0;
                std::string entries;
                for (u8 c : g.commands)
                    entries += std::string(entries.empty() ? "" : ", ") + BagCommandLabel(c);
                log.push_back(Utils::Format("bag: %s %s: %s", g.YesNo() ? "question" : "menu",
                                            NameOr(g.item).c_str(), entries.c_str()));
            }
        }
        else
            s_menuSignature = 0;

        if (g.mode != s_prevMode)
        {
            static const char *const modes[] = { "none", "list", "menu", "pick", "message", "busy" };
            if (g.mode != BagMode::Busy && g.mode != BagMode::Menu)
                log.push_back(std::string("bag: game ") + modes[(int)g.mode]);
            if (g.mode == BagMode::Pick)
            {
                // Back at the pick after a Pokémon was chosen: that one is
                // done, the player may choose another.
                if (s_flow.active && s_flow.slot >= 0)
                {
                    const Flow again = s_flow;
                    FinishFlow(sceneManager, log);
                    s_flow = again;
                    s_flow.slot = -1;
                    s_flow.countBefore = CountOf(g, g.item);
                    s_flow.before = s_partyState;
                }
                ReadPartyNow(sceneManager);
                s_pickCursor = g.partyCursor >= 0 && g.partyCursor < (int)s_party.size() ? g.partyCursor : 0;
            }
            if (g.mode == BagMode::List)
                ReadPartyNow(sceneManager);
            s_prevMode = g.mode;
        }

        // Messages: A through them (the result is read from the game after).
        if (!s_driver.Busy() && g.mode == BagMode::Message)
            s_driver.Advance();
        if (!s_driver.Busy() && g.mode == BagMode::List && s_flow.active)
            FinishFlow(sceneManager, log);
    }

    static void RunDriver(u32 sceneManager, std::vector<std::string> &log)
    {
        const bool wasBusy = s_driver.Busy();
        const BagCmd cmd = s_driver.Update(s_game, ActionRunning(), log);
        if (cmd.step != BagStep::None)
        {
            const ActionDef *def = BagAction(cmd);
            if (def == nullptr)
                s_driver.Refused("no action for that step", log);
            else if (!ActionStart(*def, sceneManager, log))
                s_driver.Refused(std::string(def->name) + " refused", log);
        }
        if (wasBusy && !s_driver.Busy())
        {
            if (!s_driver.Failure().empty())
            {
                SetStatus("Didn't work (" + s_driver.Failure() + "): use the bottom screen (L+R)");
                s_flow.active = false;
            }
            s_wantTab = -1;
        }
    }

    void BagExpectOpen(void)
    {
        s_expectUntil = s_frames + ExpectFrames;
    }

    void BagUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        s_frames++;
        // The Party screen's Use item drives the Bag (Restore) itself.
        const bool inBag = scene == SceneBag && !PartyScreenUsingBag();
        s_game = inBag ? ReadBagGame(sceneManager) : BagGame();
        const bool expecting = s_frames < s_expectUntil && !inBag;
        if (inBag)
            s_expectUntil = 0;
        if (!inBag && !expecting)
            s_yield = false;

        // A flow can take the game to another screen (a TM's move-forget
        // screen, an evolution) and back into the Bag; keep it meanwhile.
        // Back in the field, the Bag is closed and the flow is over.
        if (!inBag && !scene.empty() && s_flow.active)
        {
            if (scene == SceneField || scene == SceneBag)
            {
                log.push_back("bag: the Bag closed during " + NameOr(s_flow.item));
                s_flow.active = false;
                s_handedTo.clear();
            }
            else if (s_handedTo.empty())
            {
                s_handedTo = scene;
                log.push_back("bag: handed over to " + scene + " (" + NameOr(s_flow.item) + ")");
            }
        }
        if (!inBag)
            s_aside = false;

        // A screen inside the Bag that the plugin doesn't read: step aside.
        s_busyFrames = inBag && s_game.mode == BagMode::Busy ? s_busyFrames + 1 : 0;
        const bool aside = s_enabled && !s_yield && inBag && s_game.Valid()
                           && BagFollowGame(s_game.mode, s_busyFrames, s_flow.active, s_aside) == BagFollow::Aside;
        if (aside != s_aside)
        {
            s_aside = aside;
            if (aside)
            {
                s_driver.Stop();
                SetPanelMode(PanelMode::Panel);
                log.push_back("bag: the game shows a screen the native Bag doesn't read (" + NameOr(s_flow.item)
                              + "), the panel takes over -> " + PanelStatus());
            }
            else
                log.push_back(std::string("bag: back from the bottom screen, game ") + (s_game.mode == BagMode::Pick ? "pick" : "list"));
        }

        // The panel: off while the native Bag is (about to be) up. A panel
        // turned on while we hold it off is the player's doing (L+R): step
        // aside. Not on a scene change: AutoPanel then clears its override
        // and picks the scene's mode itself (the panel, for the Bag).
        const bool sceneChanged = scene != s_prevScene;
        s_prevScene = scene;
        bool holdOff = s_enabled && !s_yield && !s_aside && ((inBag && s_game.Valid()) || expecting);
        if (holdOff && s_forcedOff && !sceneChanged && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            holdOff = false;
            s_driver.Stop();
            log.push_back("bag: panel turned on by hand, native Bag hidden until the Bag closes");
        }
        if (holdOff)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("bag: native Bag up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }

        const bool shown = s_enabled && !s_yield && !s_aside && inBag && s_game.Valid();
        if (shown && !s_shown)
        {
            UI::RequestAssets();    // the game's font and cursor (usually loaded at the first field already)
            s_prevTab = -1;
            s_prevMode = BagMode::None;
            s_wantTab = -1;
            // Back from another screen or the panel: the flow goes on.
            if (!s_flow.active)
                s_statusUntil = 0;
            else if (!s_handedTo.empty())
                log.push_back("bag: back from " + s_handedTo);
            s_handedTo.clear();
            s_repeatKey = 0;
            ReadPartyNow(sceneManager);
            log.push_back(Utils::Format("bag: shown (pocket %s, %u items)", s_game.tab >= 0 ? BagTabAt(s_game.tab).name : "?",
                                        (unsigned)s_game.items.size()));
            log.push_back(UI::ScreenBuffer::HeapLog("bag shown"));
        }
        else if (!shown && s_shown)
        {
            s_driver.Stop();
            log.push_back("bag: hidden");
            log.push_back(UI::ScreenBuffer::HeapLog("bag hidden"));
        }
        s_shown = shown;
        AssetsUpdate(shown, log);

        if (shown)
        {
            if (s_frames - s_partyAt >= PartyEvery && !s_driver.Busy())
                ReadPartyNow(sceneManager);
            FollowGame(sceneManager, log);
            RunDriver(sceneManager, log);
        }

        // Input: ours while the Bag shows; the action layer captures
        // everything while it runs and gives ours back afterwards.
        if (shown && !ActionRunning())
        {
            if (!s_capturing)
            {
                InputCaptureBegin(CaptureKeys, true, false);
                s_capturing = true;
            }
            HandleInput(sceneManager, log);
        }
        else if (s_capturing && !shown && !ActionRunning())
        {
            InputCaptureEnd();
            s_capturing = false;
        }

        if (shown)
            Publish();
    }

    bool BagCommandLine(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(BagStatus());
        }
        else if (args == "status")
            log.push_back(BagStatus());
        else if (args == "read")
        {
            const BagGame g = ReadBagGame(sceneManager);
            static const char *const modes[] = { "none", "list", "menu", "pick", "message", "busy" };
            std::string out = Utils::Format("bagui: mode %s tab %d items %u visible %d row %d scroll %d index %d item %u party %d commands",
                                            modes[(int)g.mode], g.tab, (unsigned)g.items.size(), g.visible, g.cursorRow,
                                            g.scroll, g.Index(), g.item, g.partyCursor);
            for (u8 c : g.commands)
                out += Utils::Format(" %02X", c);
            for (size_t i = 0; i < g.items.size() && i < 12; i++)
                out += Utils::Format("%s%s x%u", i == 0 ? "\n  " : ", ", NameOr(g.items[i].id).c_str(), g.items[i].count);
            log.push_back(out);
        }
        else if (args.compare(0, 7, "select ") == 0)
        {
            const int index = std::atoi(args.c_str() + 7);
            if (!s_shown || s_driver.Busy() || s_game.mode != BagMode::List)
                log.push_back("bagui: select refused: the native Bag isn't waiting in its list");
            else if (index < 0 || index >= (int)s_game.items.size())
                log.push_back(Utils::Format("bagui: no item %d (%u in this pocket)", index, (unsigned)s_game.items.size()));
            else
            {
                s_cursor = index;
                s_top = BagListTop(s_top, s_cursor, (int)s_game.items.size());
                log.push_back(Utils::Format("bag: cursor %d %s", s_cursor, NameOr(s_game.items[s_cursor].id).c_str()));
                Publish();
            }
        }
        else
            return false;
        return true;
    }

    std::string BagStatus(void)
    {
        static const char *const modes[] = { "none", "list", "menu", "pick", "message", "busy" };
        const BagAssets *assets = Assets();
        return Utils::Format("bagui: %s, %s, game %s, pocket %s, cursor %d, driver %s%s%s%s%s%s%s, icons %d, descriptions read %u, screen buffer %s, ",
                             s_enabled ? "on" : "off", s_shown ? "shown" : "hidden", modes[(int)s_game.mode],
                             s_game.tab >= 0 ? BagTabAt(s_game.tab).name : "-", s_cursor, s_driver.Status().c_str(),
                             s_yield ? ", panel on by hand" : "", s_aside ? ", stepped aside" : "",
                             s_handedTo.empty() ? "" : (", handed over to " + s_handedTo).c_str(),
                             s_flow.active ? (std::string(", ") + FlowName(s_flow.command) + " " + NameOr(s_flow.item)).c_str() : "",
                             s_forcedOff ? ", holding panel off" : "", s_capturing ? ", input captured" : "",
                             assets != nullptr ? assets->PocketCount() : 0,
                             (unsigned)s_descriptions.size(),
                             UI::ScreenBuffer::Holder() != nullptr ? UI::ScreenBuffer::Holder() : "free")
               + UI::ScreenBuffer::HeapStatus();
    }
}
