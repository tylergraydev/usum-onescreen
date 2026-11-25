// Host tests for the ini write-back helpers (Sources/SettingsFile.cpp).
// Build and run: make -C tests

#include "Settings.hpp"
#include "SettingsFile.hpp"

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

// Expected-vs-got check that prints both on failure.
#define CHECK_TEXT(got, want)                                                   \
    do                                                                          \
    {                                                                           \
        const std::string g_ = (got), w_ = (want);                              \
        s_checks++;                                                             \
        if (g_ != w_)                                                           \
        {                                                                       \
            s_failures++;                                                       \
            std::printf("FAIL %s:%d:\n--- want\n%s\n--- got\n%s\n---\n",        \
                        __FILE__, __LINE__, w_.c_str(), g_.c_str());            \
        }                                                                       \
    } while (0)

static std::string Set(const std::string &text, const std::string &value)
{
    return SetIniValue(text, "battle", "layout", value);
}

static void TestReplaceExisting(void)
{
    CHECK_TEXT(Set("[battle]\nlayout = classic\n", "bar"), "[battle]\nlayout = bar\n");
    // Other lines and sections untouched.
    CHECK_TEXT(Set("; top\n[display]\naspect = 0\n\n[battle]\n; look\nlayout = classic\n\n[controls]\nmap_key = ZR\n", "rail"),
               "; top\n[display]\naspect = 0\n\n[battle]\n; look\nlayout = rail\n\n[controls]\nmap_key = ZR\n");
    // Any spacing, case-insensitive key; indentation and key spelling kept.
    CHECK_TEXT(Set("[battle]\n\t  LayOut=classic\n", "bar"), "[battle]\n\t  LayOut = bar\n");
    CHECK_TEXT(Set("[battle]\nlayout\t \t=\t\tclassic   \n", "bar"), "[battle]\nlayout = bar\n");
    // Trailing comment kept, with its spacing; '#' too.
    CHECK_TEXT(Set("[battle]\nlayout = classic   ; my pick\n", "rail"), "[battle]\nlayout = rail   ; my pick\n");
    CHECK_TEXT(Set("[battle]\nlayout = classic# x\n", "rail"), "[battle]\nlayout = rail # x\n");
    // An empty value (a parse warning today) is still the key's line.
    CHECK_TEXT(Set("[battle]\nlayout =\n", "bar"), "[battle]\nlayout = bar\n");
    // Only the first one in the section changes.
    CHECK_TEXT(Set("[battle]\nlayout = classic\nlayout = rail\n", "bar"), "[battle]\nlayout = bar\nlayout = rail\n");
    // Case-insensitive section header with spacing and a comment.
    CHECK_TEXT(Set("[ Battle ] ; menus\nlayout = classic\n", "bar"), "[ Battle ] ; menus\nlayout = bar\n");
    // The same key in another section is not this one.
    CHECK_TEXT(Set("[panel]\nlayout = x\n[battle]\nlayout = classic\n", "bar"), "[panel]\nlayout = x\n[battle]\nlayout = bar\n");
    // Similar keys and commented-out keys are not the key.
    CHECK_TEXT(Set("[battle]\n; layout = rail\nlayouts = 1\nlayout = classic\n", "bar"),
               "[battle]\n; layout = rail\nlayouts = 1\nlayout = bar\n");
    // A key in a second [battle] block is found.
    CHECK_TEXT(Set("[battle]\nfoo = 1\n[panel]\nsize = 1/2\n[battle]\nlayout = classic\n", "rail"),
               "[battle]\nfoo = 1\n[panel]\nsize = 1/2\n[battle]\nlayout = rail\n");
    // Last line without a line ending stays without one.
    CHECK_TEXT(Set("[battle]\nlayout = classic", "bar"), "[battle]\nlayout = bar");
}

static void TestLineEndings(void)
{
    CHECK_TEXT(Set("[battle]\r\nlayout = classic\r\n[panel]\r\nsize = 1/2\r\n", "bar"),
               "[battle]\r\nlayout = bar\r\n[panel]\r\nsize = 1/2\r\n");
    // Missing key and missing section in a CRLF file get CRLF lines.
    CHECK_TEXT(Set("[battle]\r\nfoo = 1\r\n", "rail"), "[battle]\r\nfoo = 1\r\nlayout = rail\r\n");
    CHECK_TEXT(Set("[panel]\r\nsize = 1/2\r\n", "rail"), "[panel]\r\nsize = 1/2\r\n\r\n[battle]\r\nlayout = rail\r\n");
    // LF file: LF lines.
    CHECK_TEXT(Set("[panel]\nsize = 1/2\n", "rail"), "[panel]\nsize = 1/2\n\n[battle]\nlayout = rail\n");
    // Mixed: each line keeps its own ending; new ones use CRLF.
    CHECK_TEXT(Set("[battle]\nfoo = 1\r\nbar = 2\n", "rail"), "[battle]\nfoo = 1\r\nbar = 2\nlayout = rail\r\n");
    // UTF-8 BOM kept.
    CHECK_TEXT(Set("\xEF\xBB\xBF[battle]\r\nlayout = bar\r\n", "rail"), "\xEF\xBB\xBF[battle]\r\nlayout = rail\r\n");
    CHECK_TEXT(Set("\xEF\xBB\xBF[panel]\nsize = 1/2\n", "bar"), "\xEF\xBB\xBF[panel]\nsize = 1/2\n\n[battle]\nlayout = bar\n");
}

static void TestMissingKey(void)
{
    // After the last key line of the section, before blank lines and the
    // next section's comments.
    CHECK_TEXT(Set("[battle]\nfoo = 1\n\n; about panel\n[panel]\nsize = 1/2\n", "bar"),
               "[battle]\nfoo = 1\nlayout = bar\n\n; about panel\n[panel]\nsize = 1/2\n");
    // Empty section: right after the header.
    CHECK_TEXT(Set("[battle]\n\n[panel]\nsize = 1/2\n", "bar"), "[battle]\nlayout = bar\n\n[panel]\nsize = 1/2\n");
    CHECK_TEXT(Set("[battle]\n", "bar"), "[battle]\nlayout = bar\n");
    // Header is the last line, no line ending.
    CHECK_TEXT(Set("[battle]", "bar"), "[battle]\nlayout = bar\n");
    CHECK_TEXT(Set("[battle]\nfoo = 1", "bar"), "[battle]\nfoo = 1\nlayout = bar\n");
    // Two [battle] blocks, neither has it: the first one gets it.
    CHECK_TEXT(Set("[battle]\nfoo = 1\n[panel]\nsize = 1/2\n[battle]\nbaz = 2\n", "rail"),
               "[battle]\nfoo = 1\nlayout = rail\n[panel]\nsize = 1/2\n[battle]\nbaz = 2\n");
}

static void TestMissingSection(void)
{
    CHECK_TEXT(Set("", "bar"), "[battle]\nlayout = bar\n");
    CHECK_TEXT(Set("[panel]\nsize = 1/2", "bar"), "[panel]\nsize = 1/2\n\n[battle]\nlayout = bar\n");
    // Already ends with a blank line: no second one.
    CHECK_TEXT(Set("[panel]\nsize = 1/2\n\n", "bar"), "[panel]\nsize = 1/2\n\n[battle]\nlayout = bar\n");
    // A broken header is not the section.
    CHECK_TEXT(Set("[battle\nlayout = classic\n", "bar"), "[battle\nlayout = classic\n\n[battle]\nlayout = bar\n");
    // Section name given with odd case and spacing is written trimmed.
    CHECK_TEXT(SetIniValue("", " Battle ", " layout ", "rail"), "[Battle]\nlayout = rail\n");
}

static void TestTopLevelSection(void)
{
    // Empty section name: lines before any header.
    CHECK_TEXT(SetIniValue("auto = on\n[panel]\nauto = off\n", "", "auto", "off"), "auto = off\n[panel]\nauto = off\n");
    CHECK_TEXT(SetIniValue("[panel]\nauto = off\n", "", "auto", "on"), "auto = on\n[panel]\nauto = off\n");
    CHECK_TEXT(SetIniValue("\xEF\xBB\xBF[panel]\n", "", "auto", "on"), "\xEF\xBB\xBF" "auto = on\n[panel]\n");
}

// The default file, changed, still parses with no warnings to the new value,
// and changes only that one line.
static void TestDefaultFile(void)
{
    const std::string def = DefaultSettingsText();
    const BattleLayout all[] = { BattleLayout::Classic, BattleLayout::Bar, BattleLayout::Rail };
    for (BattleLayout layout : all)
    {
        const std::string text = Set(def, BattleLayoutName(layout));
        std::vector<std::string> warnings;
        const Settings s = ParseSettings(text, warnings);
        CHECK(warnings.empty());
        CHECK(s.battleLayout == layout);
        CHECK(text.size() + std::string("classic").size() == def.size() + std::string(BattleLayoutName(layout)).size());
        CHECK(text.find(std::string("layout = ") + BattleLayoutName(layout) + "\n") != std::string::npos);
    }
    // Setting the same value twice is stable.
    CHECK_TEXT(Set(Set(def, "rail"), "rail"), Set(def, "rail"));
    CHECK_TEXT(Set(Set(def, "rail"), "classic"), def);

    // A file from before [battle] existed (no section) gains it and parses.
    std::string old = def;
    const size_t at = old.find("[battle]\n");
    const size_t end = old.find("\n\n", at);
    old.erase(at, end + 2 - at);
    CHECK(old.find("[battle]") == std::string::npos);
    std::vector<std::string> warnings;
    const Settings s = ParseSettings(Set(old, "bar"), warnings);
    CHECK(warnings.empty());
    CHECK(s.battleLayout == BattleLayout::Bar);
}

static void TestLayoutNames(void)
{
    BattleLayout l = BattleLayout::Classic;
    CHECK(ParseBattleLayout("Bar", l) && l == BattleLayout::Bar);
    CHECK(ParseBattleLayout(" RAIL ", l) && l == BattleLayout::Rail);
    CHECK(ParseBattleLayout("classic", l) && l == BattleLayout::Classic);
    CHECK(!ParseBattleLayout("", l) && l == BattleLayout::Classic);
    CHECK(!ParseBattleLayout("bars", l) && l == BattleLayout::Classic);
    CHECK(std::string(BattleLayoutName(BattleLayout::Classic)) == "classic");
    CHECK(std::string(BattleLayoutName(BattleLayout::Bar)) == "bar");
    CHECK(std::string(BattleLayoutName(BattleLayout::Rail)) == "rail");
    CHECK(std::string(BattleLayoutTitle(BattleLayout::Rail)) == "Rail");
    CHECK(NextBattleLayout(BattleLayout::Classic) == BattleLayout::Bar);
    CHECK(NextBattleLayout(BattleLayout::Bar) == BattleLayout::Rail);
    CHECK(NextBattleLayout(BattleLayout::Rail) == BattleLayout::Classic);
}

static void TestDebugDisabled(void)
{
    std::vector<std::string> warnings;
    const auto bit = [](DebugSubsystem s) { return 1ull << (unsigned)s; };
    CHECK(ParseDebugDisabled("", warnings, "test: ") == 0);
    CHECK(ParseDebugDisabled("  AcTiOnS, panel,actions,, ", warnings, "test: ")
          == (bit(DebugSubsystem::Actions) | bit(DebugSubsystem::Panel)));
    CHECK(warnings.empty());
    const uint64_t all = (1ull << (unsigned)DebugSubsystem::Count) - 1;
    CHECK(ParseDebugDisabled("all", warnings, "test: ") == all);
    CHECK(ParseDebugDisabled(DescribeDebugDisabled(all), warnings, "test: ") == all);
    CHECK(ParseDebugDisabled("gpuquad", warnings, "test: ") == bit(DebugSubsystem::Gpuquad));
    CHECK(ParseDebugDisabled("no-such-name,actions", warnings, "line 2: ") == bit(DebugSubsystem::Actions));
    CHECK(warnings.size() == 1 && warnings[0].find("line 2: unknown debug subsystem") == 0);
    warnings.clear();
    CHECK(ParseSettings("[DEBUG]\nDISABLE =\n", warnings).debugDisabled == 0);
    CHECK(warnings.empty());
    const Settings disabled = ParseSettings("[debug]\ndisable=ALL\n", warnings);
    CHECK(disabled.debugDisabled == all && warnings.empty());
    SetDebugDisabled(disabled.debugDisabled);
    for (unsigned i = 0; i < (unsigned)DebugSubsystem::Count; ++i)
        CHECK(!DebugEnabled((DebugSubsystem)i));
    SetSettings(ParseSettings("[debug]\ndisable=\n", warnings));
    CHECK(!DebugEnabled(DebugSubsystem::Actions)); // reload cannot change boot switches
    SetDebugDisabled(bit(DebugSubsystem::Gpuquad));
    CHECK(!DebugEnabled(DebugSubsystem::Gpuquad) && DebugEnabled(DebugSubsystem::Actions));
    SetDebugDisabled(0);
    for (unsigned i = 0; i < (unsigned)DebugSubsystem::Count; ++i)
        CHECK(DebugEnabled((DebugSubsystem)i));
    warnings.clear();
    ParseSettings("[debug]\nunknown=on\n", warnings);
    CHECK(warnings.size() == 1 && warnings[0].find("unknown key") != std::string::npos);
}

int main(void)
{
    TestDebugDisabled();
    TestReplaceExisting();
    TestLineEndings();
    TestMissingKey();
    TestMissingSection();
    TestTopLevelSection();
    TestDefaultFile();
    TestLayoutNames();

    std::printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
