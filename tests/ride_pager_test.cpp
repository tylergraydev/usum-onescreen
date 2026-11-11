#include "Screens/RidePager.hpp"
#include "hires_screen_smoke.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace OneScreen;

static RidePagerState State(unsigned cursor = 0)
{
    RidePagerState s;
    s.object = 0x318427D8; s.count = 7; s.cursor = cursor;
    for (unsigned r = 0; r < 7; ++r) s.ids[r] = (u8)r;
    return s;
}

static std::vector<Sample> Pixels()
{
    std::vector<Sample> p;
    for (unsigned r = 0; r < 7; ++r)
    {
        p.push_back(r == 4 || r == 5 ? Sample{105, 165, 215} : Sample{209, 253, 255});
        p.push_back(r == 0 || r == 1 || r == 2 || r == 6 ? Sample{255, 150, 0} : Sample{210, 210, 210});
    }
    return p;
}

static void Feed(RidePagerScreen &screen, const std::vector<Sample> &p)
{
    std::vector<std::string> log;
    for (int i = 0; i < 3; ++i) screen.Sampled(p.data(), log);
}

static void Sequence(const ActionDef *def, int row, bool back = false)
{
    assert(def && CheckAction(*def).empty());
    ActionRunner runner;
    ActionWorld world; world.scene = SceneField;
    std::vector<std::string> log;
    assert(runner.Start(*def, world, log));
    unsigned touches = 0, aFrames = 0, lifts = 0;
    bool touched = false;
    for (int f = 0; runner.Running() && f < 150; ++f)
    {
        const ActionOutput out = runner.Update(world, log);
        if (out.touching)
        {
            assert(out.x == (back ? 14 : 180));
            assert(out.y == (back ? 224 : 30 * row + 15));
            ++touches; touched = true;
        }
        else if (touched) ++lifts;
        if (out.keys & ScreenKey::A) ++aFrames;
        assert(!(out.keys & ScreenKey::DPad));
    }
    assert(runner.Succeeded());
    const bool tap = back || row == 0 || row == 3 || row == 6;
    assert(tap ? touches == ActionHoldFrames && aFrames == 0 && lifts > 0
               : touches == 0 && aFrames == ActionHoldFrames);
}

int main(int argc, char **argv)
{
    std::vector<std::string> log;
    RidePagerScreen screen;
    screen.Begin(log);
    assert(!screen.Ready() && screen.CaptureKeys() == 0);
    assert(!screen.Key(ScreenKey::Up, log)); // field D-pad remains the game's
    auto s = State();
    assert(RidePagerValid(s));
    for (unsigned r = 0; r < 7; ++r)
    {
        const auto *button = FindLayoutButton(RidePagerRowButton(r));
        assert(button && button->tapX == 180 && button->tapY == 30 * (int)r + 15);
        assert(button->x0 == 59 && button->x1 == 313);
        assert(std::strcmp(RidePagerName(r), "Unknown ride") != 0);
    }
    assert(!RidePagerRowButton(7));
    auto bad = s; bad.count = 8; assert(!RidePagerValid(bad));
    bad = s; bad.cursor = 7; assert(!RidePagerValid(bad));
    bad = s; bad.ids[2] = 1; assert(!RidePagerValid(bad));
    bad = s; bad.ids[0] = 7; assert(!RidePagerValid(bad));
    bad = s; bad.current = 7; assert(!RidePagerValid(bad));
    auto p = Pixels();
    for (unsigned r = 0; r < 7; ++r)
    {
        screen.Begin(log); screen.SetState(State(r)); Feed(screen, p);
        assert(screen.Ready());
        assert(screen.Available(r) == (r != 4 && r != 5));
        assert(screen.Registered(r) == (r == 0 || r == 1 || r == 2 || r == 6));
        const auto *def = screen.Key(ScreenKey::A, log);
        if (r == 4 || r == 5) assert(!def);
        else Sequence(def, (int)r);
    }
    // Usable water rides also select once, through the game's A cursor path.
    for (unsigned r : {4u, 5u})
    {
        screen.Begin(log); screen.SetState(State(r));
        auto water = p; water[2*r] = {209,253,255}; Feed(screen, water);
        Sequence(screen.Key(ScreenKey::A, log), (int)r);
    }
    screen.Begin(log); s.current = 0; screen.SetState(s); Feed(screen, p);
    Sequence(screen.Key(1u << 11, log), 0); // Y/current ride uses the same selection
    assert(!screen.Key(ScreenKey::A, log)); // no second tap while busy
    screen.ActionDone(log); assert(!screen.Ready()); Feed(screen, p);
    Sequence(screen.Key(ScreenKey::B, log), 0, true);
    screen.ActionDone(log); Feed(screen, p);
    const ActionDef *up = screen.Key(ScreenKey::Up, log);
    assert(up && up->steps[0].keys == ScreenKey::Up);
    screen.ActionDone(log); Feed(screen, p);
    auto faded = p; faded[0] = {0,0,0}; screen.Sampled(faded.data(), log);
    assert(!screen.Ready() && !screen.Key(ScreenKey::A, log));
    Feed(screen, p);
    s.dialog = true; screen.SetState(s); Feed(screen, p);
    assert(!screen.Ready() && screen.CaptureKeys() == 0);
    s = State(1); s.count = 2; s.ids[0] = 6; s.ids[1] = 0;
    assert(RidePagerValid(s)); // row and ID are independent (reduced save unverified)
    screen.SetState(s); Feed(screen, p);
    Sequence(screen.Key(ScreenKey::A, log), 1);
    screen.Begin(log); screen.SetState(State());
    screen.Sampled(p.data(), log); assert(!screen.Ready());
    screen.Sampled(p.data(), log); assert(!screen.Ready());
    screen.Sampled(p.data(), log); assert(screen.Ready());
    s = State(3); s.current = 3; screen.SetState(s);

    std::vector<u8> pixels(400 * 240 * 4);
    UI::Surface fb{pixels.data(), 4, 1600, UI::PixelFormat::RGBA8, 400, 240};
    UI::Canvas canvas(fb); canvas.SetClip(screen.Area()); screen.Draw(canvas);
    assert(HiResScreenSmoke::Run(screen.Area(), ScreenStyle::Radius, [&](UI::Canvas &hi) { screen.Draw(hi); }));
    if (argc > 1)
    {
        FILE *file = std::fopen(argv[1], "wb"); assert(file);
        std::fprintf(file, "P6\n400 240\n255\n");
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 400; ++x)
        {
            UI::Color c{}; canvas.GetPixel(x,y,c);
            const u8 rgb[] = {c.r,c.g,c.b}; std::fwrite(rgb,1,3,file);
        }
        std::fclose(file);
    }
    std::puts("ride_pager_test: all passed (mapping, availability, actions, 1x/2.5x draw)");
}
