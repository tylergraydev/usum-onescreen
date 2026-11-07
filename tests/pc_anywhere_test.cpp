// Host tests for PC anywhere's pure part (Includes/PcAnywhereRules.hpp): when
// the boxes may open, and the pc_key combo the pad hook takes. Build and run:
// make -C tests.

#include "PcAnywhereRules.hpp"
#include "Settings.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace OneScreen;

static int s_failures = 0, s_checks = 0;

#define CHECK(cond) \
    do { s_checks++; if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static PcWorld Field(void)
{
    PcWorld w;
    w.scene = "FieldRo";
    w.fieldState = 0;
    return w;
}

static void TestRefusals(void)
{
    CHECK(PcRefusal(Field()).empty());

    PcWorld w = Field();
    w.scene = "Battle";
    CHECK(PcRefusal(w) == "in battle");
    w.scene = "Box";
    CHECK(PcRefusal(w) == "the boxes are already open");
    w.scene = "";
    CHECK(PcRefusal(w) == "loading");
    w.scene = "main:00647FCC";
    CHECK(PcRefusal(w) == "not in the field (scene main:00647FCC)");

    w = Field();
    w.fieldState = PcFieldMenu;
    CHECK(PcRefusal(w) == "a menu is open");
    w.fieldState = PcFieldTalk;
    CHECK(PcRefusal(w) == "talking");
    w.fieldState = PcFieldEncounter;
    CHECK(PcRefusal(w) == "encounter starting");
    w.fieldState = -1;
    CHECK(PcRefusal(w) == "field state unknown");
    w.fieldState = 7;
    CHECK(PcRefusal(w) == "field busy (state 7)");

    w = Field();
    w.eventRunning = true;
    CHECK(PcRefusal(w) == "a script or event is running");
    w = Field();
    w.festivalPlaza = true;
    CHECK(PcRefusal(w) == "in Festival Plaza");
    w = Field();
    w.actionRunning = true;
    CHECK(PcRefusal(w) == "a plugin action is running");
    w = Field();
    w.pending = true;
    CHECK(PcRefusal(w) == "already opening");

    // A pending request wins over everything else, battle over the field checks.
    w.scene = "Battle";
    CHECK(PcRefusal(w) == "already opening");
    w.pending = false;
    w.eventRunning = true;
    CHECK(PcRefusal(w) == "in battle");
}

static void TestCombo(void)
{
    const uint32_t R = KeyBits::R, X = KeyBits::X, A = KeyBits::A;
    PcComboState s;

    // Off: never fires, hides nothing.
    CHECK(!PcComboRead(s, 0, R | X));
    CHECK(s.suppressed == 0);

    // R held first: nothing yet, R not hidden.
    CHECK(!PcComboRead(s, R | X, R));
    CHECK(s.suppressed == 0);
    // X joins: fires once, both hidden.
    CHECK(PcComboRead(s, R | X, R | X));
    CHECK(s.suppressed == (R | X));
    // Several reads a frame and held frames: no second fire.
    CHECK(!PcComboRead(s, R | X, R | X));
    CHECK(!PcComboRead(s, R | X, R | X | A));
    CHECK(s.suppressed == (R | X));   // A was never part of it
    // X released: X no longer hidden, R still is until released.
    CHECK(!PcComboRead(s, R | X, R));
    CHECK(s.suppressed == R);
    // X again while R is still held: fires again (a new press of the combo).
    CHECK(PcComboRead(s, R | X, R | X));
    CHECK(!PcComboRead(s, R | X, 0));
    CHECK(s.suppressed == 0);

    // Combo turned off while its keys are hidden: they stay hidden until released.
    PcComboState t;
    CHECK(PcComboRead(t, R | X, R | X));
    CHECK(!PcComboRead(t, 0, R | X));
    CHECK(t.suppressed == (R | X));
    CHECK(!PcComboRead(t, 0, X));
    CHECK(t.suppressed == X);
    CHECK(!PcComboRead(t, 0, 0));
    CHECK(t.suppressed == 0);

    // A single-key combo works too.
    PcComboState u;
    CHECK(PcComboRead(u, KeyBits::Y, KeyBits::Y));
    CHECK(!PcComboRead(u, KeyBits::Y, KeyBits::Y));
}

static void TestSetting(void)
{
    std::vector<std::string> warnings;
    CHECK(Settings().pcKey == 0);
    CHECK(ParseSettings(DefaultSettingsText(), warnings).pcKey == 0);
    CHECK(warnings.empty());
    CHECK(DefaultSettingsText().find("pc_key = none\n") != std::string::npos);

    warnings.clear();
    Settings s = ParseSettings("[controls]\npc_key = R+X\n", warnings);
    CHECK(warnings.empty());
    CHECK(s.pcKey == (KeyBits::R | KeyBits::X));
    CHECK(DescribeSettings(s).find("pc_key X+R") != std::string::npos);

    warnings.clear();
    s = ParseSettings("[controls]\npc_key = select + x\n", warnings);
    CHECK(warnings.empty());
    CHECK(s.pcKey == (KeyBits::Select | KeyBits::X));

    warnings.clear();
    s = ParseSettings("[controls]\npc_key = R+Q\n", warnings);
    CHECK(warnings.size() == 1);
    CHECK(s.pcKey == 0);

    warnings.clear();
    s = ParseSettings("[controls]\npc_key = L+Y\npc_key = none\n", warnings);
    CHECK(warnings.empty());
    CHECK(s.pcKey == 0);
}

int main(void)
{
    TestRefusals();
    TestCombo();
    TestSetting();
    if (s_failures)
    {
        std::printf("pc_anywhere_test: %d of %d checks FAILED\n", s_failures, s_checks);
        return 1;
    }
    std::printf("pc_anywhere_test: all %d checks passed\n", s_checks);
    return 0;
}
