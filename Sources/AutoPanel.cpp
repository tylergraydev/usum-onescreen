#include "AutoPanel.hpp"

#include <CTRPluginFramework.hpp>

#include "Panel.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Scene IDs from SceneModule() (see docs/OFFSETS.md).
    static const char *SceneTitle = "TitleMenu";
    static const char *SceneField = "FieldRo";
    static const char *SceneParty = "main:00647FCC";

    static bool s_enabled = true;
    static bool s_override = false;
    static bool s_menuOpen = false;     // X menu, an overlay inside FieldRo
    static std::string s_scene;
    static u32 s_prevHeld = 0;

    static PanelMode ModeFor(const std::string &scene)
    {
        if (scene == SceneTitle)
            return PanelMode::Off;
        if (scene == SceneField)
            return s_menuOpen ? PanelMode::Panel : PanelMode::Off;
        if (scene == SceneParty)
            return PanelMode::Full;
        // Battle, Zukan, Bag, and anything not mapped yet: the bottom screen
        // probably matters, so show it.
        return PanelMode::Panel;
    }

    bool AutoPanelUpdate(const std::string &scene, u32 gameHeld)
    {
        const u32 pressed = gameHeld & ~s_prevHeld;
        s_prevHeld = gameHeld;

        if (!scene.empty() && scene != s_scene)
        {
            s_scene = scene;
            s_override = false;
            if (scene == SceneTitle)
                s_menuOpen = false;
        }

        // The X menu opens and closes on X; B backs out of it. Selecting an
        // entry switches scene, and coming back returns to the open menu, so
        // the flag survives other scenes. Closing it by touch isn't seen.
        if (s_scene == SceneField)
        {
            if (pressed & Key::X)
                s_menuOpen = !s_menuOpen;
            else if (pressed & Key::B)
                s_menuOpen = false;
        }

        if (!s_enabled || s_override || s_scene.empty())
            return false;

        const PanelMode want = ModeFor(s_scene);
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

    std::string AutoPanelStatus(void)
    {
        return Utils::Format("auto: %s, scene %s, x menu %s%s",
                             s_enabled ? "on" : "off", s_scene.empty() ? "(none)" : s_scene.c_str(),
                             s_menuOpen ? "open" : "closed", s_override ? ", manual override" : "");
    }
}
