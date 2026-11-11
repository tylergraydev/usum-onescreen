#include "Screens/Pelago.hpp"
#include "hires_screen_smoke.hpp"
#include "pelago_samples.hpp"
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>
using namespace OneScreen;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static std::vector<Sample> Isle()
{
    return {std::begin(PelagoCaptures::IsleBottom), std::end(PelagoCaptures::IsleBottom)};
}

static void Feed(PelagoScreen &screen, const std::vector<Sample> &s, std::vector<std::string> &log)
{
    for (int i = 0; i < 3; ++i) screen.Sampled(s.data(), log);
}
static void Captures()
{
    using namespace PelagoCaptures;
    const SamplePoint bottomPoints[] = {{16,117},{303,117},{16,120},{303,120},{40,90},{280,90},
                                      {40,150},{280,150},{40,170},{280,170},{20,113},{299,113}};
    const SamplePoint topPoints[] = {{48,174},{140,174},{40,210},{360,210}};
    CHECK(PelagoSamplePoints().size() == std::size(bottomPoints));
    CHECK(std::size(bottomPoints) == PelagoBottomSampleCount);
    CHECK(PelagoMessagePoints().size() == std::size(topPoints));
    for (size_t i = 0; i < std::size(bottomPoints); ++i)
        CHECK(PelagoSamplePoints()[i].x == bottomPoints[i].x && PelagoSamplePoints()[i].y == bottomPoints[i].y);
    for (size_t i = 0; i < std::size(topPoints); ++i)
    {
        CHECK(PelagoMessagePoints()[i].x == topPoints[i].x && PelagoMessagePoints()[i].y == topPoints[i].y);
        CHECK(topPoints[i].y < PelagoArea.y);
    }
    CHECK(DecodePelago(IsleBottom) == PelagoView::Isle);
    CHECK(DecodePelago(AfterNoBottom) == PelagoView::Isle);
    for (const Sample *s : AllIsleBottoms)
    {
        CHECK(DecodePelago(s) == PelagoView::Isle);
        CHECK(!PelagoClear(s));
        std::vector<std::string> log; PelagoScreen screen; screen.Begin(log);
        Feed(screen, {s, s + PelagoBottomSampleCount}, log);
        CHECK(screen.Ready()); CHECK((screen.CaptureKeys() & ScreenKey::B) != 0);
        CHECK(screen.Key(ScreenKey::B, log) == &PelagoLeaveAction());
    }
    // Arbitrary isle content outside the glyphs never hides the bar.
    auto scenery = Isle();
    for (int i = 4; i < 10; ++i) scenery[i] = {0,0,0};
    CHECK(DecodePelago(scenery.data()) == PelagoView::Isle);
    CHECK(DecodePelago(AskBottom) == PelagoView::Ask);
    CHECK(DecodePelago(XMenuBottom) == PelagoView::Other);
    CHECK(PelagoMessage(AskTop));
    for (const Sample *s : {IsleTop, AfterNoTop, XMenuTop}) CHECK(!PelagoMessage(s));
    for (const Sample *s : {IsleBottom, AfterNoBottom, AskBottom, XMenuBottom}) CHECK(!PelagoClear(s));
    std::vector<Sample> black(PelagoBottomSampleCount, {0,0,0}), white(PelagoBottomSampleCount, {255,255,255});
    CHECK(DecodePelago(black.data()) == PelagoView::Other);
    CHECK(DecodePelago(white.data()) == PelagoView::Other);
    CHECK(!PelagoMessage(black.data())); CHECK(!PelagoMessage(white.data()));
    CHECK(!PelagoClear(black.data())); CHECK(!PelagoClear(white.data()));
    CHECK(DecodePelago(AfterYesBottom) == PelagoView::Other);
    CHECK(PelagoClear(AfterYesBottom)); CHECK(PelagoMessage(AfterYesTop));
    CHECK(PelagoBottomFlags(AfterYesBottom) == WorldPelagoClear);
    std::vector<Sample> clear(std::begin(AfterYesBottom), std::end(AfterYesBottom));
    std::vector<Sample> beige(PelagoBottomSampleCount, {247,231,198});
    CHECK(!PelagoClear(beige.data()));
    // Each independently required point rejects a mismatched surface.
    for (int i = 0; i < 4; ++i)
    {
        std::vector<Sample> message(std::begin(AskTop), std::end(AskTop));
        message[i] = IsleTop[i]; CHECK(!PelagoMessage(message.data()));
    }
    for (u32 i = 0; i < PelagoBottomSampleCount; ++i)
    {
        std::vector<Sample> ask(std::begin(AskBottom), std::end(AskBottom));
        ask[i] = {0,0,0}; CHECK(DecodePelago(ask.data()) != PelagoView::Ask);
        if (i < 4 || i >= 10)
        { auto s = clear; s[i] = AskBottom[4]; CHECK(!PelagoClear(s.data())); }
    }
    // Every glyph point is required, with inclusive +/-24 RGB tolerance.
    for (int i : {0,1,2,3,10,11})
    {
        auto s = Isle(); s[i] = {247,231,198};
        CHECK(DecodePelago(s.data()) != PelagoView::Isle);
        const Sample target = IsleBottom[i];
        for (int delta : {-24,24})
        {
            s = Isle();
            s[i] = {(u8)(target.r+delta), (u8)(target.g+delta), (u8)(delta < 0 ? target.b+delta : 255)};
            CHECK(DecodePelago(s.data()) == PelagoView::Isle);
        }
        for (int delta : {-25,25})
        {
            s = Isle(); s[i].r = (u8)(target.r+delta);
            CHECK(DecodePelago(s.data()) != PelagoView::Isle);
        }
    }
    std::vector<Sample> tinted(std::begin(AskBottom), std::end(AskBottom));
    for (int i : {0,1,2,3,8,9,10,11}) tinted[i] = {227,211,178};
    for (int i : {4,5,6,7}) tinted[i] = {200,235,235};
    CHECK(DecodePelago(tinted.data()) == PelagoView::Ask);
    tinted[4].r = 199; CHECK(DecodePelago(tinted.data()) == PelagoView::Other);
    for (int i = 0; i < 4; ++i)
        for (int delta : {-24, 24})
        {
            std::vector<Sample> message(std::begin(AskTop), std::end(AskTop));
            const int red = (int)message[i].r + delta;
            message[i].r = (u8)(red < 0 ? 0 : red > 255 ? 255 : red);
            CHECK(PelagoMessage(message.data()));
        }
    for (int i : {0,1,2,3,10,11})
    {
        for (int delta : {-20,20})
        { auto s = clear; s[i].b = (u8)(clear[i].b+delta); CHECK(PelagoClear(s.data())); }
        for (int delta : {-21,21})
        { auto s = clear; s[i].b = (u8)(clear[i].b+delta); CHECK(!PelagoClear(s.data())); }
    }

}
static void ProbeHold()
{
    PelagoBottomProbe probe;
    CHECK(probe.Update(WorldPelagoAsk) == 0);
    CHECK(probe.Update(0) == 0);
    CHECK(probe.Update(WorldPelagoAsk) == 0);
    CHECK(probe.Update(0) == 0);
    CHECK(probe.Update(WorldPelagoAsk) == WorldPelagoAsk);
    for (u32 i = 0; i < PelagoBottomProbe::AskHoldSamples; ++i)
        CHECK(probe.Update(0) == WorldPelagoAsk);
    CHECK(probe.Update(0) == 0); // expires, no permanently latched Yes
    CHECK(probe.Update(WorldPelagoAsk) == 0); // rearm needs fresh matches
    for (u32 next : {WorldPelagoIsle, WorldPelagoClear})
    {
        for (int i = 0; i < 3; ++i) probe.Update(WorldPelagoAsk);
        CHECK(probe.Update(next) == 0); // Ask cleared even before next is stable
        CHECK(probe.Update(next) == 0);
        CHECK(probe.Update(next) == next);
    }
}
static void Keys()
{
    std::vector<std::string> log;
    CHECK(IsPelago(PelagoScene, true));
    CHECK(!IsPelago(PelagoScene, false)); CHECK(!IsPelago("FieldRo", true));
    PelagoScreen screen; screen.Begin(log);
    CHECK(!screen.Ready()); CHECK(screen.CaptureKeys() == 0);
    const auto isle = Isle();
    screen.Sampled(isle.data(), log); screen.Sampled(isle.data(), log);
    CHECK(!screen.Ready()); screen.Sampled(isle.data(), log); CHECK(screen.Ready());
    CHECK(screen.CaptureKeys() == (ScreenKey::L | ScreenKey::R | ScreenKey::Start | ScreenKey::B));
    for (u32 k : {ScreenKey::A, ScreenKey::DPad, ScreenKey::CPad, ScreenKey::L | ScreenKey::R})
        CHECK(screen.Key(k, log) == nullptr);
    const u32 keys[] = {ScreenKey::L, ScreenKey::R, ScreenKey::Start, ScreenKey::B};
    const char *buttons[] = {"pelago.previous", "pelago.next", "pelago.mohn", "pelago.back"};
    const int xy[][2] = {{16,120}, {303,120}, {300,222}, {16,223}};
    for (int i = 0; i < 4; ++i)
    {
        const ActionDef *def = screen.Key(keys[i], log);
        CHECK(def != nullptr); if (!def) continue;
        CHECK(CheckAction(*def).empty());
        CHECK(std::strcmp(def->steps[0].button, buttons[i]) == 0);
        const auto *b = FindLayoutButton(def->steps[0].button);
        CHECK(b && b->tapX == xy[i][0] && b->tapY == xy[i][1]);
        CHECK(screen.Key(keys[i], log) == nullptr); // busy cannot enqueue
        screen.ActionDone(log); CHECK(!screen.Ready()); Feed(screen, isle, log);
    }
    std::vector<Sample> ask(std::begin(PelagoCaptures::AskBottom), std::end(PelagoCaptures::AskBottom));
    CHECK(DecodePelago(ask.data()) == PelagoView::Ask);
    screen.Sampled(ask.data(), log); CHECK(!screen.Ready()); CHECK(screen.CaptureKeys() == 0);
    CHECK(screen.Key(ScreenKey::B, log) == nullptr);
    ask[8] = ask[9] = {220,255,255}; // a third menu pill is not Yes/No
    CHECK(DecodePelago(ask.data()) == PelagoView::Other);
    CHECK(!PelagoClear(ask.data()));
    Feed(screen, {std::begin(PelagoCaptures::AfterNoBottom), std::end(PelagoCaptures::AfterNoBottom)}, log);
    CHECK(screen.Ready());

}
// Run the real action runner against a scripted game; count input edges,
// rather than equating six held input frames with six separate taps.
enum class Scenario { Good, FlickeringAsk, NoAsk, NoMessage, StuckYes, LostScene, ChangedPrompt, SlowExit };
static void Sequence(Scenario scenario, bool close = false)
{
    ActionWorld w; w.scene = PelagoScene; w.flags = WorldPelagoIsle;
    std::vector<std::string> log;
    ActionRunner runner;
    const auto &def = PelagoLeaveAction(close);
    CHECK(CheckAction(def).empty());
    CHECK(runner.Start(def, w, log));
    int back = 0, yes = 0, a = 0, b = 0, total = 0;
    bool touching = false; u32 keys = 0;
    PelagoBottomProbe probe;
    const Sample *bottom = PelagoCaptures::IsleBottom;
    bool message = false;
    for (int i = 0; i < 3; ++i) probe.Update(WorldPelagoIsle);
    std::vector<std::string> edges;
    for (; runner.Running() && total < 2600; ++total)
    {
        if (w.scene == PelagoScene)
        {
            u32 flags = PelagoBottomFlags(bottom);
            if (scenario == Scenario::FlickeringAsk && bottom == PelagoCaptures::AskBottom && total % 2)
                flags = 0; // observed single-frame misses of the Ask decoder
            w.flags = probe.Update(flags) | (message ? WorldPelagoMessage : 0);
        }
        const auto out = runner.Update(w, log);
        if (out.touching && !touching)
        {
            if (out.x == 16 && out.y == 223) { ++back; edges.push_back("back"); }
            else if (out.x == 160 && out.y == 92) { ++yes; edges.push_back("yes"); }
            else CHECK(false);
        }
        if (out.keys && !keys)
        {
            if (out.keys == ScreenKey::A) { ++a; edges.push_back("A"); }
            else if (out.keys == ScreenKey::B) { ++b; edges.push_back("B"); }
            else CHECK(false);
        }
        if (!out.touching && touching)
        {
            if (yes)
            {
                if (scenario != Scenario::StuckYes) bottom = PelagoCaptures::AfterYesBottom;
            }
            else if (scenario != Scenario::NoAsk)
            { bottom = PelagoCaptures::AskBottom; message = scenario != Scenario::NoMessage; }
        }
        // Change the scene / prompt after WaitFor passed, before guarded Yes.
        if (total == 18 && scenario == Scenario::LostScene) { w.scene = "FieldRo"; w.flags = 0; }
        if (total == 18 && scenario == Scenario::ChangedPrompt) bottom = PelagoCaptures::IsleBottom;
        if (!out.keys && keys == ScreenKey::A && scenario != Scenario::SlowExit)
        { w.scene = SceneField; w.flags = WorldXMenu; }
        if (out.keys == ScreenKey::B && w.scene == SceneField) w.flags = 0;
        touching = out.touching; keys = out.keys;
    }
    CHECK(!runner.Running()); CHECK(back == 1); CHECK(yes <= 1); CHECK(a <= 1);
    const bool good = scenario == Scenario::Good || scenario == Scenario::FlickeringAsk;
    CHECK(runner.Succeeded() == good);
    if (good)
    {
        CHECK(yes == 1 && a == 1 && b == (close ? 1 : 0));
        CHECK(edges.size() == (close ? 4u : 3u));
        CHECK(edges[0] == "back" && edges[1] == "yes" && edges[2] == "A");
        CHECK(!close || w.flags == 0);
    }
    if (scenario == Scenario::NoAsk || scenario == Scenario::NoMessage || scenario == Scenario::LostScene || scenario == Scenario::ChangedPrompt)
        CHECK(yes == 0 && a == 0);
    if (scenario == Scenario::StuckYes) CHECK(yes == 1 && a == 0);
}
static void Preview(float scale, const char *dir)
{
    PelagoScreen screen;
    const int width = (int)(400 * scale), height = (int)(240 * scale);
    std::vector<u8> pixels((size_t)width * height * 3, 130);
    UI::Surface surface{ pixels.data(), 3, width * 3, UI::PixelFormat::BGR8, width, height };
    UI::Canvas canvas(surface, 0, 0, scale); screen.Draw(canvas);
    // Everything above the bottom 24 pixels remains the game's own picture.
    for (int i = 0; i < width * (int)(216 * scale) * 3; ++i) CHECK(pixels[i] == 130);
    CHECK(HiResScreenSmoke::Run(PelagoArea, ScreenStyle::Radius, [&](UI::Canvas &c) { screen.Draw(c); }));
    using namespace UI::Theme;
    CHECK(5 + PromptWidth(Button::L,"< Isle") + 5 + PromptWidth(Button::R,">") + 12
          + PromptWidth(Button::Start,"Mohn") + 12 + PromptWidth(Button::B,"Leave") <= PelagoArea.w);
    if (!dir) return;
    const std::string path = std::string(dir) + (scale == 1 ? "/pelago-1x.ppm" : "/pelago-2.5x.ppm");
    FILE *f = std::fopen(path.c_str(), "wb"); CHECK(f != nullptr); if (!f) return;
    std::fprintf(f,"P6\n%d %d\n255\n",width,height);
    for (size_t i = 0; i < pixels.size(); i += 3)
    { const u8 rgb[] = {pixels[i+2],pixels[i+1],pixels[i]}; std::fwrite(rgb,1,3,f); }
    std::fclose(f);
}
int main(int argc, char **argv)
{
    Captures();
    ProbeHold();
    Keys();
    for (auto scenario : {Scenario::Good, Scenario::FlickeringAsk, Scenario::NoAsk, Scenario::NoMessage, Scenario::StuckYes,
                         Scenario::LostScene, Scenario::ChangedPrompt, Scenario::SlowExit}) Sequence(scenario);
    Sequence(Scenario::Good, true);
    Sequence(Scenario::FlickeringAsk, true);
    std::vector<std::string> log; ActionRunner runner; ActionWorld world;
    world.scene = PelagoScene;
    CHECK(!runner.Start(PelagoLeaveAction(), world, log)); // no isle authorisation
    world.scene = "FieldRo"; world.flags = WorldPelagoIsle;
    CHECK(!runner.Start(PelagoLeaveAction(), world, log));
    Preview(1, argc > 1 ? argv[1] : nullptr); Preview(2.5f, argc > 1 ? argv[1] : nullptr);
    std::printf("pelago_test: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
