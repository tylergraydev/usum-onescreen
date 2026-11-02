#include "AutoPanel.hpp"

#include <CTRPluginFramework.hpp>

#include "Panel.hpp"
#include "Scene.hpp"
#include "Settings.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Scene IDs from SceneModule() (see docs/OFFSETS.md). The mode for each
    // scene comes from the [scenes] settings; these two drive the X menu flag.
    static const char *SceneTitle = "TitleMenu";
    static const char *SceneField = "FieldRo";

    static bool s_enabled = true;
    static bool s_override = false;
    static bool s_menuOpen = false;     // X menu, an overlay inside FieldRo
    static int s_fieldState = -1;       // last FieldState() that worked, -1 if none yet
    static int s_settle = 0;            // frames the field state must hold before it counts

    // Coming back to the field from an app Rotom opened (Town Map, Pokédex),
    // the field state still says 4 for about 20 frames before it drops to 0.
    static const int FieldSettleFrames = 30;
    static std::string s_scene;
    static u32 s_prevHeld = 0;

    bool AutoPanelUpdate(const std::string &scene, u32 gameHeld, int fieldState)
    {
        const u32 pressed = gameHeld & ~s_prevHeld;
        s_prevHeld = gameHeld;

        if (!scene.empty() && scene != s_scene)
        {
            s_scene = scene;
            s_override = false;
            if (scene == SceneTitle)
                s_menuOpen = false;
            if (scene == SceneField)
                s_settle = FieldSettleFrames;
        }

        // The field state says whether the X menu is open. After entering the
        // field it must hold for FieldSettleFrames first. It can't be read for
        // a few frames while the field loads; once it has worked, wait for it
        // then. If it never worked, fall back to keys: X opens and
        // closes the menu, B backs out of it, and closing it by touch isn't
        // seen. Selecting an entry switches scene and coming back returns to
        // the open menu, so the key-tracked flag survives other scenes.
        bool waiting = false;
        if (s_scene == SceneField)
        {
            if (fieldState >= 0)
            {
                if (s_settle > 0)
                    s_settle = fieldState == s_fieldState ? s_settle - 1 : FieldSettleFrames;
                s_fieldState = fieldState;
                s_menuOpen = fieldState == FieldStateMenu;
                waiting = s_settle > 0;
            }
            else if (s_fieldState >= 0)
                waiting = true;
            else if (pressed & Key::X)
                s_menuOpen = !s_menuOpen;
            else if (pressed & Key::B)
                s_menuOpen = false;
        }

        if (!s_enabled || s_override || s_scene.empty() || waiting)
            return false;

        const PanelMode want = (PanelMode)ModeForScene(GetSettings(), s_scene, s_menuOpen);
        if (want == GetPanelMode())
            return false;
        SetPanelMode(want);
        return true;
    }

    void AutoPanelManualOverride(void)
    {
        s_override = true;
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
        const std::string source = s_fieldState >= 0 ? Utils::Format("field state %d", s_fieldState) : "keys";
        return Utils::Format("auto: %s, scene %s, x menu %s (%s)%s",
                             s_enabled ? "on" : "off", s_scene.empty() ? "(none)" : s_scene.c_str(),
                             s_menuOpen ? "open" : "closed", source.c_str(), s_override ? ", manual override" : "");
    }
}
