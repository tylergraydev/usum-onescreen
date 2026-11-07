#include "Screens/Boxes.hpp"

#include <cstring>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Box.hpp"
#include "Buttons.hpp"
#include "Panel.hpp"
#include "Screens/BoxesGrid.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/PokeIcons.hpp"
#include "UI/ScreenBuffer.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    static_assert(BoxesKeyA == (u32)Key::A && BoxesKeyL == (u32)Key::L && BoxesKeyR == (u32)Key::R
                  && BoxesKeyX == (u32)Key::X && BoxesKeyY == (u32)Key::Y, "box keys");

    // Keys the native screen takes from the game while it waits for input.
    // The D-pad and B stay with the game: its cursor is the one shown, and B
    // puts a held Pokémon back or leaves the PC. X is taken only so the game
    // doesn't switch to another mode under the native screen.
    static const u32 CaptureKeys = Key::A | Key::L | Key::R | Key::X | Key::Y;
    // Slots are read again this often, and whenever the box or mode changes.
    static const u32 RefreshFrames = 30;
    // After the Box scene starts, hold the panel off this long while its
    // screen isn't readable yet, so it doesn't flash on.
    static const u32 EnterFrames = 90;
    // The listener reads as none of the known ones for a few frames between
    // modes (e.g. ~8 after a drop); only hide after this many.
    static const u32 UnknownFrames = 20;

    // --- Shared with the OSD callback (game thread) ---
    static Mutex          s_lock;
    static BoxesView      s_shared;             // under s_lock
    static volatile u32   s_sharedVersion = 0;
    static volatile bool  s_shown = false;
    static volatile bool  s_details = false;
    static const char *const Owner = "boxes";   // UI::ScreenBuffer
    // The badge (details page) lives at the buffer's tail, the box window at
    // its head; the two never overlap.
    static const size_t   BadgeBytes = BoxesBadgeArea.w * BoxesBadgeArea.h * 4;
    static const size_t   PixelBytes = UI::ScreenBuffer::Bytes - BadgeBytes;

    // --- Plugin thread ---
    static bool s_enabled = true;
    static bool s_forcedOff = false;    // we hold the panel off (AutoPanel manual override)
    static bool s_yield = false;        // the player turned the panel on: hidden until the PC closes
    static bool s_capturing = false;
    static bool s_ourAction = false;    // an action of ours is running
    static u32  s_frames = 0, s_enteredAt = 0, s_refreshedAt = 0;
    static u32  s_unknown = 0;          // frames in a row the mode was unknown
    static std::string s_prevScene;
    static BoxView s_game;              // latest read
    static BoxesView s_view;            // what we show
    static bool s_dirty = false;
    static bool s_haveView = false;
    static bool s_gameFont = false;     // the game font was loaded at the last publish
    static u32  s_lrKey = 0;            // L or R pressed, acted on when released
    static bool s_lrSpoiled = false;    // ... unless the other one joined in (L+R: panel)

    // --- Box icons (docs/BOXES.md, "Icons") ---
    // Loaded on the plugin thread a few per frame while the screen shows,
    // freed when it hides. The OSD callback draws from the cache under
    // s_iconLock; the plugin thread takes it only to add an icon or clear.
    static const u32 IconsPerFrame = 3;
    // A redraw costs ~50 ms of the game thread (emulated): while icons come
    // in, redraw when a batch is complete or at most this often.
    static const u32 IconRedrawFrames = 8;
    static const u32 WantedIcons = BoxSlots + BoxPartySlots + 1;    // box, party, the held one
    static Mutex               s_iconLock;
    static UI::PokeIconCache   s_icons;
    static UI::PokeIconArchive s_iconArchive;
    static UI::PokeIconTable   s_iconTable;
    static Handle              s_romfs = 0;
    static bool                s_iconsOpen = false;
    static bool                s_iconsSettled = true;   // every wanted icon is in (or failed), logged
    static u32                 s_iconsStart = 0;        // frame the current batch started
    static u32                 s_iconsDecoded = 0;
    static u32                 s_iconsShownAt = 0;      // frame of the last redraw for new icons
    static bool                s_iconsPending = false;  // decoded, not drawn yet
    static u64                 s_iconTicks = 0, s_iconTicksMax = 0;  // decode time: total, worst frame
    static volatile u32        s_drawTicksMax = 0;      // OSD: slowest redraw since the last log
    static volatile u32        s_drawTicksLast = 0;     // OSD: the last redraw
    static bool                s_iconsEnabled = true;   // `boxes icons on|off`

    // --- OSD callback ---

    static UI::Layer s_layer, s_badge;
    static u32 s_drawnVersion = ~0u;
    static bool s_badgeDrawn = false;
    static BoxesView s_drawView;

    static bool BoxesOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
            return false;
        if (!s_shown)
        {
            UI::ScreenBuffer::GiveBack(Owner);
            return false;
        }
        bool fresh = false;
        u8 *pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
        if (pixels == nullptr)
            return false;
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        if (fresh)
        {
            s_badge = UI::Layer();
            s_layer = UI::Layer();
        }
        if (s_details)
        {
            if (!s_badge.Matches(fb))
            {
                if (!s_badge.Setup(fb, BoxesBadgeArea, 8, pixels + PixelBytes, BadgeBytes))
                    return false;
                s_badgeDrawn = false;
            }
            if (!s_badgeDrawn)
            {
                UI::Canvas canvas = s_badge.Begin();
                DrawBoxesBadge(canvas);
                s_badgeDrawn = true;
            }
            s_badge.Present(fb);
            return true;
        }

        bool redraw = s_sharedVersion != s_drawnVersion;
        if (!s_layer.Matches(fb))
        {
            if (!s_layer.Setup(fb, BoxesArea, BoxesRadius, pixels, PixelBytes))
                return false;
            redraw = true;
        }
        if (redraw)
        {
            s_lock.Lock();
            s_drawView = s_shared;
            s_drawnVersion = s_sharedVersion;
            s_lock.Unlock();
            UI::Canvas canvas = s_layer.Begin();
            const u64 start = svcGetSystemTick();
            s_iconLock.Lock();
            DrawBoxes(canvas, s_drawView, &s_icons);
            s_iconLock.Unlock();
            const u32 ticks = (u32)(svcGetSystemTick() - start);
            if (ticks > s_drawTicksMax)
                s_drawTicksMax = ticks;
            s_drawTicksLast = ticks;
        }
        s_layer.Present(fb);
        return true;
    }

    void StartBoxes(void)
    {
        OSD::Run(BoxesOnScreen);
    }

    // --- Plugin thread ---

    static void Publish(void)
    {
        if (!s_dirty)
            return;
        s_dirty = false;
        s_lock.Lock();
        s_shared = s_view;
        s_sharedVersion = s_sharedVersion + 1;
        s_lock.Unlock();
    }

    static const std::string &LabelAt(u32 cursor)
    {
        static const std::string none;
        if (cursor < BoxSlots)
            return s_view.slots[cursor];
        if (cursor < BoxSlots + BoxPartySlots)
            return s_view.party[cursor - BoxSlots];
        return none;
    }

    static u16 IconAt(u32 cursor)
    {
        if (cursor < BoxSlots)
            return s_view.slotIcons[cursor];
        if (cursor < BoxSlots + BoxPartySlots)
            return s_view.partyIcons[cursor - BoxSlots];
        return 0;
    }

    static u8 BallAt(u32 cursor)
    {
        if (cursor < BoxSlots)
            return s_view.slotBalls[cursor];
        if (cursor < BoxSlots + BoxPartySlots)
            return s_view.partyBalls[cursor - BoxSlots];
        return 0;
    }

    static bool ReadGame(u32 address, void *dst, u32 size)
    {
        if (size == 0 || !Process::CheckAddress(address, MEMPERM_READ)
            || !Process::CheckAddress(address + size - 1, MEMPERM_READ))
            return false;
        std::memcpy(dst, (const void *)address, size);
        return true;
    }

    static double Ms(u64 ticks)
    {
        return (double)ticks / CPU_TICKS_PER_MSEC;
    }

    // The game's icon table, the RomFS and the icon archive, once per show.
    static void OpenIcons(std::vector<std::string> &log)
    {
        if (!s_iconsEnabled || s_iconsOpen)
            return;
        const u64 start = svcGetSystemTick();
        std::string error;
        if (!UI::FindPokeIconTable(ReadGame, s_iconTable, error))
        {
            log.push_back("boxes: icons not loaded (" + error + "), names only");
            return;
        }
        static const u32 lowPath[3] = { 0, 0, 0 };
        const FS_Path archive = { PATH_EMPTY, 1, "" };
        const FS_Path file = { PATH_BINARY, sizeof(lowPath), lowPath };
        const Result res = FSUSER_OpenFileDirectly(&s_romfs, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
        if (R_FAILED(res))
        {
            s_romfs = 0;
            log.push_back(Utils::Format("boxes: icons not loaded (can't open the RomFS, 0x%08lX), names only", (u32)res));
            return;
        }
        const Handle handle = s_romfs;
        const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
            u32 got = 0;
            return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
        };
        if (!s_iconArchive.Open(read, error))
        {
            FSFILE_Close(s_romfs);
            s_romfs = 0;
            log.push_back("boxes: icons not loaded (a/0/6/2: " + error + "), names only");
            return;
        }
        s_iconsOpen = true;
        log.push_back(Utils::Format("boxes: icons: a/0/6/2, %lu entries, table 0x%08lX, egg %lu, opened in %.1f ms (%u KB)",
                                    s_iconArchive.Count(), s_iconTable.table, s_iconTable.egg,
                                    Ms(svcGetSystemTick() - start), (unsigned)(s_iconArchive.Bytes() / 1024)));
    }

    static void CloseIcons(void)
    {
        s_iconLock.Lock();
        s_icons.Clear();
        s_iconLock.Unlock();
        s_iconArchive.Close();
        if (s_romfs != 0)
            FSFILE_Close(s_romfs);
        s_romfs = 0;
        s_iconsOpen = false;
        s_iconsSettled = true;
        s_iconsPending = false;
    }

    static u16 IconFor(const Pk7 &mon)
    {
        if (!s_iconsOpen || mon.form == Pk7Form::Invalid || mon.species == 0)
            return 0;
        const u32 index = UI::PokeIconIndex(ReadGame, s_iconTable, mon.species, mon.altForm, mon.gender, mon.isEgg);
        return index < s_iconArchive.Count() && index <= 0xFFFF ? (u16)index : 0;
    }

    // A few of the icons on screen that aren't decoded yet; a redraw when
    // any came in, log lines once all of them are in.
    static void LoadIcons(std::vector<std::string> &log)
    {
        if (!s_iconsOpen)
            return;
        u32 wanted[WantedIcons];
        for (u32 i = 0; i < BoxSlots; i++)
            wanted[i] = s_view.slotIcons[i] != 0 ? s_view.slotIcons[i] : UI::PokeIconNone;
        for (u32 i = 0; i < BoxPartySlots; i++)
            wanted[BoxSlots + i] = s_view.partyIcons[i] != 0 ? s_view.partyIcons[i] : UI::PokeIconNone;
        wanted[WantedIcons - 1] = s_view.heldIcon != 0 ? s_view.heldIcon : UI::PokeIconNone;

        static UI::PokeIcon scratch;
        const u64 start = svcGetSystemTick();
        u32 done = 0;
        for (; done < IconsPerFrame; done++)
        {
            const u32 index = s_icons.Next(wanted, WantedIcons);
            if (index == UI::PokeIconNone)
                break;
            if (s_iconsSettled)
            {
                s_iconsSettled = false;
                s_iconsStart = s_frames;
                s_iconsDecoded = 0;
                s_iconTicks = s_iconTicksMax = 0;
            }
            std::string error;
            bool ok = s_iconArchive.Load(index, scratch, error);
            if (ok)
            {
                s_iconLock.Lock();
                ok = s_icons.Store(scratch);
                s_iconLock.Unlock();
                if (!ok)
                    error = "no room in the cache";
            }
            if (!ok)
            {
                s_icons.Fail(index);
                log.push_back(Utils::Format("boxes: icon %lu not loaded (%s)", index, error.c_str()));
            }
            else
                s_iconsDecoded++;
        }
        if (done == 0)
        {
            if (s_iconsPending)
            {
                s_iconsPending = false;
                s_iconsShownAt = s_frames;
                s_dirty = true;
            }
            if (!s_iconsSettled)
            {
                s_iconsSettled = true;
                log.push_back(Utils::Format("boxes: icons: %lu decoded over %lu frames, %.1f ms (worst frame %.1f ms), "
                                            "cache %lu (%u KB), slowest redraw %.1f ms",
                                            s_iconsDecoded, s_frames - s_iconsStart, Ms(s_iconTicks),
                                            Ms(s_iconTicksMax), s_icons.Size(), (unsigned)(s_icons.Bytes() / 1024),
                                            Ms(s_drawTicksMax)));
                log.push_back(UI::ScreenBuffer::HeapLog("boxes icons in"));
                s_drawTicksMax = 0;
            }
            return;
        }
        const u64 ticks = svcGetSystemTick() - start;
        s_iconTicks += ticks;
        if (ticks > s_iconTicksMax)
            s_iconTicksMax = ticks;
        s_iconsPending = true;
        if (s_frames - s_iconsShownAt >= IconRedrawFrames)
        {
            s_iconsPending = false;
            s_iconsShownAt = s_frames;
            s_dirty = true;
        }
    }

    // Reads the box on screen and the screen's party.
    static void Refresh(void)
    {
        const BoxStorage storage = ReadBoxStorage();
        s_view.tray = s_game.tray;
        s_view.unlocked = storage.valid ? storage.unlocked : 1;
        s_view.boxName = BoxName(storage, s_game.tray);
        for (u32 i = 0; i < BoxSlots; i++)
        {
            const Pk7 mon = ReadBoxMon(storage, s_game.tray, i);
            s_view.slots[i] = BoxMonLabel(mon);
            s_view.slotBalls[i] = mon.ball;
            s_view.slotShiny[i] = mon.shiny && !mon.isEgg && mon.form != Pk7Form::Invalid;
            s_view.slotIcons[i] = IconFor(mon);
        }
        for (u32 i = 0; i < BoxPartySlots; i++)
        {
            const Pk7 mon = s_game.partyValid ? ReadParam(s_game.party[i]) : Pk7();
            s_view.party[i] = s_game.partyValid ? BoxMonLabel(mon) : "";
            s_view.partyBalls[i] = mon.ball;
            s_view.partyShiny[i] = mon.shiny && !mon.isEgg && mon.form != Pk7Form::Invalid;
            s_view.partyIcons[i] = s_game.partyValid ? IconFor(mon) : 0;
        }
        s_refreshedAt = s_frames;
        s_dirty = true;
    }

    static std::string Where(u32 cursor)
    {
        const std::string &label = LabelAt(cursor);
        switch (BoxCursorKindOf(cursor))
        {
        case BoxCursorKind::Slot:    return Utils::Format("slot %lu (%s)", cursor, label.empty() ? "empty" : label.c_str());
        case BoxCursorKind::Party:   return Utils::Format("party %lu (%s)", cursor - BoxSlots + 1, label.empty() ? "empty" : label.c_str());
        case BoxCursorKind::Name:    return "box name";
        case BoxCursorKind::BoxList: return "box list";
        default:                     return Utils::Format("%lu", cursor);
        }
    }

    static void Run(BoxesCommand command, u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 cursor = s_game.cursor;
        const ActionDef *def = nullptr;
        std::string busy;
        switch (command)
        {
        case BoxesCommand::Details:
            s_details = !s_details;
            log.push_back(std::string("boxes: details ") + (s_details ? "on" : "off"));
            return;
        case BoxesCommand::Pick:
            def = &BoxesPickAction(cursor);
            busy = "Picking up " + LabelAt(cursor) + "...";
            log.push_back("boxes: pick " + Where(cursor));
            break;
        case BoxesCommand::Drop:
            def = &BoxesDropAction(cursor);
            busy = "Putting " + (s_view.held.empty() ? std::string("it") : s_view.held) + " down...";
            log.push_back("boxes: drop at " + Where(cursor));
            break;
        case BoxesCommand::PrevBox:
        case BoxesCommand::NextBox:
            def = &BoxesTurnAction(command == BoxesCommand::NextBox);
            busy = "Turning the box...";
            log.push_back(std::string("boxes: ") + (command == BoxesCommand::NextBox ? "next" : "previous") + " box");
            break;
        default:
            log.push_back("boxes: nothing to do at " + Where(cursor) + " (" + BoxModeName(s_game.mode) + ")");
            return;
        }
        if (!ActionStart(*def, sceneManager, log))
            return;
        if (command == BoxesCommand::Pick)
        {
            s_view.held = LabelAt(cursor);
            s_view.heldBall = BallAt(cursor);
            s_view.heldIcon = IconAt(cursor);
            s_view.heldTray = (int)s_game.tray;
            s_view.heldIndex = (int)cursor;
        }
        s_ourAction = true;
        s_view.busy = busy;
        s_dirty = true;
    }

    static void HandleInput(u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 held = CaptureRealHeld();
        const u32 pressed = CaptureRealPressed();
        const u32 cursor = s_game.cursor;
        const bool occupied = !LabelAt(cursor).empty();

        // L / R act when released, so L+R (the panel key) doesn't turn the box.
        const u32 lr = Key::L | Key::R;
        if (s_lrKey == 0 && (pressed & lr) != 0)
        {
            s_lrKey = (pressed & Key::L) ? (u32)Key::L : (u32)Key::R;
            s_lrSpoiled = (held & lr) == lr;
        }
        else if (s_lrKey != 0)
        {
            if ((held & lr) == lr)
                s_lrSpoiled = true;
            if (!(held & s_lrKey))
            {
                if (!s_lrSpoiled)
                    Run(BoxesCommandFor(s_lrKey, s_game.mode, cursor, occupied), sceneManager, log);
                s_lrKey = 0;
                return;
            }
        }

        if (pressed & Key::Y)
            Run(BoxesCommand::Details, sceneManager, log);
        else if (pressed & Key::A)
            Run(BoxesCommandFor(BoxesKeyA, s_game.mode, cursor, occupied), sceneManager, log);
    }

    void BoxesUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        s_frames++;
        const bool inBox = scene == SceneBox;
        if (inBox && s_prevScene != SceneBox)
            s_enteredAt = s_frames;
        s_prevScene = scene;
        if (!inBox)
        {
            s_yield = false;
            s_details = false;
        }

        const BoxView before = s_game;
        s_game = inBox ? ReadBoxView(sceneManager) : BoxView();
        s_unknown = s_game.valid && s_game.mode == BoxMode::Unknown ? s_unknown + 1 : 0;
        const bool known = s_game.valid && (s_game.mode != BoxMode::Unknown || (s_shown && s_unknown < UnknownFrames));

        // The panel: off while the native screen is (about to be) up. A panel
        // turned on while we hold it off is the player's doing: step aside.
        bool holdOff = s_enabled && !s_yield && inBox && (known || s_frames - s_enteredAt < EnterFrames);
        if (holdOff && s_forcedOff && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            holdOff = false;
            log.push_back("boxes: panel turned on by hand, native boxes hidden until the PC closes");
        }
        if (holdOff)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("boxes: native boxes up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }

        if (s_ourAction && !ActionRunning())
        {
            s_ourAction = false;
            s_view.busy.clear();
            s_dirty = true;
        }

        const bool shown = s_enabled && !s_yield && known;
        if (shown && !s_shown)
        {
            s_lrKey = 0;
            s_haveView = false;
            // The game's font and icons (normally loaded at the first field
            // already); the screen redraws with them once they are in.
            UI::RequestAssets();
            OpenIcons(log);
        }
        if (shown)
        {
            if (!s_haveView || s_game.tray != s_view.tray || s_game.mode != before.mode
                || s_frames - s_refreshedAt >= RefreshFrames)
            {
                if (s_haveView && s_game.tray != s_view.tray)
                    log.push_back(Utils::Format("boxes: box %lu", s_game.tray + 1));
                Refresh();
            }
            if (!s_shown)
            {
                log.push_back(Utils::Format("boxes: shown (box %lu \"%s\", %s, cursor %s)", s_game.tray + 1,
                                            s_view.boxName.c_str(), BoxModeName(s_game.mode),
                                            Where(s_game.cursor).c_str()));
                log.push_back(UI::ScreenBuffer::HeapLog("boxes shown"));
            }
            else
            {
                if (s_game.mode != before.mode)
                    log.push_back(std::string("boxes: mode ") + BoxModeName(s_game.mode));
                if (s_game.cursor != before.cursor)
                    log.push_back("boxes: cursor " + Where(s_game.cursor));
            }
            s_haveView = true;
            LoadIcons(log);
        }
        else if (s_shown)
        {
            CloseIcons();
            log.push_back(std::string("boxes: hidden") + (s_yield ? " (panel on by hand)" : ""));
            log.push_back(UI::ScreenBuffer::HeapLog("boxes hidden"));
        }

        s_shown = shown;

        // Held: set when our pick starts; gone once nothing is held (dropped,
        // put back with B, or the pick didn't take).
        if (s_game.mode != BoxMode::Carrying && !s_ourAction && s_view.heldIndex >= 0)
        {
            s_view.held.clear();
            s_view.heldBall = 0;
            s_view.heldIcon = 0;
            s_view.heldTray = s_view.heldIndex = -1;
        }
        if (s_view.cursor != s_game.cursor || s_view.mode != s_game.mode)
        {
            s_view.cursor = s_game.cursor;
            s_view.mode = s_game.mode;
            s_dirty = true;
        }

        // Input: ours while the game waits on its box view (not while its
        // slot menu or a message is up: those take A and B). The action
        // layer captures everything while it runs and gives our capture back
        // afterwards, so ours only ends once it is done.
        const bool owning = shown && (s_game.mode == BoxMode::Idle || s_game.mode == BoxMode::Carrying);
        if (owning && !ActionRunning())
        {
            if (!s_capturing)
            {
                InputCaptureBegin(CaptureKeys, false, false);
                s_capturing = true;
            }
            HandleInput(sceneManager, log);
        }
        else if (s_capturing && !owning && !ActionRunning())
        {
            InputCaptureEnd();
            s_capturing = false;
            s_lrKey = 0;
        }

        // Redraw once the game's font and icons have loaded (UI::Theme draws
        // with them from then on).
        if (UI::Theme::UsingGameFont() != s_gameFont)
        {
            s_gameFont = !s_gameFont;
            s_dirty = true;
        }
        Publish();
    }

    bool BoxesRunCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        BoxesCommand command = BoxesCommand::None;
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(BoxesStatus());
            return true;
        }
        if (args == "icons on" || args == "icons off")
        {
            s_iconsEnabled = args == "icons on";
            if (!s_iconsEnabled)
                CloseIcons();
            else if (s_shown)
                OpenIcons(log);
            if (s_shown)
                Refresh();
            Publish();
            log.push_back(BoxesStatus());
            return true;
        }
        if (args == "status")
        {
            log.push_back(BoxesStatus());
            return true;
        }
        if (args == "details")
            command = BoxesCommand::Details;
        else if (args == "pick" || args == "drop")
        {
            const u32 cursor = s_game.cursor;
            command = BoxesCommandFor(BoxesKeyA, s_game.mode, cursor, !LabelAt(cursor).empty());
            if (command != (args == "pick" ? BoxesCommand::Pick : BoxesCommand::Drop))
                command = BoxesCommand::None;
        }
        else if (args == "prev")
            command = BoxesCommandFor(BoxesKeyL, s_game.mode, s_game.cursor, false);
        else if (args == "next")
            command = BoxesCommandFor(BoxesKeyR, s_game.mode, s_game.cursor, false);
        else
            return false;

        if (!s_shown || ActionRunning())
            log.push_back("boxes: " + args + " refused: the native boxes aren't waiting for input");
        else
        {
            Run(command, sceneManager, log);
            Publish();
        }
        return true;
    }

    std::string BoxesStatus(void)
    {
        return Utils::Format("boxes: %s, %s%s, box %lu, mode %s, cursor %s%s%s%s%s, icons %s (%lu cached, %u KB), "
                             "last redraw %.1f ms", s_enabled ? "on" : "off",
                             s_shown ? "shown" : "hidden", s_details ? " (details)" : "", s_game.tray + 1,
                             BoxModeName(s_game.mode), Where(s_game.cursor).c_str(),
                             s_view.held.empty() ? "" : (", holding " + s_view.held).c_str(),
                             s_yield ? ", panel on by hand" : "", s_forcedOff ? ", holding panel off" : "",
                             s_capturing ? ", input captured" : "", !s_iconsEnabled ? "off" : s_iconsOpen ? "on" : "not loaded",
                             s_icons.Size(), (unsigned)(s_icons.Bytes() / 1024), Ms(s_drawTicksLast));
    }
}
