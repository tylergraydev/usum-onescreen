#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Plain C++ only (no libctru / CTRPF), so tests/ can build this on the PC.

namespace OneScreen
{
    // Key bits as the game and CTRPF see them (CTRPluginFramework::Key).
    namespace KeyBits
    {
        const uint32_t A = 1u << 0, B = 1u << 1, Select = 1u << 2, Start = 1u << 3;
        const uint32_t DRight = 1u << 4, DLeft = 1u << 5, DUp = 1u << 6, DDown = 1u << 7;
        const uint32_t R = 1u << 8, L = 1u << 9, X = 1u << 10, Y = 1u << 11;
        const uint32_t ZL = 1u << 14, ZR = 1u << 15;
        const uint32_t CStickRight = 1u << 24, CStickLeft = 1u << 25, CStickUp = 1u << 26, CStickDown = 1u << 27;
        const uint32_t CPadRight = 1u << 28, CPadLeft = 1u << 29, CPadUp = 1u << 30, CPadDown = 1u << 31;
    }

    // Same order as PanelMode, so the two convert with a cast.
    enum class SceneMode : uint8_t { Off, Panel, Full };

    enum class CursorSource : uint8_t { CStick, DPad, CirclePad };
    enum class PanelSize : uint8_t { Half, TwoThirds };     // 2/3 is parsed but not drawn yet
    enum class PanelCorner : uint8_t { BottomRight, BottomLeft, TopRight, TopLeft };

    struct Settings
    {
        uint32_t     panelKey = KeyBits::ZR;            // cycles off / panel / full
        uint32_t     touchKey = KeyBits::ZL;            // cursor touches while held
        CursorSource cursorSource = CursorSource::CStick;
        uint32_t     cursorSpeed = 6;                   // pixels per frame once the stick has been held a while
        PanelSize    panelSize = PanelSize::Half;
        PanelCorner  panelCorner = PanelCorner::BottomRight;
        bool         autoPanel = true;

        // Mode per scene ID (SceneModule() names). Later entries win.
        // "FieldRo.xmenu" is the X menu overlay inside FieldRo; "default" is
        // any scene not listed.
        std::vector<std::pair<std::string, SceneMode>> scenes = {
            { "TitleMenu",     SceneMode::Off },
            { "FieldRo",       SceneMode::Off },
            { "FieldRo.xmenu", SceneMode::Panel },
            { "Zukan",         SceneMode::Panel },
            { "Battle",        SceneMode::Panel },
            { "main:00647FCC", SceneMode::Full },     // Party
            { "main:006488C8", SceneMode::Panel },    // Bag
            { "default",       SceneMode::Panel },
        };
    };

    // Tolerant INI parse: `;` / `#` comments, any whitespace, case-insensitive
    // names, unknown keys and bad values skipped. Anything skipped is added to
    // `warnings` as "line N: ...". Missing keys keep their defaults.
    Settings ParseSettings(const std::string &text, std::vector<std::string> &warnings);

    // The commented file written when /usum-onescreen.ini is missing. Parses
    // back to Settings{} with no warnings.
    std::string DefaultSettingsText(void);

    // Mode for `scene`; `xmenuOpen` selects FieldRo.xmenu inside FieldRo.
    SceneMode ModeForScene(const Settings &settings, const std::string &scene, bool xmenuOpen);

    // Up, down, left, right key bits for the cursor source.
    void CursorKeys(CursorSource source, uint32_t &up, uint32_t &down, uint32_t &left, uint32_t &right);

    // "ZR", "L+R"; "none" for 0.
    std::string KeysToString(uint32_t keys);
    const char *SceneModeName(SceneMode mode);

    // Short multi-line summary for the plugin menu and the log.
    std::string DescribeSettings(const Settings &settings);

    // The settings in use. Set once at boot (and on reload from the menu).
    const Settings &GetSettings(void);
    void SetSettings(const Settings &settings);
}
