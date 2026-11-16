#include "UI/Motion.hpp"

namespace OneScreen { namespace UI { namespace Motion {
    int Ease(int from, int to, unsigned frame, unsigned frames)
    {
        if (frames == 0 || frame >= frames)
            return to;
        const std::int64_t n = frames, left = frames - frame;
        return from + static_cast<int>((static_cast<std::int64_t>(to) - from)
                                      * (n * n - left * left) / (n * n));
    }

    struct Value
    {
        int from = 0, target = 0, current = 0;
        unsigned frame = 0, duration = 0;
        bool Moving() const { return frame < duration; }
        void Snap(int n) { from = target = current = n; frame = duration = 0; }
        void Target(int n, unsigned frames)
        {
            if (n == target) return;
            from = current; target = n; frame = 0; duration = frames;
        }
        void Step()
        {
            if (Moving()) current = Ease(from, target, ++frame, duration);
        }
    };
    struct GaugeState
    {
        Value value;
        unsigned identity = 0;
        int maximum = 0;
        bool valid = false;
    };
    struct State
    {
        Value x, y;
        Rect cursor{};
        int identity = 0;
        bool valid = false, dirty = false, enabled = true;
        GaugeState gauges[6];
    };
    static State states[3];
    static int context = -1;
    static State &Get(Screen screen) { return states[static_cast<unsigned>(screen)]; }

    void Reset(Screen screen) { Get(screen) = State{}; }
    void Begin(Screen screen, bool reset)
    {
        if (reset) Reset(screen);
        context = static_cast<int>(screen);
        State &s = Get(screen);
        const bool enabled = GetSettings().uiMotion;
        s.dirty = s.dirty || s.enabled != enabled;
        s.enabled = enabled;
        const auto step = [&](Value &v)
        {
            s.dirty = s.dirty || v.Moving();
            if (enabled) v.Step(); else v.Snap(v.target);
        };
        step(s.x); step(s.y);
        for (auto &g : s.gauges) step(g.value);
    }
    void End(bool drawn)
    {
        if (drawn && context >= 0) states[context].dirty = false;
        context = -1;
    }
    bool Active(Screen screen)
    {
        const State &s = Get(screen);
        if (s.dirty || s.x.Moving() || s.y.Moving()) return true;
        for (const auto &g : s.gauges) if (g.value.Moving()) return true;
        return false;
    }
    Rect Cursor(Screen screen, const Rect &target, int identity)
    {
        if (context != static_cast<int>(screen)) return target;
        State &s = Get(screen);
        if (!s.enabled || !s.valid || s.identity != identity)
        {
            s.x.Snap(target.x); s.y.Snap(target.y);
        }
        else
        {
            s.x.Target(target.x, CursorFrames); s.y.Target(target.y, CursorFrames);
        }
        s.valid = true; s.identity = identity; s.cursor = target;
        return Rect{ s.x.current, s.y.current, target.w, target.h };
    }
    int Gauge(Screen screen, unsigned slot, int hp, int maxHp, unsigned identity)
    {
        if (context != static_cast<int>(screen) || slot >= 6) return hp;
        State &s = Get(screen);
        GaugeState &g = s.gauges[slot];
        if (!s.enabled || !g.valid || g.identity != identity || g.maximum != maxHp)
            g.value.Snap(hp);
        else
            g.value.Target(hp, GaugeFrames);
        g.valid = true; g.identity = identity; g.maximum = maxHp;
        return g.value.current;
    }
} } }
