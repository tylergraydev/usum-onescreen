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

    // Packed render-to-texture box-filter pyramid in the existing FB scratch.
    // Level 0 averages a dense 4x4 bilinear grid; subsequent 2:1 reductions
    // explicitly average four texels each, also at emulator upscales.
    // The final 8x8 texture is magnified.
    static constexpr u32 BlurLevels = 4, BlurFilterTaps = 16;
    constexpr u32 BlurLevelTaps(u32 level) { return level == 0 ? BlurFilterTaps : 4; }
    static constexpr u32 OverlayGxCommands = 1;
    constexpr u32 BlurLevelSize(u32 level) { return 64u >> level; }
    constexpr u32 BlurLevelOffset(u32 level)
    {
        u32 offset = 0;
        for (u32 i = 0; i < level; i++) offset += BlurLevelSize(i) * BlurLevelSize(i) * 4;
        return offset;
    }
    static constexpr u32 BlurPyramidBytes = BlurLevelOffset(BlurLevels);
    inline u8 BlurTapAlpha(u32 tap) { return (u8)((255 + (tap + 1) / 2) / (tap + 1)); }

    // Standard (unrotated) offscreen target, sharing exactly these UVs with the
    // software regression renderer. Pixel centres stay within the owning region.
    inline void BlurPassVertices(float *v, const Rect &region, u32 level, u32 tap)
    {
        const float size = level == 0 ? 1024.f : (float)BlurLevelSize(level - 1);
        // The guard also contains shifted footprints at arbitrary emulator
        // raster scales, whose first pixel centre is closer to the quad edge.
        const s32 gx = region.w / 128 + 1, gy = region.h / 128 + 1;
        const Rect r = level == 0 ? Rect{ region.x + gx, region.y + gy, region.w - 2 * gx, region.h - 2 * gy }
                                  : Rect{ 0, 0, (s32)size, (s32)size };
        const float du = level == 0 ? (((tap % 4) + .5f) / 4 - .5f) * r.w / 64.f : (float)(tap % 2) - .5f;
        const float dv = level == 0 ? (((tap / 4) + .5f) / 4 - .5f) * r.h / 64.f : (float)(tap / 2) - .5f;
        for (u32 i = 0; i < 4; i++, v += 6)
        {
            v[0] = i < 2 ? -1.f : 1.f; v[1] = (i & 1) ? -1.f : 1.f;
            v[2] = -.5f; v[3] = 1.f;
            v[4] = ((i < 2 ? r.x : r.Right()) + du) / size;
            v[5] = 1.f - (((i & 1) ? r.Bottom() : r.y) + dv) / size;
        }
    }

    template<class Emit>
    void BlurBandRuns(const ColumnRange &range, Emit emit)
    {
        if (!range.active) return;
        if (range.begin > 0) emit(0, range.begin);
        if (range.end < ScreenW) emit(range.end, ScreenW - range.end);
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
