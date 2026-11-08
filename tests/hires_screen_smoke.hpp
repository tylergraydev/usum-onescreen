#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include "UI/Layer.hpp"

// Exercise the actual Layer/Canvas hi-res lifecycle with guarded CPU staging;
// no libctru, framebuffer copies, or GPU is required. Every screen test keeps
// its original 1x assertions and additionally draws representative states here.
namespace HiResScreenSmoke
{
    using namespace OneScreen::UI;
    inline std::vector<u8> staging;
    inline s32 width = 0, height = 0, stride = 0;
    inline bool submitted = false;
    inline bool Ready() { return true; }
    inline bool Setup(u32, s32 w, s32 h, Surface &out)
    {
        width = w; height = h; stride = w * 4 + 16;
        staging.assign(128 + (size_t)stride * h, 0xCD);
        out = Surface{ staging.data() + 64, 4, stride, PixelFormat::RGBA8, w, h };
        out.transparent = true;
        return true;
    }
    inline bool Present(u32, const Surface &, const Rect &, bool)
    {
        submitted = true;
        return true;
    }
    inline void Hide(u32) {}

    template<class Draw>
    bool Run(const Rect &area, s32 radius, Draw draw, const Rect &skip = Rect{})
    {
        const HiResBackend saved = Layer::s_hiRes;
        HiResBackend backend{};
        backend.ready = Ready; backend.setup = Setup;
        backend.present = Present; backend.hide = Hide;
        Layer::s_hiRes = backend;
        std::vector<u8> fbPixels((size_t)ScreenW * ScreenH * 4, 0);
        Surface fb{ fbPixels.data(), 4, ScreenW * 4, PixelFormat::RGBA8, ScreenW, ScreenH };
        std::vector<u8> fallback((size_t)ScreenW * ScreenH * 4, 0);
        Layer layer;
        submitted = false;
        bool ok = layer.SetupHiRes(fb, area, radius, 0, fallback.data(), fallback.size());
        if (!ok)
        {
            Layer::s_hiRes = saved;
            return false;
        }
        Canvas canvas = layer.Begin();
        ok = ok && canvas.Scale() == HiResScale;
        draw(canvas);
        ok = !layer.Present(fb, skip) && ok && submitted;
        bool ink = false;
        const float rad = std::max<s32>(0, std::min(radius, std::min(area.w, area.h) / 2)) * HiResScale;
        for (s32 y = 0; y < height; y++)
        {
            for (s32 x = 0; x < width; x++)
            {
                const u8 alpha = staging[64 + (size_t)y * stride + x * 4];
                ink = ink || alpha > 0;
                const float dx = std::max(0.f, std::max(rad - x - .5f, x + .5f - (width - rad)));
                const float dy = std::max(0.f, std::max(rad - y - .5f, y + .5f - (height - rad)));
                if (rad > 0 && std::sqrt(dx * dx + dy * dy) >= rad + .5f)
                    ok = ok && alpha == 0;
                const float lx = area.x + (x + .5f) / HiResScale;
                const float ly = area.y + (y + .5f) / HiResScale;
                if (!skip.Empty() && lx >= skip.x && lx < skip.Right() && ly >= skip.y && ly < skip.Bottom())
                    ok = ok && alpha == 0;
            }
            for (s32 i = width * 4; i < stride; i++)
                ok = ok && staging[64 + (size_t)y * stride + i] == 0xCD;
        }
        for (size_t i = 0; i < 64; i++)
            ok = ok && staging[i] == 0xCD && staging[staging.size() - 1 - i] == 0xCD;
        ok = ok && ink && std::all_of(fbPixels.begin(), fbPixels.end(), [](u8 b) { return b == 0; });
        layer.Hide();
        Layer::s_hiRes = saved;
        return ok;
    }
}
