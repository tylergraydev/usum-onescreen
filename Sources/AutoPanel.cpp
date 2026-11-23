#include "AutoPanel.hpp"

#include <CTRPluginFramework.hpp>

#include "FieldSettle.hpp"
#include "Game.hpp"
#include "Panel.hpp"
#include "Scene.hpp"
#include "Settings.hpp"
#include "QuickSave.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Scene IDs from CurrentScene() (see docs/OFFSETS.md). The mode for each
    // scene comes from the [scenes] settings. SceneField and SceneBattle come
    // from ActionQueue.hpp (via QuickSave.hpp) and drive the X menu flag.
    static const char *SceneTitle = "TitleMenu";

    static bool s_enabled = false;      // Settings::autoPanel; set at boot
    static bool s_nativeBattle = false;
    static bool s_afterBattle = false;  // in the Pokédex straight after a battle (a catch)
    static bool s_override = false;
    static bool s_menuOpen = false;     // X menu, an overlay inside FieldRo
    static int s_fieldState = -1;       // last FieldState() that worked, -1 if none yet
    static FieldSettle s_settle;        // the field state must hold before it counts
    static int s_unread = 0;            // frames in a row the field state couldn't be read

    // It can't be read for a few frames while the field loads; past this,
    // assume it can't be read here and use keys.
    static const int FieldWaitFrames = 60;
    static std::string s_scene;
    static u32 s_prevHeld = 0;

    static bool HasUsumEngine(void)
    {
        const GameInfo *game = CurrentGame();
        return game && (game->features & FeatureUsumEngine);
    }

    bool AutoPanelUpdate(const std::string &scene, u32 gameHeld, int fieldState)
    {
        const bool usum = HasUsumEngine();
        if (usum && QuickSaveRunning())
        {
            const bool changed = GetPanelMode() != PanelMode::Off;
            SetPanelMode(PanelMode::Off);
            return changed;
        }
        const u32 pressed = gameHeld & ~s_prevHeld;
        s_prevHeld = gameHeld;

        if (!scene.empty() && scene != s_scene)
        {
            // A catch goes Battle -> Pokédex (nickname question).
            s_afterBattle = usum && scene == "Zukan" && s_scene == SceneBattle;
            s_scene = scene;
            s_override = false;
            if (usum && scene == SceneTitle)
                s_menuOpen = false;
            if (usum && scene == SceneField)
                s_settle.Enter();
        }

        // The field state says whether the X menu is open. After entering the
        // field it must hold for FieldSettle::Frames first. While the field
        // loads it can't be read; once it has worked, wait up to
        // FieldWaitFrames for it. Otherwise fall back to keys: X opens and
        // closes the menu, B backs out of it, and closing it by touch isn't
        // seen. Selecting an entry switches scene and coming back returns to
        // the open menu, so the key-tracked flag survives other scenes.
        bool waiting = false;
        if (usum && s_scene == SceneField)
        {
            if (fieldState >= 0)
            {
                s_unread = 0;
                waiting = s_settle.Read(fieldState);
                s_fieldState = fieldState;
                s_menuOpen = fieldState == FieldStateMenu;
            }
            else if (s_fieldState >= 0 && s_unread < FieldWaitFrames)
            {
                s_unread++;
                waiting = true;
            }
            else if (pressed & Key::X)
                s_menuOpen = !s_menuOpen;
            else if (pressed & Key::B)
                s_menuOpen = false;
        }

        if (!s_enabled || s_override || s_scene.empty() || waiting)
            return false;

        const PanelMode want = usum && s_nativeBattle && (s_scene == SceneBattle || s_afterBattle)
                               ? PanelMode::Off : (PanelMode)ModeForScene(GetSettings(), s_scene, usum && s_menuOpen);
        if (want == GetPanelMode())
            return false;
        SetPanelMode(want);
        return true;
    }

    void AutoPanelManualOverride(void)
    {
        s_override = true;
    }

    void AutoPanelSetNativeBattle(bool on)
    {
        s_nativeBattle = HasUsumEngine() && on;
    }

    void AutoPanelSetEnabled(bool enabled)
    {
        s_enabled = enabled;
        s_override = false;
    }

    bool AutoPanelEnabled(void)
    {
        return s_enabled;
    }

    std::string AutoPanelStatus(void)
    {
        if (!HasUsumEngine())
            return Utils::Format("auto: %s, scene %s%s",
                                 s_enabled ? "on" : "off", s_scene.empty() ? "(none)" : s_scene.c_str(),
                                 s_override ? ", manual override" : "");
        const std::string source = s_fieldState >= 0 ? Utils::Format("field state %d", s_fieldState) : "keys";
        return Utils::Format("auto: %s, scene %s, x menu %s (%s)%s%s",
                             s_enabled ? "on" : "off", s_scene.empty() ? "(none)" : s_scene.c_str(),
                             s_menuOpen ? "open" : "closed", source.c_str(), s_override ? ", manual override" : "",
                             s_nativeBattle ? ", native battle" : "");
    }
}
