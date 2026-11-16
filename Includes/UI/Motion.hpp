#pragma once

#include "UI/Raster.hpp"

namespace OneScreen { namespace UI { namespace Motion {
    constexpr unsigned CursorFrames = 8;
    constexpr unsigned GaugeFrames = 24;

    // Integer quadratic ease-out, including exact endpoints. No clock or allocation.
    int Ease(int from, int to, unsigned frame, unsigned frames);

    enum class Screen { XMenu, Party, Bag };
    // OSD thread only. Begin once per visible top-screen callback; End after
    // drawing. Standalone renderers (including battle callers) stay instantaneous.
    void Begin(Screen screen, bool reset = false);
    void End(bool drawn = true);
    void Reset(Screen screen);
    bool Active(Screen screen);
    Rect Cursor(Screen screen, const Rect &target, int identity = 0);
    int Gauge(Screen screen, unsigned slot, int hp, int maxHp, unsigned identity);
} } }
