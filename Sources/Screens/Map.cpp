#include "Screens/Map.hpp"

#include <cstring>
#include <new>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Panel.hpp"
#include "RomfsText.hpp"
#include "Scene.hpp"
#include "Screens/MapView.hpp"
#include "Settings.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "Where.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Current text language (0-9, 2 = English): picks the message GARC
    // a/0/3/<lang>. docs/RE2-text.md, docs/OFFSETS.md.
    static const u32 LanguageByte = 0x671A10;
    // Place names: message file 72 of that GARC (docs/RE2-text.md, RE4-map.md).
    static const u32 PlaceNamesFile = 72;

    // Field readings while the map is open, every this many frames.
    static const u32 ReadEvery = 4;
    // A key press seen by both readers (the game's pad read and CTRPF's) may
    // land a frame apart; ignore a second toggle this soon.
    static const u32 ToggleGuard = 10;
    // MapRequestOpen waits this long for the field.
    static const u32 RequestFrames = 90;

    // --- Shared with the OSD callback (game thread) ---
    static volatile bool s_shown = false;
    static MapView       s_views[2];
    static volatile u32  s_viewSlot = 0;        // the slot the OSD callback reads
    static volatile u32  s_viewVersion = 0;
    static volatile bool s_copyFailed = false;  // the GPU copy refused; the window shows the note instead
    static u8           *s_pixels = nullptr;
    static const size_t  PixelBytes = MapArea.w * MapArea.h * 4;

    // --- Plugin thread ---
    static bool s_enabled = true;
    static bool s_open = false;
    static bool s_forcedOff = false;
    static bool s_capturing = false;
    static u32  s_captureMask = 0;
    static u32  s_frames = 0;
    static u32  s_lastToggle = 0;
    static u32  s_requestUntil = 0;
    static const char *s_requestHow = "X menu";
    static MapView s_view;
    static bool s_logged = false;               // the current view's zone was logged
    static MapZoneFilter s_zones;
    static bool s_namesTried = false;
    static std::vector<std::string> s_names;
    static std::string s_namesStatus = "place names: not loaded";

    // --- OSD callback ---

    static UI::Layer s_layer;
    static u32 s_drawnVersion = ~0u;

    static bool MapOnScreen(const Screen &screen)
    {
        if (!screen.IsTop || !s_shown || s_pixels == nullptr)
            return false;
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        const u32 version = s_viewVersion;
        bool redraw = version != s_drawnVersion;
        if (!s_layer.Matches(fb))
        {
            if (!s_layer.Setup(fb, MapArea, MapRadius, s_pixels, PixelBytes))
                return false;
            redraw = true;
        }
        const MapView view = s_views[s_viewSlot];
        const bool picture = view.picture && !s_copyFailed;
        if (redraw)
        {
            MapView drawn = view;
            drawn.picture = picture;
            UI::Canvas canvas = s_layer.Begin();
            DrawMap(canvas, drawn);
            s_drawnVersion = version;
        }
        if (!picture)
        {
            s_layer.Present(fb);
            return true;
        }
        // The window is the game's own map, copied by the GPU; the CPU
        // leaves those pixels alone.
        s_layer.Present(fb, MapWindow);
        if (!PanelCopyBottomRect(screen, MapSource.x, MapSource.y, MapSource.w, MapSource.h, MapWindow.x, MapWindow.y))
        {
            s_copyFailed = true;
            s_drawnVersion = ~0u;
        }
        return true;
    }

    void StartMap(void)
    {
        OSD::Run(MapOnScreen);
    }

    // --- Plugin thread ---

    // Place names from the game's own RomFS (ARCHIVE_ROMFS opens the running
    // title's), so nothing of the game's text is shipped. Read once.
    static void LoadNames(std::vector<std::string> &log)
    {
        if (s_namesTried)
            return;
        s_namesTried = true;
        u8 lang = 0xFF;
        Process::Read8(LanguageByte, lang);
        if (lang > 9)
        {
            s_namesStatus = Utils::Format("place names: language byte %u not 0-9", lang);
            log.push_back("map: " + s_namesStatus);
            return;
        }
        static const u32 lowPath[3] = { 0, 0, 0 };
        const FS_Path archive = { PATH_EMPTY, 1, "" };
        const FS_Path file = { PATH_BINARY, sizeof(lowPath), lowPath };
        Handle handle = 0;
        const Result res = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
        if (R_FAILED(res))
        {
            s_namesStatus = Utils::Format("place names: can't open the RomFS (0x%08lX)", (u32)res);
            log.push_back("map: " + s_namesStatus);
            return;
        }
        const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
            u32 got = 0;
            return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
        };
        const std::string garc = Utils::Format("a/0/3/%u", lang);
        std::string error;
        const u64 start = svcGetSystemTick();
        const bool ok = ReadMessageFile(read, garc, PlaceNamesFile, s_names, error);
        const u64 ticks = svcGetSystemTick() - start;
        FSFILE_Close(handle);
        if (!ok)
        {
            s_names.clear();
            s_namesStatus = "place names: " + error;
        }
        else
            s_namesStatus = Utils::Format("place names: %u lines from %s file %lu (%lu ms)", s_names.size(),
                                          garc.c_str(), PlaceNamesFile, (u32)(ticks / CPU_TICKS_PER_MSEC));
        log.push_back("map: " + s_namesStatus);
    }

    static void Publish(const MapView &view)
    {
        const u32 next = s_viewSlot ^ 1;
        s_views[next] = view;
        s_viewSlot = next;
        s_viewVersion = s_viewVersion + 1;
    }

    static void Refresh(std::vector<std::string> &log)
    {
        const WhereState where = ReadWhere();
        // Keep the last reading through door transitions (no player then).
        if (!where.valid && s_view.known)
            return;
        const MapView view = MakeMapView(where, s_names, KeysToString(GetSettings().mapKey));
        // A new zone counts once it has settled (MapZoneFilter): right
        // after a door the field reads a wrong zone for a few frames.
        if (view.known)
        {
            int ignored = -1;
            const bool changed = s_zones.Feed(view.zone, where.entryZone, ignored);
            if (ignored >= 0)
                log.push_back(Utils::Format("map: zone %d not settled, ignored", ignored));
            if (!changed && view.zone != s_zones.Current())
                return;
        }
        if (view.known && (!s_logged || view.zone != s_view.zone))
        {
            log.push_back(std::string(s_logged ? "map: now " : "map: shown ") + DescribeMapView(view));
            s_logged = true;
        }
        if (view != s_view || s_viewVersion == 0)
        {
            s_view = view;
            Publish(view);
        }
    }

    static void Open(const char *how, std::vector<std::string> &log)
    {
        LoadNames(log);
        if (s_pixels == nullptr)
            s_pixels = new (std::nothrow) u8[PixelBytes];
        s_open = true;
        s_logged = false;
        s_copyFailed = false;
        s_view = MapView();
        s_zones.Reset();
        s_lastToggle = s_frames;
        log.push_back(std::string("map: open (") + how + ")" + (s_pixels == nullptr ? ", NO MEMORY for the layer" : ""));
        Refresh(log);
        Publish(s_view);
    }

    static void Close(const char *why, std::vector<std::string> &log)
    {
        if (!s_open)
            return;
        s_open = false;
        s_lastToggle = s_frames;
        log.push_back(std::string("map: closed (") + why + ")");
    }

    void MapUpdate(u32 sceneManager, const std::string &scene, int fieldState, std::vector<std::string> &log)
    {
        (void)sceneManager;
        s_frames++;
        const bool inField = scene == SceneField;
        const bool menu = inField && fieldState == FieldStateMenu;
        const bool canOpen = s_enabled && inField && !menu && !ActionRunning();

        // Things that close it: another scene (not a transition, ""), the X
        // menu, the panel turned on by hand while we hold it off.
        if (s_open && !scene.empty() && !inField)
            Close(("scene " + scene).c_str(), log);
        else if (s_open && menu)
            Close("X menu", log);
        else if (s_open && s_forcedOff && GetPanelMode() != PanelMode::Off)
            Close("panel turned on by hand", log);

        if (s_requestUntil != 0)
        {
            if (canOpen && !s_open)
                Open(s_requestHow, log);
            if (s_open || s_frames >= s_requestUntil)
                s_requestUntil = 0;
        }

        // Keys: map_key toggles, B closes.
        const u32 mapKey = GetSettings().mapKey;
        u32 pressed = CaptureRealPressed();
        if (mapKey != 0 && Controller::IsKeysPressed(mapKey))
            pressed |= mapKey;
        if (s_frames - s_lastToggle >= ToggleGuard && s_enabled)
        {
            switch (MapKeys(s_open, canOpen, pressed, mapKey))
            {
            case MapKeyResult::Open:  Open(KeysToString(mapKey).c_str(), log); break;
            case MapKeyResult::Close: Close((pressed & MapKeyB) && !(pressed & mapKey) ? "B" : KeysToString(mapKey).c_str(), log); break;
            case MapKeyResult::None:  break;
            }
        }

        const bool shown = s_open && inField;
        if (shown && s_frames % ReadEvery == 0)
            Refresh(log);

        // The panel: off while the map shows (AutoPanel's manual override).
        if (shown)
        {
            if (GetPanelMode() != PanelMode::Off && !s_forcedOff)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("map: shown -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            s_forcedOff = false;
            // In the X menu the native X menu holds the panel itself; let
            // AutoPanel pick for anything else.
            if (!menu)
                AutoPanelSetEnabled(AutoPanelEnabled());
        }

        // Input: B (and map_key) are the map's while it shows; the circle
        // pad, A and X stay with the game, so the player can walk with it up.
        // Only end a capture that is still ours (the X menu may have begun
        // its own this frame).
        if (shown && !ActionRunning())
        {
            const u32 mask = MapKeyB | mapKey;
            if (!s_capturing || s_captureMask != mask)
            {
                InputCaptureBegin(mask, false, false);
                s_capturing = true;
                s_captureMask = mask;
            }
        }
        else if (s_capturing && !shown && !ActionRunning())
        {
            if (InputCaptureRequest() == s_captureMask)
                InputCaptureEnd();
            s_capturing = false;
        }

        if (shown && !s_shown)
            log.push_back("map: drawing" + std::string(s_copyFailed ? " (GPU copy refused)" : ""));
        else if (!shown && s_shown)
            log.push_back("map: hidden");
        s_shown = shown && s_pixels != nullptr;

        static bool reportedCopy = false;
        if (s_copyFailed && !reportedCopy)
        {
            reportedCopy = true;
            log.push_back("map: GPU copy of the Rotom map refused, showing the window without it");
        }
    }

    void MapRequestOpen(const char *how)
    {
        s_requestUntil = s_frames + RequestFrames;
        s_requestHow = how;
    }

    bool MapEnabled(void)
    {
        return s_enabled;
    }

    bool MapIsOpen(void)
    {
        return s_open;
    }

    void MapClose(const char *why, std::vector<std::string> &log)
    {
        Close(why, log);
    }

    bool MapCommand(const std::string &args, std::vector<std::string> &log)
    {
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            if (!s_enabled)
                Close("map off", log);
            log.push_back(MapStatus());
        }
        else if (args == "open")
        {
            if (s_open)
                log.push_back("map: already open");
            else
                MapRequestOpen();
        }
        else if (args == "close")
            Close("command", log);
        else if (args == "status")
            log.push_back(MapStatus());
        else
            return false;
        return true;
    }

    std::string MapStatus(void)
    {
        return Utils::Format("map: %s, %s%s%s, %s | %s", s_enabled ? "on" : "off", s_open ? "open" : "closed",
                             s_shown ? ", shown" : "", s_capturing ? ", input captured" : "",
                             s_view.known ? DescribeMapView(s_view).c_str() : "no reading", s_namesStatus.c_str());
    }
}
