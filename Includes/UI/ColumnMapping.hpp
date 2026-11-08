#pragma once

#include <cmath>
#include <algorithm>
#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    // Destination -> unsqueezed screen column, nearest neighbour about x=200.
    // Clamp before converting to int: even extreme finite aspects are safe.
    // -1/400 are out-of-screen sentinels, never valid layer columns.
    inline int SourceColumn(int destination, float squeeze)
    {
        if (!(squeeze > 1.0f))
            return destination;
        const double source = 200.0 + (destination - 200) * (double)squeeze;
        if (source < -0.5) return -1;
        if (source >= 399.5) return 400;
        return (int)std::floor(source + 0.5);
    }

    struct ColumnRange
    {
        int begin, end; // Destination coverage [begin, end).
        bool active;
    };

    // Top columns whose nearest-neighbour source is in [sourceBegin, sourceEnd).
    inline ColumnRange SqueezedColumns(int sourceBegin, int sourceEnd, float squeeze)
    {
        const auto lowerBound = [&](int source) {
            int lo = 0, hi = ScreenW;
            while (lo < hi)
            {
                const int mid = lo + (hi - lo) / 2;
                if (SourceColumn(mid, squeeze) < source) lo = mid + 1;
                else hi = mid;
            }
            return lo;
        };
        if (sourceBegin >= sourceEnd)
            return { 0, 0, false };
        return { lowerBound(sourceBegin), lowerBound(sourceEnd), true };
    }

    inline ColumnRange PillarboxColumns(const Surface &screen, const Rect &area)
    {
        // Full-screen native areas contain the standard 376x224 inset.
        // Compact Options, battle controls, badges and bottom output don't qualify.
        if (screen.width != ScreenW || screen.height != ScreenH || !(screen.squeeze > 1.0f)
            || screen.pillarbox == Pillarbox::Off || area.Empty()
            || area.x > 12 || area.y > 8 || area.Right() < 388 || area.Bottom() < 232)
            return { 0, 0, false };
        return SqueezedColumns(area.x, area.Right(), screen.squeeze);
    }

    // Each run samples consecutive stripe phases, or stretches one tint/edge
    // phase. Destination coordinates match the legacy per-column presenter.
    template<class Emit>
    void BandRuns(const ColumnRange &range, Pillarbox mode, Emit emit)
    {
        if (!range.active || mode == Pillarbox::Off) return;
        const auto side = [&](int begin, int end, bool left) {
            for (int x = begin; x < end;)
            {
                const int distance = left ? range.begin - 1 - x : x - range.end;
                const int phase = distance < 3 ? 11 - distance : mode == Pillarbox::Pattern ? x & 7 : 8;
                const int limit = left ? range.begin - 3 : end;
                const int width = distance < 3 ? 1 : mode == Pillarbox::Pattern
                    ? std::min(8 - phase, limit - x) : limit - x;
                emit(x, width, phase);
                x += width;
            }
        };
        side(0, range.begin, true); side(range.end, ScreenW, false);
    }

}
}
