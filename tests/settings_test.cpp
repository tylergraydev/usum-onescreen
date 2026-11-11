// Host tests for the settings parser and model (Sources/Settings.cpp).
// Build and run: make -C tests   (any C++17 compiler: g++, clang++, MSVC)

#include "Settings.hpp"
#include "SettingsFile.hpp"
#include "FastTextPatches.hpp"

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
    return a.panelKey == b.panelKey && a.touchKey == b.touchKey && a.mapKey == b.mapKey && a.pcKey == b.pcKey && a.saveKey == b.saveKey && a.photoKey == b.photoKey && a.cursorSource == b.cursorSource
        && a.cursorSpeed == b.cursorSpeed && a.panelSize == b.panelSize && a.panelCorner == b.panelCorner
        && a.instantText == b.instantText && a.battleMessages == b.battleMessages && a.battleBars == b.battleBars
        && a.filter == b.filter && a.pillarbox == b.pillarbox && a.autoPanel == b.autoPanel && a.aspect == b.aspect && a.fieldFov == b.fieldFov && a.battleLayout == b.battleLayout && a.scenes == b.scenes;
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

static void TestBattleLayout(void)
{
    std::vector<std::string> warnings;
    CHECK(Settings{}.battleLayout == BattleLayout::Classic);

    CHECK(Parse("[battle]\nlayout = classic\n", warnings).battleLayout == BattleLayout::Classic);
    CHECK(warnings.empty());
    CHECK(Parse("[battle]\nlayout = bar\n", warnings).battleLayout == BattleLayout::Bar);
    CHECK(warnings.empty());
    CHECK(Parse("[battle]\nlayout = rail\n", warnings).battleLayout == BattleLayout::Rail);
    CHECK(warnings.empty());

    // Case-insensitive section, key and value; spacing and comments as elsewhere.
    CHECK(Parse("[ Battle ]\r\n  LAYOUT\t=  RaIl  ; mine\r\n", warnings).battleLayout == BattleLayout::Rail);
    CHECK(warnings.empty());
    CHECK(Parse("[BATTLE]\nLayout = BAR\n", warnings).battleLayout == BattleLayout::Bar);
    CHECK(warnings.empty());
    // Later lines win, as for every key.
    CHECK(Parse("[battle]\nlayout = bar\nlayout = rail\n", warnings).battleLayout == BattleLayout::Rail);

    // A bad value warns and keeps the default (or the value set before it).
    Settings s = Parse("[battle]\nlayout = sideways\n", warnings);
    CHECK(s.battleLayout == BattleLayout::Classic);
    CHECK(warnings.size() == 1);
    CHECK(warnings.size() == 1 && warnings[0].find("line 2: bad layout 'sideways'") == 0);
    CHECK(warnings.size() == 1 && warnings[0].find("classic, bar or rail") != std::string::npos);
    s = Parse("[battle]\nlayout = bar\nlayout = 2\n", warnings);
    CHECK(s.battleLayout == BattleLayout::Bar);
    CHECK(warnings.size() == 1);

    // [battle] is a known section: no "unknown section" warning.
    Parse("[battle]\n", warnings);
    CHECK(warnings.empty());

    // The default file says classic, and DescribeSettings names the layout.
    CHECK(DefaultSettingsText().find("[battle]\n") != std::string::npos);
    CHECK(DefaultSettingsText().find("layout = classic\n") != std::string::npos);
    CHECK(DescribeSettings(Settings{}).find("battle layout classic") != std::string::npos);
    Settings rail;
    rail.battleLayout = BattleLayout::Rail;
    CHECK(DescribeSettings(rail).find("battle layout rail") != std::string::npos);
}

static void TestSpeedSettings(void)
{
    std::vector<std::string> warnings;
    const Settings defaults;
    CHECK(!defaults.instantText);
    CHECK(defaults.battleMessages == BattleSpeed::Normal);
    CHECK(defaults.battleBars == BattleSpeed::Normal);
    Settings s = Parse("[SpEeD]\ninstant_text = ON\nbattle_messages = FaSt\nbattle_bars = INSTANT\n", warnings);
    CHECK(warnings.empty());
    CHECK(s.instantText && s.battleMessages == BattleSpeed::Fast && s.battleBars == BattleSpeed::Instant);
    CHECK(DescribeSettings(s).find("instant text on, battle messages fast, battle bars instant") != std::string::npos);
    s = Parse("[speed]\ninstant_text = yes\nbattle_messages = 0\nbattle_bars = turbo\n", warnings);
    CHECK(warnings.size() == 3);
    CHECK(SameSettings(s, defaults));
    s = Parse("[speed]\ninstant_text = on\ninstant_text = bad\nbattle_messages = instant\n"
              "battle_messages = bad\nbattle_bars = fast\nbattle_bars = bad\n", warnings);
    CHECK(warnings.size() == 3);
    CHECK(s.instantText && s.battleMessages == BattleSpeed::Instant && s.battleBars == BattleSpeed::Fast);
    BattleSpeed speed = BattleSpeed::Fast;
    CHECK(!ParseBattleSpeed("bad", speed) && speed == BattleSpeed::Fast);
    for (const auto mode : {BattleSpeed::Normal, BattleSpeed::Fast, BattleSpeed::Instant})
    {
        CHECK(ParseBattleSpeed(BattleSpeedName(mode), speed) && speed == mode);
        std::string text = "; keep me\r\n[battle]\r\nlayout = rail\r\n";
        text = SetIniValue(text, "speed", "instant_text", "on");
        text = SetIniValue(text, "speed", "battle_messages", BattleSpeedName(mode));
        text = SetIniValue(text, "speed", "battle_bars", BattleSpeedName(mode));
        const auto saved = Parse(text, warnings);
        CHECK(warnings.empty());
        CHECK(saved.instantText && saved.battleMessages == mode && saved.battleBars == mode);
        CHECK(saved.battleLayout == BattleLayout::Rail);
        CHECK(text.find("; keep me\r\n") == 0);
    }
}

static void TestSpeedPatches(void)
{
    namespace P = FastTextPatches;
    static_assert(sizeof(P::TextStock) == 96, "text tables are 24 u32 words");
    CHECK(P::TextAddress + (P::TextStock.size() - 1) * 4 == 0x5D60B8);
    CHECK(P::BattleWords(BattleSpeed::Normal, BattleSpeed::Normal) == P::BattleStock);
    CHECK(P::MessageFrames(BattleSpeed::Fast) == 8);
    CHECK(P::MessageFrames(BattleSpeed::Instant) == 1);
    const auto fast = P::BattleWords(BattleSpeed::Fast, BattleSpeed::Fast);
    const auto instant = P::BattleWords(BattleSpeed::Instant, BattleSpeed::Instant);
    CHECK(fast[0] == 0xE3A02008 && fast[1] == 0xE3A02008 && fast[2] == 0xE3500008);
    CHECK(fast[3] == 0xB2800004 && fast[4] == 0xE2800008);
    CHECK(instant[0] == 0xE3A02001 && instant[1] == 0xE3A02001 && instant[2] == 0xE3500001);
    CHECK(instant[3] == 0xB1A00001 && instant[4] == 0xE2800064);
    CHECK(P::BattleWords(BattleSpeed::Normal, BattleSpeed::Instant)[0] == P::BattleStock[0]);
    CHECK(P::BattleWords(BattleSpeed::Fast, BattleSpeed::Normal)[3] == P::BattleStock[3]);
    for (unsigned i = 0; i < 24; ++i)
        CHECK(P::TextInstant[i] == ((i < 4 || (i >= 8 && i < 12)) ? 0u : 255u));

    // Any mismatching/unreadable word rejects the whole plan before writes.
    // Previously applied words are accepted only when they are our expected values.
    auto memory = P::BattleStock;
    uint32_t bad = 0;
    int reads = 0;
    bool readable = true;
    auto read = [&](uint32_t offset, uint32_t &value) {
        ++reads;
        for (unsigned i = 0; i < P::BattleOffsets.size(); ++i)
            if (offset == P::BattleOffsets[i]) { value = memory[i]; return readable; }
        return false;
    };
    CHECK(P::Preflight(P::BattleOffsets, P::BattleStock, read, bad));
    CHECK(reads == 5);
    for (unsigned i = 0; i < memory.size(); ++i)
    {
        memory = P::BattleStock;
        memory[i] ^= 1;
        CHECK(!P::Preflight(P::BattleOffsets, P::BattleStock, read, bad));
        CHECK(bad == P::BattleOffsets[i]);
    }
    memory = fast;
    CHECK(!P::Preflight(P::BattleOffsets, P::BattleStock, read, bad));
    CHECK(P::Preflight(P::BattleOffsets, fast, read, bad));
    readable = false;
    CHECK(!P::Preflight(P::BattleOffsets, fast, read, bad));
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

static void TestPillarbox(void)
{
    std::vector<std::string> warnings;
    CHECK(Settings{}.pillarbox == Pillarbox::Tint);
    for (const auto &entry : { std::make_pair("TiNt", Pillarbox::Tint),
                              std::make_pair("PATTERN", Pillarbox::Pattern),
                              std::make_pair("BlUr", Pillarbox::Blur),
                              std::make_pair("off", Pillarbox::Off) })
    {
        const Settings s = Parse(std::string("[display]\npillarbox = ") + entry.first, warnings);
        CHECK(warnings.empty());
        CHECK(s.pillarbox == entry.second);
        CHECK(DescribeSettings(s).find(std::string("pillarbox ") +
              (entry.second == Pillarbox::Tint ? "tint" : entry.second == Pillarbox::Pattern ? "pattern" :
               entry.second == Pillarbox::Blur ? "blur" : "off")) != std::string::npos);
    }
    CHECK(Parse("[display]\npillarbox = transparent", warnings).pillarbox == Pillarbox::Tint);
    CHECK(warnings.size() == 1);
    CHECK(Parse("[display]\npillarbox = blur\npillarbox = invalid", warnings).pillarbox == Pillarbox::Blur);
    CHECK(warnings.size() == 1);
    CHECK(Parse("[display]\npillarbox = pattern\npillarbox = invalid", warnings).pillarbox == Pillarbox::Pattern);
    CHECK(warnings.size() == 1);
}

static void TestGlyphs(void)
{
    std::vector<std::string> warnings;
    CHECK(Settings{}.glyphs == GlyphFamily::ThreeDS);
    const char *values[] = { "3ds", "xbox", "playstation", "nintendo", "auto" };
    const GlyphFamily families[] = { GlyphFamily::ThreeDS, GlyphFamily::Xbox,
        GlyphFamily::PlayStation, GlyphFamily::Nintendo, GlyphFamily::ThreeDS };
    for (unsigned i = 0; i < 5; ++i)
    {
        const Settings s = Parse(std::string("[controls]\nglyphs = ") + values[i], warnings);
        CHECK(warnings.empty());
        CHECK(s.glyphs == families[i]);
        CHECK(DescribeSettings(s).find(std::string("glyphs ") + GlyphFamilyName(families[i])) != std::string::npos);
        SetSettings(s);
        CHECK(GetSettings().glyphs == families[i]);
    }
    CHECK(Parse("[CONTROLS]\nGLYPHS = PlayStation", warnings).glyphs == GlyphFamily::PlayStation);
    CHECK(warnings.empty());
    CHECK(Parse("[controls]\nglyphs=xbox\nglyphs=bad", warnings).glyphs == GlyphFamily::ThreeDS);
    CHECK(warnings.size() == 1);
    CHECK(warnings[0].find("line 3: bad glyphs 'bad'") != std::string::npos);
    CHECK(Parse("[controls]\nglyphs=bad", warnings).glyphs == GlyphFamily::ThreeDS);
    CHECK(warnings.size() == 1);
    CHECK(Parse("[controls]\nglyphs=xbox\nglyphs=", warnings).glyphs == GlyphFamily::ThreeDS);
    CHECK(warnings.size() == 1);
    CHECK(Parse(DefaultSettingsText(), warnings).glyphs == GlyphFamily::ThreeDS);
    CHECK(warnings.empty());
    SetSettings(Settings{});
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
    TestBattleLayout();
    TestCurrentSettings();
    TestPillarbox();
    TestGlyphs();
    TestSpeedSettings();
    TestSpeedPatches();

    std::printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
