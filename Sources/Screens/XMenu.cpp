#include "Screens/XMenu.hpp"

#include <malloc.h>
#include <new>

#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Framebuffer.hpp"
#include "Panel.hpp"
#include "Scene.hpp"
#include "Screens/PartyScreen.hpp"
#include "Screens/Pokedex.hpp"
#include "Screens/Bag.hpp"
#include "Screens/Map.hpp"
#include "Screens/XMenuGrid.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Keys the native menu takes from the game while it shows. B, X and Y
    // stay with the game (close / close / save dialog), so the game's own
    // menu logic handles them exactly as without the plugin.
    static const u32 MoveUp    = Key::DPadUp | Key::CPadUp;
    static const u32 MoveDown  = Key::DPadDown | Key::CPadDown;
    static const u32 MoveLeft  = Key::DPadLeft | Key::CPadLeft;
    static const u32 MoveRight = Key::DPadRight | Key::CPadRight;
    static const u32 CaptureKeys = Key::A | Key::DPadUp | Key::DPadDown | Key::DPadLeft | Key::DPadRight | Key::L | Key::R | Key::Start;

    // Held direction: first repeat after RepeatDelay frames, then every RepeatEvery.
    static const u32 RepeatDelay = 15, RepeatEvery = 5;
    // Field state turned 4: hold the panel off this long while the X menu
    // slides in (the probe sees it ~12 frames after X; coming back from
    // Party it shows ~20 frames after FieldRo).
    static const u32 PendingFrames = 45;
    // After the menu shows, look for the game's cursor this long.
    static const u32 ScanFrames = 60;
    static const u32 ScanStable = 3;
    // The probe drops for a frame or two while the game slides its page in;
    // the menu only counts as gone after this many frames without it.
    static const u32 GoneFrames = 6;
    // The layer's buffer goes back to the heap this many frames after the
    // menu hides, once the OSD callback can't be drawing with it any more
    // (it checks s_shown first). The plugin heap is 5 MiB.
    static const u32 FreeAfter = 4;

    // --- Shared with the OSD callback (game thread) ---
    static volatile bool s_shown = false;
    static volatile int  s_viewCursor = 0;
    static volatile int  s_viewOpening = -1;
    static volatile u32  s_viewVersion = 0;     // bumped whenever the view changes
    static volatile bool s_scanWanted = false;
    static volatile int  s_scanPage = -1, s_scanSlot = -1;
    static volatile u32  s_scanCount = 0;
    static u8           *s_pixels = nullptr;    // the layer's buffer, allocated on first show
    static const size_t  PixelBytes = XMenuArea.w * XMenuArea.h * 4;

    // --- Plugin thread ---
    static bool s_enabled = true;
    static XMenuCursor s_cursor;
    static unsigned s_enabledMask = 0;
    static bool s_forcedOff = false;    // we hold the panel off (AutoPanel manual override)
    static bool s_yield = false;        // the player turned the panel on: stay hidden until the X menu closes
    static bool s_capturing = false;
    static int  s_opening = -1;         // entry whose action is running
    static bool s_mapAfter = false;     // Map: open the native map once the X menu has closed
    static bool s_synced = false;       // the game's cursor was read (or given up on) since the menu showed
    static u32  s_frames = 0;
    static u32  s_shownAt = 0;
    static u32  s_pendingUntil = 0;
    static int  s_prevFieldState = -1;
    static std::string s_prevScene;
    static u32  s_repeatKey = 0, s_repeatAt = 0;
    static u32  s_lastScan = 0;
    static int  s_stablePage = -1, s_stableSlot = -1;
    static u32  s_stableCount = 0;
    static u32  s_probeMissing = GoneFrames;    // frames in a row the X menu probe was off
    static u32  s_freeAt = 0;

    // --- OSD callback ---

    static UI::Layer s_layer;
    static u32 s_drawnVersion = ~0u;
    static u8 *s_layerBuffer = nullptr;     // the buffer s_layer was set up with
    static bool s_drawnGameFont = false;    // the game font was loaded when last drawn

    // The game's cursor is a red arrow left of the pill it points at
    // (docs/LAYOUTS.md); it bobs a few pixels, so scan two rows across the gutter.
    static bool ArrowRed(const FbLayout &fb, s32 x, s32 y)
    {
        u8 r, g, b;
        DecodePixel(fb.origin + x * fb.stepX + y * fb.stepY, fb.format, r, g, b);
        return r > 200 && g < 60 && b < 60;
    }

    static void ScanGameCursor(const Screen &screen)
    {
        const FbLayout fb = GetLayout(screen);
        int found = -1;
        for (int slot = 0; slot < 6 && found != -2; slot++)
        {
            const s32 x0 = slot % 2 == 0 ? 14 : 156, y0 = 50 + 60 * (slot / 2);
            int hits = 0;
            for (s32 y = y0; y <= y0 + 1; y++)
                for (s32 x = x0; x < x0 + 26; x++)
                    hits += ArrowRed(fb, x, y);
            if (hits >= 2)
                found = found == -1 ? slot : -2;
        }
        s_scanPage = (ActionProbeFlags() & WorldXMenuPage2) ? 1 : 0;
        s_scanSlot = found >= 0 ? found : -1;
        s_scanCount = s_scanCount + 1;
    }

    static bool XMenuOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
        {
            if (s_scanWanted && s_shown)
                ScanGameCursor(screen);
            return false;
        }
        u8 *const pixels = s_pixels;
        if (!s_shown || pixels == nullptr)
            return false;

        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        const u32 version = s_viewVersion;
        // Also once the game's font and icons have loaded (UI::Theme draws
        // with them from then on).
        const bool gameFont = UI::Theme::UsingGameFont();
        bool redraw = version != s_drawnVersion || gameFont != s_drawnGameFont;
        // The buffer is freed while the menu is hidden, so a new one needs
        // a new setup.
        if (!s_layer.Matches(fb) || s_layerBuffer != pixels)
        {
            s_layerBuffer = nullptr;
            if (!s_layer.Setup(fb, XMenuArea, XMenuRadius, pixels, PixelBytes))
                return false;
            s_layerBuffer = pixels;
            redraw = true;
        }
        if (redraw)
        {
            XMenuView view;
            view.cursor = s_viewCursor;
            view.opening = s_viewOpening;
            view.enabled = s_enabledMask;
            UI::Canvas canvas = s_layer.Begin();
            DrawXMenu(canvas, view);
            s_drawnVersion = version;
            s_drawnGameFont = gameFont;
        }
        s_layer.Present(fb);
        return true;
    }

    void StartXMenu(void)
    {
        for (int i = 0; i < (int)XMenuEntries().size(); i++)
            if (CheckAction(XMenuAction(i)).empty())
                s_enabledMask |= 1u << i;
        OSD::Run(XMenuOnScreen);
    }

    // --- Plugin thread ---

    static unsigned HeapUsedKb(void)
    {
        return (unsigned)(mallinfo().uordblks / 1024);
    }

    static const char *EntryName(int index)
    {
        const std::vector<XMenuEntry> &entries = XMenuEntries();
        return index >= 0 && index < (int)entries.size() ? entries[index].name : "?";
    }

    static void Publish(void)
    {
        if (s_viewCursor != s_cursor.Index() || s_viewOpening != s_opening)
        {
            s_viewCursor = s_cursor.Index();
            s_viewOpening = s_opening;
            s_viewVersion = s_viewVersion + 1;
        }
    }

    static void MoveTo(int index, std::vector<std::string> &log)
    {
        s_synced = true;            // the player moved first: don't jump to the game's cursor
        s_scanWanted = false;
        if (index == s_cursor.Index())
            return;
        s_cursor.Set(index);
        log.push_back(Utils::Format("xmenu: cursor %s", EntryName(index)));
    }

    // Entry `index` opens a screen the plugin shows natively (and it is on).
    static bool OpensNative(int index)
    {
        const char *native = XMenuNativeScreen(index);
        return native != nullptr && std::string(native) == "pokedex" && PokedexEnabled();
    }

    static void Open(int index, u32 sceneManager, std::vector<std::string> &log)
    {
        if (((s_enabledMask >> index) & 1u) == 0)
        {
            log.push_back(Utils::Format("xmenu: %s not available: %s", EntryName(index),
                                        CheckAction(XMenuAction(index)).c_str()));
            return;
        }
        s_cursor.Chosen(index);
        s_synced = true;
        s_scanWanted = false;
        const bool native = std::string(EntryName(index)) == "map" && MapEnabled();
        log.push_back(Utils::Format("xmenu: open %s%s", EntryName(index),
                                    native ? " (native map)" : OpensNative(index) ? " (native)" : ""));
        if (!ActionStart(native ? XMenuCloseAction() : XMenuAction(index), sceneManager, log))
            return;
        s_opening = index;
        s_mapAfter = native;
        // The Bag has a native screen: it takes over once the game's Bag is
        // up, and keeps the panel off until then.
        if (XMenuEntries()[index].opens == SceneBag)
            BagExpectOpen();
        // Pokémon opens the game's Party screen, which the native Party
        // screen takes over (Screens/PartyScreen.hpp): it holds the panel
        // off from here, so the panel doesn't flash on in between.
        if (index == XMenuFind("pokemon"))
            PartyScreenExpect();
    }

    static void HandleInput(u32 sceneManager, std::vector<std::string> &log)
    {
        const u32 held = CaptureRealHeld();
        const u32 pressed = CaptureRealPressed();

        // One direction at a time, with key repeat while it is held.
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

        const int from = s_cursor.Index();
        if (dir == MoveUp)         MoveTo(XMenuMove(from, 0, -1, ~0u), log);
        else if (dir == MoveDown)  MoveTo(XMenuMove(from, 0, 1, ~0u), log);
        else if (dir == MoveLeft)  MoveTo(XMenuMove(from, -1, 0, ~0u), log);
        else if (dir == MoveRight) MoveTo(XMenuMove(from, 1, 0, ~0u), log);

        if (pressed & Key::L)
            MoveTo(XMenuFlipPage(s_cursor.Index(), 0, ~0u), log);
        else if (pressed & Key::R)
            MoveTo(XMenuFlipPage(s_cursor.Index(), 1, ~0u), log);
        else if (pressed & Key::A)
            Open(s_cursor.Index(), sceneManager, log);
    }

    // While the menu shows, follow the game's cursor once it reads the same
    // ScanStable times in a row.
    static void SyncFromGame(std::vector<std::string> &log)
    {
        if (s_synced)
            return;
        if (s_frames - s_shownAt > ScanFrames)
        {
            s_synced = true;
            s_scanWanted = false;
            log.push_back("xmenu: game cursor not found, keeping " + std::string(EntryName(s_cursor.Index())));
            return;
        }
        if (s_scanCount == s_lastScan)
            return;
        s_lastScan = s_scanCount;
        const int page = s_scanPage, slot = s_scanSlot;
        if (slot < 0 || page != s_stablePage || slot != s_stableSlot)
        {
            s_stablePage = page;
            s_stableSlot = slot;
            s_stableCount = slot < 0 ? 0 : 1;
            return;
        }
        if (++s_stableCount < ScanStable)
            return;
        s_synced = true;
        s_scanWanted = false;
        const bool moved = s_cursor.FromGame(page, slot);
        log.push_back(Utils::Format("xmenu: game cursor page %d slot %d, native cursor %s%s", page + 1, slot,
                                    EntryName(s_cursor.Index()), moved ? " (followed)" : ""));
    }

    void XMenuUpdate(u32 sceneManager, const std::string &scene, int fieldState, std::vector<std::string> &log)
    {
        s_frames++;
        const bool inField = scene == SceneField;
        if ((ActionProbeFlags() & WorldXMenu) != 0)
            s_probeMissing = 0;
        else if (s_probeMissing < GoneFrames)
            s_probeMissing++;
        const bool menuUp = inField && s_probeMissing < GoneFrames;

        // Field state 4 just began (or the field came back with it): most
        // likely the X menu sliding in, so keep the panel from flashing on.
        if (inField && fieldState == FieldStateMenu && (s_prevFieldState != FieldStateMenu || s_prevScene != scene))
            s_pendingUntil = s_frames + PendingFrames;
        if (!inField || (fieldState >= 0 && fieldState != FieldStateMenu))
            s_pendingUntil = 0;
        s_prevFieldState = fieldState;
        s_prevScene = scene;

        if (!menuUp && s_frames >= s_pendingUntil)
            s_yield = false;

        // The panel: off while the native menu is (about to be) up. A panel
        // turned on while we hold it off is the player's doing (L+R): step aside.
        // Also while an entry with a native screen opens (field and loading
        // frames): the X menu is gone then, and the panel would show until
        // the native screen holds it off in its own scene.
        const bool nativeOpening = (inField || scene.empty()) && s_opening >= 0 && ActionRunning()
                                   && OpensNative(s_opening);
        bool holdOff = s_enabled && !s_yield && (menuUp || s_frames < s_pendingUntil || nativeOpening);
        if (holdOff && s_forcedOff && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            holdOff = false;
            log.push_back("xmenu: panel turned on by hand, native menu hidden until the X menu closes");
        }
        if (holdOff)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("xmenu: native menu up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            // Clears the override: AutoPanel picks the mode for whatever is
            // up now (Quick Link and the save dialog: panel; Party: full).
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }

        if (s_opening >= 0 && !ActionRunning())
        {
            s_opening = -1;
            if (s_mapAfter)
                MapRequestOpen();
            s_mapAfter = false;
        }

        const bool shown = s_enabled && !s_yield && menuUp;
        if (shown && !s_shown)
        {
            if (s_pixels == nullptr)
                s_pixels = new (std::nothrow) u8[PixelBytes];
            // The game's font and icons (normally loaded at the first field
            // already); the menu redraws with them once they are in.
            UI::RequestAssets();
            s_shownAt = s_frames;
            s_synced = false;
            s_stableCount = 0;
            s_stableSlot = s_stablePage = -1;
            s_lastScan = s_scanCount;
            s_scanWanted = true;
            s_repeatKey = 0;
            log.push_back(Utils::Format("xmenu: shown (game page %d)%s, heap %u KB used",
                                        (ActionProbeFlags() & WorldXMenuPage2) ? 2 : 1,
                                        s_pixels == nullptr ? ", NO MEMORY for the layer" : "", HeapUsedKb()));
        }
        else if (!shown && s_shown)
        {
            s_scanWanted = false;
            s_freeAt = s_frames + FreeAfter;
            log.push_back("xmenu: hidden");
        }
        s_shown = shown;
        if (!shown && s_pixels != nullptr && s_frames >= s_freeAt)
        {
            delete[] s_pixels;
            s_pixels = nullptr;
            log.push_back(Utils::Format("xmenu: layer freed, heap %u KB used", HeapUsedKb()));
        }

        // Not while an entry opens: the game turns its page then, and its
        // cursor is the entry being tapped anyway (Chosen).
        if (shown && s_opening < 0)
            SyncFromGame(log);

        // Input: ours while the menu shows and nothing is being opened. The
        // action layer captures everything while it runs and gives our
        // capture back afterwards, so ours is only ended once it is done.
        const bool owning = shown && s_opening < 0;
        if (owning && !ActionRunning())
        {
            if (!s_capturing)
            {
                InputCaptureBegin(CaptureKeys, true, false);
                s_capturing = true;
            }
            HandleInput(sceneManager, log);
        }
        else if (s_capturing && !owning && !ActionRunning())
        {
            InputCaptureEnd();
            s_capturing = false;
        }

        Publish();
    }

    bool XMenuCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(XMenuStatus());
        }
        else if (args == "status")
            log.push_back(XMenuStatus());
        else if (args.compare(0, 7, "select ") == 0 || args.compare(0, 5, "open ") == 0)
        {
            const bool open = args[0] == 'o';
            const std::string name = args.substr(open ? 5 : 7);
            const int index = XMenuFind(name);
            if (index < 0)
                log.push_back("xmenu: no entry '" + name + "'");
            else if (!s_shown || s_opening >= 0 || ActionRunning())
                log.push_back("xmenu: " + args + " refused: the native menu isn't waiting for input");
            else if (open)
                Open(index, sceneManager, log);
            else
            {
                MoveTo(index, log);
                if (log.empty())
                    log.push_back(Utils::Format("xmenu: cursor %s", EntryName(index)));
            }
            Publish();
        }
        else
            return false;
        return true;
    }

    std::string XMenuStatus(void)
    {
        return Utils::Format("xmenu: %s, %s, cursor %s%s%s%s%s", s_enabled ? "on" : "off",
                             s_shown ? "shown" : "hidden", EntryName(s_cursor.Index()),
                             s_opening >= 0 ? (std::string(", opening ") + EntryName(s_opening)).c_str() : "",
                             s_yield ? ", panel on by hand" : "", s_forcedOff ? ", holding panel off" : "",
                             s_capturing ? ", input captured" : "");
    }
}
