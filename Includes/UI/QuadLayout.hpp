#pragma once

#include <cstring>
#include "UI/Raster.hpp"

namespace OneScreen { namespace UI {
    // CPU Morton upload, shared with host tests. Atlas regions must not overlap;
    // leave a transparent texel between unrelated regions for bilinear filtering.
    inline bool TileQuadRegion(const Surface &source, const Rect &region, u8 *atlas, s32 width, s32 height)
    {
        if (source.origin == nullptr || atlas == nullptr || source.format != PixelFormat::RGBA8
            || region.Empty() || source.width != region.w || source.height != region.h
            || region.x < 0 || region.y < 0 || region.Right() > width || region.Bottom() > height
            || width <= 0 || height <= 0 || (width & 7) != 0 || (height & 7) != 0) return false;
        for (s32 y = 0; y < region.h; y++)
            for (s32 x = 0; x < region.w; x++)
            {
                const u32 tx = (u32)(region.x + x), ty = (u32)(region.y + y);
                const u32 morton = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1)
                    | ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
                const u32 at = (((ty / 8) * (u32)(width / 8) + tx / 8) * 64 + morton) * 4;
                std::memcpy(atlas + at, source.origin + y * source.stepY + x * source.stepX, 4);
            }
        return true;
    }

    // Four vertices, each float4 clip position + float2 UV. Rotated top target;
    // same x=200 squeeze as SourceColumn, continuous rather than rounded to 1x.
    inline void QuadVertices(float *v, const Rect &r, const Rect &tex, s32 texW, s32 texH,
                             float squeeze, bool reverse)
    {
        if (!(squeeze >= 1.f)) squeeze = 1.f;
        const float left = (float)tex.x / texW, right = (float)tex.Right() / texW;
        const float top = 1.f - (float)tex.y / texH, bottom = 1.f - (float)tex.Bottom() / texH;
        const float corners[4][4] = { { (float)r.x, (float)r.y, left, top },
            { (float)r.x, (float)r.Bottom(), left, bottom },
            { (float)r.Right(), (float)r.y, right, top },
            { (float)r.Right(), (float)r.Bottom(), right, bottom } };
        for (int i = 0; i < 4; i++, v += 6)
        {
            const float x = 200.f + (corners[i][0] - 200.f) / squeeze;
            v[0] = reverse ? 1.f - corners[i][1] / 120.f : corners[i][1] / 120.f - 1.f;
            v[1] = 1.f - x / 200.f;
            v[2] = -.5f; v[3] = 1.f; v[4] = corners[i][2]; v[5] = corners[i][3];
        }
    }
} }
