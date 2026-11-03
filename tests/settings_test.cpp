// Host tests for the settings parser and model (Sources/Settings.cpp).
// Build and run: make -C tests   (any C++17 compiler: g++, clang++, MSVC)

#include "Settings.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace OneScreen;

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond)                                                             \
    do                                                                          \
    {                                                                           \
        s_checks++;                                                             \
        if (!(cond))                                                            \
        {                                                                       \
            s_failures++;                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                       \
    } while (0)

static bool SameSettings(const Settings &a, const Settings &b)
{
    return a.panelKey == b.panelKey && a.touchKey == b.touchKey && a.mapKey == b.mapKey && a.cursorSource == b.cursorSource
        && a.cursorSpeed == b.cursorSpeed && a.panelSize == b.panelSize && a.panelCorner == b.panelCorner
        && a.autoPanel == b.autoPanel && a.scenes == b.scenes;
}

static Settings Parse(const std::string &text, std::vector<std::string> &warnings)
{
    warnings.clear();
    return ParseSettings(text, warnings);
}

// The default [scenes] table must give the same view per scene as AutoPanel.cpp
// did before settings existed: TitleMenu off, FieldRo off (panel with the X
// menu open), Party full, everything else panel. Auto is off by default and
// L+R cycles the view: native menus come first, the panel is a fallback.
static void TestDefaultsMatchOldBehaviour(void)
{
    const Settings s;
    CHECK(s.panelKey == (KeyBits::L | KeyBits::R));
    CHECK(s.touchKey == KeyBits::ZL);
    CHECK(s.mapKey == KeyBits::ZR);
    CHECK(s.cursorSource == CursorSource::CStick);
    CHECK(s.cursorSpeed == 6);
    CHECK(s.panelSize == PanelSize::Half);
    CHECK(s.panelCorner == PanelCorner::BottomRight);
    CHECK(!s.autoPanel);

    CHECK(ModeForScene(s, "TitleMenu", false) == SceneMode::Off);
    CHECK(ModeForScene(s, "TitleMenu", true) == SceneMode::Off);
    CHECK(ModeForScene(s, "FieldRo", false) == SceneMode::Off);
    CHECK(ModeForScene(s, "FieldRo", true) == SceneMode::Panel);
    CHECK(ModeForScene(s, "main:00647FCC", false) == SceneMode::Full);
    CHECK(ModeForScene(s, "main:00647FCC", true) == SceneMode::Full);
    CHECK(ModeForScene(s, "main:006488C8", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "Zukan", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "Battle", true) == SceneMode::Panel);
    CHECK(ModeForScene(s, "SomethingNew", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "main:00123456", true) == SceneMode::Panel);

    uint32_t up, down, left, right;
    CursorKeys(s.cursorSource, up, down, left, right);
    CHECK(up == KeyBits::CStickUp && down == KeyBits::CStickDown);
    CHECK(left == KeyBits::CStickLeft && right == KeyBits::CStickRight);
}

// Scenes mapped in P1-1 (docs/OFFSETS.md): full where the bottom screen is
// the whole scene, panel where the top screen still says something.
static void TestMappedSceneDefaults(void)
{
    const Settings s;
    CHECK(ModeForScene(s, "Status", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "Box", false) == SceneMode::Full);
    CHECK(ModeForScene(s, "TownMap", false) == SceneMode::Full);
    CHECK(ModeForScene(s, "TrainerPass", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "Config", false) == SceneMode::Full);
    CHECK(ModeForScene(s, "Kawaigari", false) == SceneMode::Full);
    CHECK(ModeForScene(s, "BattleVideoPlayer", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "box", true) == SceneMode::Full);
}

static void TestDefaultFileRoundTrip(void)
{
    std::vector<std::string> warnings;
    const Settings s = Parse(DefaultSettingsText(), warnings);
    CHECK(warnings.empty());
    for (const std::string &w : warnings)
        std::printf("  warning: %s\n", w.c_str());
    CHECK(SameSettings(s, Settings{}));
}

static void TestEmptyFile(void)
{
    std::vector<std::string> warnings;
    CHECK(SameSettings(Parse("", warnings), Settings{}));
    CHECK(warnings.empty());
    CHECK(SameSettings(Parse("\n\n   \r\n; only a comment\r\n", warnings), Settings{}));
    CHECK(warnings.empty());
}

static void TestTolerantParsing(void)
{
    std::vector<std::string> warnings;
    const Settings s = Parse(
        "\xEF\xBB\xBF; BOM and CRLF line endings\r\n"
        "  [ Controls ]  \r\n"
        "PANEL_KEY=l + r   ; inline comment\r\n"
        "\ttouch_key\t=\tY # another comment\r\n"
        "cursor_move = Circle-Pad\r\n"
        "cursor_speed = 12\r\n"
        "[panel]\r\n"
        "corner = Top_Left\r\n"
        "size = 2/3\r\n"
        "auto = OFF\r\n",
        warnings);
    CHECK(warnings.empty());
    for (const std::string &w : warnings)
        std::printf("  warning: %s\n", w.c_str());
    CHECK(s.panelKey == (KeyBits::L | KeyBits::R));
    CHECK(s.touchKey == KeyBits::Y);
    CHECK(s.cursorSource == CursorSource::CirclePad);
    CHECK(s.cursorSpeed == 12);
    CHECK(s.panelCorner == PanelCorner::TopLeft);
    CHECK(s.panelSize == PanelSize::TwoThirds);
    CHECK(!s.autoPanel);
}

static void TestKeysWithoutSection(void)
{
    std::vector<std::string> warnings;
    const Settings s = Parse("auto = no\npanel_key = Select\n", warnings);
    CHECK(warnings.empty());
    CHECK(!s.autoPanel);
    CHECK(s.panelKey == KeyBits::Select);
}

static void TestBadValuesKeepDefaults(void)
{
    std::vector<std::string> warnings;
    const Settings s = Parse(
        "[controls]\n"
        "panel_key = ZZ\n"            // 2: unknown key name
        "touch_key = L+\n"            // 3: dangling +
        "cursor_speed = 0\n"          // 4: out of range
        "cursor_speed = fast\n"       // 5: not a number
        "cursor_move = mouse\n"       // 6
        "colour = blue\n"             // 7: unknown key
        "just some words\n"           // 8: no '='
        "[panel]\n"
        "corner = middle\n"           // 10
        "size = 3/4\n"                // 11
        "auto = maybe\n"              // 12
        "[scenes]\n"
        "Battle = huge\n"             // 14
        "[extras]\n"                  // 15: unknown section
        "thing = 1\n"                 // skipped silently
        "[broken\n"                   // 17
        "= panel\n",                  // 18: empty key
        warnings);
    CHECK(SameSettings(s, Settings{}));
    CHECK(warnings.size() == 14);
    for (const std::string &w : warnings)
        std::printf("  expected warning: %s\n", w.c_str());
    CHECK(warnings.size() > 0 && warnings[0].compare(0, 8, "line 2: ") == 0);
    CHECK(warnings.size() > 6 && warnings[5].find("unknown key 'colour'") != std::string::npos);
    CHECK(warnings.size() > 11 && warnings[11].find("[extras]") != std::string::npos);
}

static void TestSceneOverrides(void)
{
    std::vector<std::string> warnings;
    const Settings s = Parse(
        "[scenes]\n"
        "battle = full\n"                  // case-insensitive match on an existing ID
        "FieldRo = panel\n"
        "FieldRo.xmenu = full\n"
        "main:00647fcc = panel\n"
        "main:00ABCDEF = off\n"            // a scene not in the defaults
        "default = off\n",
        warnings);
    CHECK(warnings.empty());
    CHECK(ModeForScene(s, "Battle", false) == SceneMode::Full);
    CHECK(ModeForScene(s, "FieldRo", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "FieldRo", true) == SceneMode::Full);
    CHECK(ModeForScene(s, "main:00647FCC", false) == SceneMode::Panel);
    CHECK(ModeForScene(s, "main:00ABCDEF", false) == SceneMode::Off);
    CHECK(ModeForScene(s, "Unmapped", false) == SceneMode::Off);
    CHECK(ModeForScene(s, "TitleMenu", false) == SceneMode::Off);
    CHECK(ModeForScene(s, "Zukan", false) == SceneMode::Panel);
    // Overriding an existing ID replaces it rather than adding a second one.
    CHECK(s.scenes.size() == Settings{}.scenes.size() + 1);
}

static void TestXMenuOnlyInField(void)
{
    // xmenuOpen only matters in FieldRo: the flag survives other scenes
    // (AutoPanel keeps it while Bag/Party are open).
    std::vector<std::string> warnings;
    const Settings s = Parse("[scenes]\nFieldRo.xmenu = full\n", warnings);
    CHECK(ModeForScene(s, "main:006488C8", true) == SceneMode::Panel);
    CHECK(ModeForScene(s, "FieldRo", true) == SceneMode::Full);
}

static void TestKeyNames(void)
{
    CHECK(KeysToString(KeyBits::ZR) == "ZR");
    CHECK(KeysToString(KeyBits::L | KeyBits::R) == "L+R");
    CHECK(KeysToString(0) == "none");

    std::vector<std::string> warnings;
    const Settings s = Parse("panel_key = none\ntouch_key = start+up\n", warnings);
    CHECK(warnings.empty());
    CHECK(s.panelKey == 0);
    CHECK(s.touchKey == (KeyBits::Start | KeyBits::DUp));
}

static void TestDescribe(void)
{
    const std::string d = DescribeSettings(Settings{});
    CHECK(d.find("panel_key L+R") != std::string::npos);
    CHECK(d.find("auto off") != std::string::npos);
    CHECK(d.find("corner bottom-right") != std::string::npos);
    CHECK(d.find("main:00647FCC=full") != std::string::npos);
    std::printf("  %s\n", d.c_str());
}

static void TestCurrentSettings(void)
{
    Settings s;
    s.autoPanel = true;
    SetSettings(s);
    CHECK(GetSettings().autoPanel);
    SetSettings(Settings{});
    CHECK(!GetSettings().autoPanel);
}

int main(void)
{
    TestDefaultsMatchOldBehaviour();
    TestMappedSceneDefaults();
    TestDefaultFileRoundTrip();
    TestEmptyFile();
    TestTolerantParsing();
    TestKeysWithoutSection();
    TestBadValuesKeepDefaults();
    TestSceneOverrides();
    TestXMenuOnlyInField();
    TestKeyNames();
    TestDescribe();
    TestCurrentSettings();

    std::printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
