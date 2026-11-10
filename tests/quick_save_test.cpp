#include "QuickSaveRules.hpp"
#include "Settings.hpp"
#include "SettingsFile.hpp"
#include <cstdio>
using namespace OneScreen;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void Rules()
{
    PcWorld w; w.scene = "FieldRo"; w.fieldState = 0;
    CHECK(SaveRefusal(w, false).empty());
    CHECK(SaveRefusal(w, true) == "already saving");
    w.pending = true; CHECK(SaveRefusal(w, false) == "the PC is opening"); w.pending = false;
    for (const char *scene : { "Battle", "Box", "", "TitleMenu", "JoinFesta" })
    { w.scene = scene; CHECK(!SaveRefusal(w, false).empty()); }
    w.scene = "FieldRo";
    for (int state : { -1, 4, 0x11, 0x1E, 1, 9 })
    { w.fieldState = state; CHECK(!SaveRefusal(w, false).empty()); }
    w.fieldState = 0;
    w.festivalPlaza = true; CHECK(SaveRefusal(w, false) == "in Festival Plaza"); w.festivalPlaza = false;
    w.eventRunning = true; CHECK(!SaveRefusal(w, false).empty()); w.eventRunning = false;
    w.actionRunning = true; CHECK(!SaveRefusal(w, false).empty());
    SaveProgress progress;
    progress.Observe(6, 0); CHECK(!progress.done);
    progress.Observe(5, 0); progress.Observe(7, 0); CHECK(!progress.done);
    progress.Observe(3, 255); CHECK(progress.asked);
    progress.Observe(6, 0); CHECK(!progress.done);
    progress.Observe(5, 0); CHECK(progress.writing);
    progress.Observe(8, 0); CHECK(!progress.done);
    progress.Observe(6, 0); CHECK(progress.done);
    progress.Observe(-1, -1); CHECK(progress.done);
    for (int state = -1; state < 256; ++state)
    {
        CHECK(SaveAsk(state, 255) == (state == 3));
        CHECK(!SaveAsk(state, 0)); CHECK(!SaveAsk(state, -1));
        CHECK(SaveDone(state) == (state == 6 || state == 7));
        CHECK(SaveError(state) == (state == 8 || state == 9));
    }
}
static void Keys()
{
    std::vector<std::string> warnings;
    CHECK(Settings().saveKey == 0);
    CHECK(ParseSettings(DefaultSettingsText(), warnings).saveKey == 0); CHECK(warnings.empty());
    CHECK(DefaultSettingsText().find("save_key = none\n") != std::string::npos);
    auto s = ParseSettings("[controls]\nsave_key = r + y\npc_key = R+X\n", warnings);
    CHECK(warnings.empty()); CHECK(s.saveKey == (KeyBits::R | KeyBits::Y));
    CHECK(s.pcKey == (KeyBits::R | KeyBits::X));
    const auto edited = SetIniValue("[controls]\r\npc_key = R+X\r\n; keep\r\n", "controls", "save_key", "R+Y");
    CHECK(ParseSettings(edited, warnings).saveKey == s.saveKey);
    CHECK(edited.find("pc_key = R+X\r\n") != std::string::npos);
    CHECK(edited.find("; keep\r\n") != std::string::npos);
    CHECK(ParseSettings("[controls]\nsave_key = R+INVALID\n", warnings).saveKey == 0);
    CHECK(warnings.size() == 1); warnings.clear();
    CHECK(ParseSettings("[controls]\nsave_key = R+Y\nsave_key = none\n", warnings).saveKey == 0);
    CHECK(warnings.empty());
    PcComboState combo;
    const auto keys = KeyBits::R | KeyBits::Y;
    CHECK(!PcComboRead(combo, keys, KeyBits::R));
    CHECK(PcComboRead(combo, keys, keys)); CHECK(combo.suppressed == keys);
    CHECK(!PcComboRead(combo, keys, keys));
    CHECK(!PcComboRead(combo, 0, KeyBits::R)); CHECK(combo.suppressed == KeyBits::R);
    CHECK(!PcComboRead(combo, 0, 0)); CHECK(combo.suppressed == 0);
}

// The fake accepts X/Y/A/B only on the correct screens. Delays let the
// runner exercise real waits, including a write slower than key hold/gap.
static void Sequence(bool offer, bool complete, bool changePrompt, bool menuRemains, bool rotomChat = false)
{
    ActionRunner runner; std::vector<std::string> log;
    ActionWorld world{ SceneField, WorldSaveIdle };
    CHECK(runner.Start(QuickSaveAction(), world, log));
    unsigned prev = 0, aCount = 0, xCount = 0, yCount = 0, bCount = 0;
    int saveAt = -1;
    bool changed = false;
    for (int frame = 0; frame < 2500 && runner.Running(); ++frame)
    {
        if (saveAt >= 0 && frame == saveAt && complete) world.flags = WorldSaveDone;
        if (saveAt >= 0 && frame == saveAt + 60 && complete)
            world.flags = WorldSaveDone | (menuRemains ? WorldXMenu : rotomChat ? 0 : WorldSaveIdle);
        // Change precisely between WaitFor and the guarded confirmation.
        if (changePrompt && (world.flags & WorldSaveAsk) && !changed && runner.Status().find("step 5/") != std::string::npos)
        {
            const auto out = runner.Update(world, log);
            CHECK(out.keys == 0); world.flags = 0; changed = true; continue;
        }
        const auto out = runner.Update(world, log);
        const auto pressed = out.keys & ~prev; prev = out.keys;
        if (pressed & ActionKeyX) { ++xCount; world.flags = WorldXMenu; }
        if (pressed & (1u << 11)) { ++yCount; if (offer) world.flags = WorldSaveAsk; }
        if (pressed & 1u) { ++aCount; CHECK(world.flags & WorldSaveAsk); world.flags = 0; saveAt = frame + 90; }
        if (pressed & ActionKeyB)
        {
            ++bCount; CHECK(world.flags & WorldXMenu);
            CHECK(frame >= saveAt + 90);
            world.flags = WorldSaveDone; // No idle-field acknowledgement required.
        }
    }
    CHECK(!runner.Running()); CHECK(xCount == 1); CHECK(yCount == 1);
    CHECK(aCount == (offer && !changePrompt ? 1u : 0u));
    CHECK(bCount == (offer && complete && !changePrompt && menuRemains ? 1u : 0u));
    CHECK(runner.Succeeded() == (offer && complete && !changePrompt));
    if (runner.Succeeded()) CHECK(world.flags == (WorldSaveDone | (!menuRemains && !rotomChat ? WorldSaveIdle : 0)));
}
int main()
{
    Rules(); Keys();
    CHECK(CheckAction(QuickSaveAction()).empty());
    for (const auto &step : QuickSaveAction().steps) CHECK(step.retries == 0 && !step.optional);
    Sequence(true, true, false, true);
    Sequence(false, false, false, true);
    Sequence(true, false, false, true);
    Sequence(true, true, true, true);
    Sequence(true, true, false, false);
    Sequence(true, true, false, false, true); // Rotom chat: no X menu or idle flag.
    ActionRunner r; std::vector<std::string> log;
    CHECK(!r.Start(QuickSaveAction(), { SceneField, 0 }, log));
    CHECK(r.Start(QuickSaveAction(), { SceneField, WorldSaveIdle }, log));
    r.Cancel(log); CHECK(!r.Succeeded());
    std::printf("quick_save_test: %s\n", failures ? "FAILED" : "passed"); return failures ? 1 : 0;
}
