#include "Screens/Boxes.hpp"

#include <new>

#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Box.hpp"
#include "Buttons.hpp"
#include "Panel.hpp"
#include "Screens/BoxesGrid.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"

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
    static u8            *s_pixels = nullptr;   // allocated on first show
    static u8            *s_badgePixels = nullptr;
    static const size_t   PixelBytes = BoxesArea.w * BoxesArea.h * 4;
    static const size_t   BadgeBytes = BoxesBadgeArea.w * BoxesBadgeArea.h * 4;

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
    static u32  s_lrKey = 0;            // L or R pressed, acted on when released
    static bool s_lrSpoiled = false;    // ... unless the other one joined in (L+R: panel)

    // --- OSD callback ---

    static UI::Layer s_layer, s_badge;
    static u32 s_drawnVersion = ~0u;
    static bool s_badgeDrawn = false;
    static BoxesView s_drawView;

    static bool BoxesOnScreen(const Screen &screen)
    {
        if (!screen.IsTop || !s_shown)
            return false;
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        if (s_details)
        {
            if (s_badgePixels == nullptr)
                return false;
            if (!s_badge.Matches(fb))
            {
                if (!s_badge.Setup(fb, BoxesBadgeArea, 8, s_badgePixels, BadgeBytes))
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

        if (s_pixels == nullptr)
            return false;
        bool redraw = s_sharedVersion != s_drawnVersion;
        if (!s_layer.Matches(fb))
        {
            if (!s_layer.Setup(fb, BoxesArea, BoxesRadius, s_pixels, PixelBytes))
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
            DrawBoxes(canvas, s_drawView);
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

    // Reads the box on screen and the screen's party.
    static void Refresh(void)
    {
        const BoxStorage storage = ReadBoxStorage();
        s_view.tray = s_game.tray;
        s_view.unlocked = storage.valid ? storage.unlocked : 1;
        s_view.boxName = BoxName(storage, s_game.tray);
        for (u32 i = 0; i < BoxSlots; i++)
            s_view.slots[i] = BoxMonLabel(ReadBoxMon(storage, s_game.tray, i));
        for (u32 i = 0; i < BoxPartySlots; i++)
            s_view.party[i] = s_game.partyValid ? BoxMonLabel(ReadParam(s_game.party[i])) : "";
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
            if (s_pixels == nullptr)
                s_pixels = new (std::nothrow) u8[PixelBytes];
            if (s_badgePixels == nullptr)
                s_badgePixels = new (std::nothrow) u8[BadgeBytes];
            s_lrKey = 0;
            s_haveView = false;
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
                log.push_back(Utils::Format("boxes: shown (box %lu \"%s\", %s, cursor %s)%s", s_game.tray + 1,
                                            s_view.boxName.c_str(), BoxModeName(s_game.mode),
                                            Where(s_game.cursor).c_str(),
                                            s_pixels == nullptr ? ", NO MEMORY for the layer" : ""));
            else
            {
                if (s_game.mode != before.mode)
                    log.push_back(std::string("boxes: mode ") + BoxModeName(s_game.mode));
                if (s_game.cursor != before.cursor)
                    log.push_back("boxes: cursor " + Where(s_game.cursor));
            }
            s_haveView = true;
        }
        else if (s_shown)
            log.push_back(std::string("boxes: hidden") + (s_yield ? " (panel on by hand)" : ""));
        s_shown = shown;

        // Held: set when our pick starts; gone once nothing is held (dropped,
        // put back with B, or the pick didn't take).
        if (s_game.mode != BoxMode::Carrying && !s_ourAction && s_view.heldIndex >= 0)
        {
            s_view.held.clear();
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
        return Utils::Format("boxes: %s, %s%s, box %lu, mode %s, cursor %s%s%s%s%s", s_enabled ? "on" : "off",
                             s_shown ? "shown" : "hidden", s_details ? " (details)" : "", s_game.tray + 1,
                             BoxModeName(s_game.mode), Where(s_game.cursor).c_str(),
                             s_view.held.empty() ? "" : (", holding " + s_view.held).c_str(),
                             s_yield ? ", panel on by hand" : "", s_forcedOff ? ", holding panel off" : "",
                             s_capturing ? ", input captured" : "");
    }
}
