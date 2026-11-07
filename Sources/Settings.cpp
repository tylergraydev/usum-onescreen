#include "Settings.hpp"
#include "SettingsFile.hpp"

#include <cstdlib>
#include <cerrno>
#include <cmath>

namespace OneScreen
{
    static Settings s_current;

    struct KeyName
    {
        const char *name;
        uint32_t    bits;
    };

    // Names accepted in key settings. Up/Down/Left/Right are the D-pad.
    static const KeyName KeyNames[] = {
        { "A", KeyBits::A }, { "B", KeyBits::B }, { "X", KeyBits::X }, { "Y", KeyBits::Y },
        { "L", KeyBits::L }, { "R", KeyBits::R }, { "ZL", KeyBits::ZL }, { "ZR", KeyBits::ZR },
        { "Start", KeyBits::Start }, { "Select", KeyBits::Select },
        { "Up", KeyBits::DUp }, { "Down", KeyBits::DDown }, { "Left", KeyBits::DLeft }, { "Right", KeyBits::DRight },
    };

    static std::string Lower(std::string s)
    {
        for (char &c : s)
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
        return s;
    }

    static std::string Trim(const std::string &s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
            a++;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
            b--;
        return s.substr(a, b - a);
    }

    // Lower case without '-', '_' and spaces, so "Bottom-Right", "bottom_right"
    // and "bottomright" are the same value.
    static std::string Squash(const std::string &s)
    {
        std::string out;
        for (char c : Lower(s))
            if (c != '-' && c != '_' && c != ' ' && c != '\t')
                out += c;
        return out;
    }

    bool ParseAspect(const std::string &value, float &out)
    {
        const std::string v = Lower(Trim(value));
        float aspect;
        if (v == "off") aspect = 0.0f;
        else if (v == "16:9") aspect = 16.0f / 9.0f;
        else if (v == "21:9") aspect = 21.0f / 9.0f;
        else
        {
            char *end = nullptr;
            errno = 0;
            aspect = std::strtof(v.c_str(), &end);
            if (v.empty() || *end != '\0' || errno == ERANGE
                || !std::isfinite(aspect) || aspect < 0.0f)
                return false;
        }
        out = aspect;
        return true;
    }

    bool ParseBattleSpeed(const std::string &value, BattleSpeed &out)
    {
        const std::string v = Lower(Trim(value));
        if (v == "normal") out = BattleSpeed::Normal;
        else if (v == "fast") out = BattleSpeed::Fast;
        else if (v == "instant") out = BattleSpeed::Instant;
        else return false;
        return true;
    }

    const char *BattleSpeedName(BattleSpeed speed)
    {
        switch (speed)
        {
        case BattleSpeed::Fast: return "fast";
        case BattleSpeed::Instant: return "instant";
        default: return "normal";
        }
    }

    const char *BattleSpeedTitle(BattleSpeed speed)
    {
        switch (speed)
        {
        case BattleSpeed::Fast: return "Fast";
        case BattleSpeed::Instant: return "Instant";
        default: return "Normal";
        }
    }

    static bool ParseKeys(const std::string &value, uint32_t &keys)
    {
        const std::string v = Squash(value);
        if (v == "none")
        {
            keys = 0;
            return true;
        }
        uint32_t out = 0;
        size_t start = 0;
        while (start <= v.size())
        {
            size_t end = v.find('+', start);
            if (end == std::string::npos)
                end = v.size();
            const std::string part = v.substr(start, end - start);
            uint32_t bits = 0;
            for (const KeyName &k : KeyNames)
                if (part == Lower(k.name))
                    bits = k.bits;
            if (bits == 0)
                return false;
            out |= bits;
            start = end + 1;
        }
        keys = out;
        return true;
    }

    static bool ParseBool(const std::string &value, bool &out)
    {
        const std::string v = Squash(value);
        if (v == "on" || v == "true" || v == "yes" || v == "1")
            out = true;
        else if (v == "off" || v == "false" || v == "no" || v == "0")
            out = false;
        else
            return false;
        return true;
    }

    static bool ParseSceneMode(const std::string &value, SceneMode &out)
    {
        const std::string v = Squash(value);
        if (v == "off")        out = SceneMode::Off;
        else if (v == "panel") out = SceneMode::Panel;
        else if (v == "full")  out = SceneMode::Full;
        else
            return false;
        return true;
    }

    static void SetScene(Settings &settings, const std::string &scene, SceneMode mode)
    {
        const std::string key = Lower(scene);
        for (auto &entry : settings.scenes)
            if (Lower(entry.first) == key)
            {
                entry.second = mode;
                return;
            }
        settings.scenes.emplace_back(scene, mode);
    }

    // Applies one `key = value` outside [scenes]. Returns an error, or "" if it applied.
    static std::string ApplyKey(Settings &s, const std::string &key, const std::string &value)
    {
        const std::string k = Lower(key);
        const std::string v = Squash(value);

        if (k == "instant_text")
        {
            if (v != "on" && v != "off")
                return "bad instant_text '" + value + "' (use on or off)";
            s.instantText = v == "on";
        }
        else if (k == "battle_messages" || k == "battle_bars")
        {
            if (!ParseBattleSpeed(value, k == "battle_messages" ? s.battleMessages : s.battleBars))
                return "bad " + key + " '" + value + "' (use normal, fast or instant)";
        }
        else if (k == "aspect")
        {
            if (!ParseAspect(value, s.aspect))
                return "bad aspect '" + value + "' (use 0, off, a positive float, 16:9 or 21:9)";
        }
        else if (k == "panel_key" || k == "touch_key" || k == "map_key")
        {
            uint32_t keys = 0;
            if (!ParseKeys(value, keys))
                return "bad key '" + value + "' (use A B X Y L R ZL ZR Start Select Up Down Left Right, "
                       "joined with +, or none)";
            (k == "panel_key" ? s.panelKey : k == "touch_key" ? s.touchKey : s.mapKey) = keys;
        }
        else if (k == "cursor_move")
        {
            if (v == "cstick")                          s.cursorSource = CursorSource::CStick;
            else if (v == "dpad")                       s.cursorSource = CursorSource::DPad;
            else if (v == "circlepad" || v == "cpad")   s.cursorSource = CursorSource::CirclePad;
            else
                return "bad cursor_move '" + value + "' (use cstick, dpad or circlepad)";
        }
        else if (k == "cursor_speed")
        {
            char *end = nullptr;
            const unsigned long n = std::strtoul(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0' || n < 1 || n > 20)
                return "bad cursor_speed '" + value + "' (use 1 to 20)";
            s.cursorSpeed = (uint32_t)n;
        }
        else if (k == "size")
        {
            if (v == "1/2" || v == "half")  s.panelSize = PanelSize::Half;
            else if (v == "2/3")            s.panelSize = PanelSize::TwoThirds;
            else
                return "bad size '" + value + "' (use 1/2 or 2/3)";
        }
        else if (k == "corner")
        {
            if (v == "bottomright")     s.panelCorner = PanelCorner::BottomRight;
            else if (v == "bottomleft") s.panelCorner = PanelCorner::BottomLeft;
            else if (v == "topright")   s.panelCorner = PanelCorner::TopRight;
            else if (v == "topleft")    s.panelCorner = PanelCorner::TopLeft;
            else
                return "bad corner '" + value + "' (use bottom-right, bottom-left, top-right or top-left)";
        }
        else if (k == "auto")
        {
            if (!ParseBool(value, s.autoPanel))
                return "bad auto '" + value + "' (use on or off)";
        }
        else if (k == "layout")
        {
            if (!ParseBattleLayout(value, s.battleLayout))
                return "bad layout '" + value + "' (use classic, bar or rail)";
        }
        else
            return "unknown key '" + key + "'";
        return "";
    }

    Settings ParseSettings(const std::string &text, std::vector<std::string> &warnings)
    {
        Settings s;
        std::string section;
        bool sectionKnown = true;
        unsigned lineNo = 0;

        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            start = end + 1;
            lineNo++;

            // No value uses ';' or '#', so either starts a comment anywhere.
            const size_t comment = line.find_first_of(";#");
            if (comment != std::string::npos)
                line.erase(comment);
            line = Trim(line);
            if (lineNo == 1 && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
                line = Trim(line.substr(3));    // UTF-8 BOM from Notepad
            if (line.empty())
                continue;

            const std::string where = "line " + std::to_string(lineNo) + ": ";

            if (line.front() == '[')
            {
                if (line.back() != ']')
                {
                    warnings.push_back(where + "bad section header '" + line + "'");
                    continue;
                }
                section = Lower(Trim(line.substr(1, line.size() - 2)));
                sectionKnown = section == "controls" || section == "panel" || section == "scenes" || section == "display"
                           || section == "battle" || section == "speed";
                if (!sectionKnown)
                    warnings.push_back(where + "unknown section [" + section + "], its keys are ignored");
                continue;
            }

            const size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                warnings.push_back(where + "expected key = value, got '" + line + "'");
                continue;
            }
            const std::string key = Trim(line.substr(0, eq));
            const std::string value = Trim(line.substr(eq + 1));
            if (key.empty() || value.empty())
            {
                warnings.push_back(where + "empty key or value");
                continue;
            }
            if (!sectionKnown)
                continue;

            if (section == "scenes")
            {
                SceneMode mode;
                if (ParseSceneMode(value, mode))
                    SetScene(s, key, mode);
                else
                    warnings.push_back(where + "bad mode '" + value + "' for scene " + key + " (use off, panel or full)");
                continue;
            }

            // Keys are unique across sections, so a key in the wrong section
            // (or before any header) still works.
            const std::string error = ApplyKey(s, key, value);
            if (!error.empty())
                warnings.push_back(where + error);
        }
        return s;
    }

    std::string DefaultSettingsText(void)
    {
        return
            "; USUM One Screen settings. Read once at boot; the plugin menu can reload it.\n"
            "; Delete this file to get the defaults back. Lines starting with ; or # are comments.\n"
            "; Problems with this file are listed in usum-onescreen.log.\n"
            "\n"
            "[display]\n"
            "; Experimental 3D camera aspect: 0/off (original), 16:9, 21:9 or a positive float.\n"
            "; Only new cameras change; see docs/WIDESCREEN.md.\n"
            "aspect = 0\n"
            "\n"
            "[battle]\n"
            "; Native battle menu look: classic, bar or rail. The plugin menu's\n"
            "; \"Battle layout\" entry changes it and saves it here.\n"
            "layout = classic\n"
            "\n"
            "[speed]\n"
            "; Remove typing delays; page ends still need a button press.\n"
            "instant_text = off\n"
            "; Battle message waits and HP/EXP bars: normal, fast or instant.\n"
            "battle_messages = normal\n"
            "battle_bars = normal\n"
            "\n"
            "[controls]\n"
            "; Cycles the bottom screen view: off, panel, full. Keys: A B X Y L R ZL ZR\n"
            "; Start Select Up Down Left Right (D-pad), joined with + for a combo, or none.\n"
            "panel_key = L+R\n"
            "; Touches the bottom screen at the cursor while held.\n"
            "touch_key = ZL\n"
            "; Opens and closes the top-screen map (B closes it too).\n"
            "map_key = ZR\n"
            "; What moves the cursor: cstick, dpad or circlepad. dpad and circlepad still\n"
            "; reach the game as well.\n"
            "cursor_move = cstick\n"
            "; Top cursor speed in pixels per frame, 1 to 20. It starts at 1 for precise\n"
            "; taps and speeds up while the stick stays held.\n"
            "cursor_speed = 6\n"
            "\n"
            "[panel]\n"
            "; Panel size: 1/2 or 2/3. 2/3 is easier to read but costs more emulator\n"
            "; speed (see docs/PERF.md).\n"
            "size = 1/2\n"
            "; Panel corner: bottom-right, bottom-left, top-right or top-left.\n"
            "corner = bottom-right\n"
            "; on: the view follows the scene below. off: it stays as panel_key sets it\n"
            "; (the default: native top-screen menus come first, the panel is a fallback).\n"
            "auto = off\n"
            "\n"
            "[scenes]\n"
            "; View per scene when auto = on: off, panel or full. A panel_key press holds\n"
            "; until the next scene change. Scene IDs are the ones in the log (scene: ...).\n"
            "TitleMenu = off\n"
            "; Overworld, and the X menu open over it (also Quick Link, the save\n"
            "; dialog and Festival Plaza's menu).\n"
            "FieldRo = off\n"
            "FieldRo.xmenu = panel\n"
            "; Pokedex\n"
            "Zukan = panel\n"
            "Battle = panel\n"
            "; Party\n"
            "main:00647FCC = full\n"
            "; Bag\n"
            "main:006488C8 = panel\n"
            "; Pokemon summary\n"
            "Status = panel\n"
            "; PC boxes\n"
            "Box = full\n"
            "TownMap = full\n"
            "TrainerPass = panel\n"
            "; Options\n"
            "Config = full\n"
            "; Pokemon Refresh\n"
            "Kawaigari = full\n"
            "BattleVideoPlayer = panel\n"
            "; Any scene not listed.\n"
            "default = panel\n";
    }

    static const SceneMode *FindScene(const Settings &settings, const std::string &scene)
    {
        const std::string key = Lower(scene);
        for (auto it = settings.scenes.rbegin(); it != settings.scenes.rend(); ++it)
            if (Lower(it->first) == key)
                return &it->second;
        return nullptr;
    }

    SceneMode ModeForScene(const Settings &settings, const std::string &scene, bool xmenuOpen)
    {
        const SceneMode *mode = nullptr;
        if (xmenuOpen && Lower(scene) == "fieldro")
            mode = FindScene(settings, "FieldRo.xmenu");
        if (mode == nullptr)
            mode = FindScene(settings, scene);
        if (mode == nullptr)
            mode = FindScene(settings, "default");
        return mode != nullptr ? *mode : SceneMode::Panel;
    }

    void CursorKeys(CursorSource source, uint32_t &up, uint32_t &down, uint32_t &left, uint32_t &right)
    {
        switch (source)
        {
        case CursorSource::DPad:
            up = KeyBits::DUp; down = KeyBits::DDown; left = KeyBits::DLeft; right = KeyBits::DRight;
            break;
        case CursorSource::CirclePad:
            up = KeyBits::CPadUp; down = KeyBits::CPadDown; left = KeyBits::CPadLeft; right = KeyBits::CPadRight;
            break;
        default:
            up = KeyBits::CStickUp; down = KeyBits::CStickDown; left = KeyBits::CStickLeft; right = KeyBits::CStickRight;
            break;
        }
    }

    std::string KeysToString(uint32_t keys)
    {
        if (keys == 0)
            return "none";
        std::string out;
        for (const KeyName &k : KeyNames)
            if (keys & k.bits)
                out += (out.empty() ? "" : "+") + std::string(k.name);
        return out;
    }

    const char *SceneModeName(SceneMode mode)
    {
        static const char *names[] = { "off", "panel", "full" };
        return names[(int)mode];
    }

    std::string DescribeSettings(const Settings &s)
    {
        static const char *sources[] = { "cstick", "dpad", "circlepad" };
        static const char *sizes[] = { "1/2", "2/3" };
        static const char *corners[] = { "bottom-right", "bottom-left", "top-right", "top-left" };

        std::string out = "panel_key " + KeysToString(s.panelKey) + ", touch_key " + KeysToString(s.touchKey)
                          + ", map_key " + KeysToString(s.mapKey)
                          + ", cursor_move " + sources[(int)s.cursorSource]
                          + ", cursor_speed " + std::to_string(s.cursorSpeed) + "\n"
                          + "size " + sizes[(int)s.panelSize] + ", corner " + corners[(int)s.panelCorner]
                          + ", auto " + (s.autoPanel ? "on" : "off") + "\n"
                          + "aspect " + (s.aspect == 0.0f ? std::string("off") : std::to_string(s.aspect))
                          + ", battle layout " + BattleLayoutName(s.battleLayout) + "\n"
                          + "instant text " + (s.instantText ? "on" : "off")
                          + ", battle messages " + BattleSpeedName(s.battleMessages)
                          + ", battle bars " + BattleSpeedName(s.battleBars) + "\n"
                          + "scenes:";
        for (const auto &entry : s.scenes)
            out += " " + entry.first + "=" + SceneModeName(entry.second);
        return out;
    }

    const Settings &GetSettings(void)
    {
        return s_current;
    }

    void SetSettings(const Settings &settings)
    {
        s_current = settings;
    }
}
