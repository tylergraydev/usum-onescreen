#include <cassert>
#include <cstdio>
#include "UI/Motion.hpp"

using namespace OneScreen;
namespace M = UI::Motion;
int main()
{
    assert(M::Ease(0, 64, 0, 8) == 0);
    assert(M::Ease(0, 64, 4, 8) == 48);
    assert(M::Ease(64, 0, 4, 8) == 16);
    assert(M::Ease(-10, 20, 8, 8) == 20);
    assert(M::Ease(10, 20, 99, 8) == 20);
    assert(M::Ease(10, 20, 0, 0) == 20);
    for (int direction : {-1, 1})
    {
        int prev = 0;
        for (unsigned i = 0; i <= 24; ++i)
        {
            int n = M::Ease(0, direction * 500, i, 24);
            assert(direction * n >= direction * prev && direction * n <= 500);
            prev = n;
        }
        assert(prev == direction * 500);
    }
    Settings settings;
    SetSettings(settings);
    const auto screen = M::Screen::Party;
    const UI::Rect a{10, 20, 30, 40}, b{90, 100, 50, 40};
    M::Begin(screen, true);
    assert(M::Cursor(screen, a).x == a.x);
    assert(M::Gauge(screen, 0, 100, 100, 7) == 100);
    assert(M::Cursor(screen, b).x == a.x);
    assert(M::Gauge(screen, 0, 0, 100, 7) == 100);
    M::End();
    for (unsigned i = 1; i <= 24; ++i)
    {
        M::Begin(screen);
        assert(M::Active(screen)); // includes the last exact-target redraw
        const auto r = M::Cursor(screen, b);
        assert(r.x == M::Ease(a.x, b.x, i, 8));
        assert(r.y == M::Ease(a.y, b.y, i, 8));
        assert(r.w == b.w);
        assert(M::Gauge(screen, 0, 0, 100, 7) == M::Ease(100, 0, i, 24));
        M::End();
    }
    M::Begin(screen);
    assert(!M::Active(screen));
    M::Gauge(screen, 0, 100, 100, 7); // heal, then retarget while moving
    M::End(); M::Begin(screen);
    const int displayed = M::Gauge(screen, 0, 100, 100, 7);
    assert(displayed > 0 && displayed < 100);
    assert(M::Gauge(screen, 0, 50, 100, 7) == displayed);
    assert(M::Gauge(screen, 1, 20, 100, 8) == 20);
    assert(M::Gauge(screen, 0, 30, 100, 9) == 30); // replacement snaps
    M::Cursor(screen, a);
    M::End();
    // A busy OSD view lock cannot lose the exact final target redraw.
    for (unsigned i = 0; i < M::CursorFrames; ++i)
    {
        M::Begin(screen);
        M::End(false);
    }
    M::Begin(screen);
    assert(M::Active(screen));
    M::End();
    settings.uiMotion = false; SetSettings(settings);
    M::Begin(screen);
    assert(M::Active(screen)); // disabling an in-flight frame must redraw
    assert(M::Cursor(screen, b).x == b.x);
    assert(M::Gauge(screen, 0, 99, 100, 9) == 99);
    M::End(); M::Begin(screen);
    assert(!M::Active(screen));
    M::End();
    assert(M::Cursor(screen, a).x == a.x); // standalone/battle stays static
    std::vector<std::string> warnings;
    assert(ParseSettings("[display]\nui_motion = off\n", warnings).uiMotion == false);
    assert(warnings.empty());
    assert(ParseSettings(DefaultSettingsText(), warnings).uiMotion);
    ParseSettings("ui_motion = maybe\n", warnings);
    assert(warnings.size() == 1);
    std::puts("motion_test: all passed");
}
