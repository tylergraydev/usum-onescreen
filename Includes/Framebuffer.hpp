#pragma once

#include <CTRPluginFramework.hpp>

namespace OneScreen
{
    using CTRPluginFramework::GSPFormat;
    using CTRPluginFramework::Screen;

    static constexpr u32 TopWidth     = 400;
    static constexpr u32 BottomWidth  = 320;
    static constexpr u32 ScreenHeight = 240;

    // Decode one pixel of a GSP framebuffer into 8-bit R, G, B.
    inline void DecodePixel(const u8 *p, GSPFormat format, u8 &r, u8 &g, u8 &b)
    {
        switch (format)
        {
        case GSP_RGBA8_OES:     // bytes A, B, G, R
            r = p[3]; g = p[2]; b = p[1];
            break;
        case GSP_BGR8_OES:      // bytes B, G, R
            r = p[2]; g = p[1]; b = p[0];
            break;
        case GSP_RGB565_OES:
        {
            const u16 v = p[0] | (p[1] << 8);
            r = (v >> 11) << 3; g = ((v >> 5) & 0x3F) << 2; b = (v & 0x1F) << 3;
            break;
        }
        case GSP_RGB5_A1_OES:
        {
            const u16 v = p[0] | (p[1] << 8);
            r = (v >> 11) << 3; g = ((v >> 6) & 0x1F) << 3; b = ((v >> 1) & 0x1F) << 3;
            break;
        }
        case GSP_RGBA4_OES:
        {
            const u16 v = p[0] | (p[1] << 8);
            r = (v >> 12) << 4; g = ((v >> 8) & 0xF) << 4; b = ((v >> 4) & 0xF) << 4;
            break;
        }
        default:
            r = g = b = 0;
            break;
        }
    }

    // Encode 8-bit R, G, B into one pixel of a GSP framebuffer.
    inline void EncodePixel(u8 *p, GSPFormat format, u8 r, u8 g, u8 b)
    {
        switch (format)
        {
        case GSP_RGBA8_OES:
            p[0] = 0xFF; p[1] = b; p[2] = g; p[3] = r;
            break;
        case GSP_BGR8_OES:
            p[0] = b; p[1] = g; p[2] = r;
            break;
        case GSP_RGB565_OES:
        {
            const u16 v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            p[0] = v & 0xFF; p[1] = v >> 8;
            break;
        }
        case GSP_RGB5_A1_OES:
        {
            const u16 v = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
            p[0] = v & 0xFF; p[1] = v >> 8;
            break;
        }
        case GSP_RGBA4_OES:
        {
            const u16 v = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | 0xF;
            p[0] = v & 0xFF; p[1] = v >> 8;
            break;
        }
        default:
            break;
        }
    }

    // 3DS framebuffers are stored rotated, so instead of assuming the layout,
    // step between pixels using the addresses CTRPF reports for (x, 0), (x, 1)
    // and (x + 1, 0).
    struct FbLayout
    {
        u8 *origin;
        s32 stepX;
        s32 stepY;
        GSPFormat format;
    };

    inline FbLayout GetLayout(const Screen &screen)
    {
        FbLayout layout;
        layout.origin = screen.GetFramebuffer(0, 0);
        layout.stepX = (s32)(screen.GetFramebuffer(1, 0) - layout.origin);
        layout.stepY = (s32)(screen.GetFramebuffer(0, 1) - layout.origin);
        // Only the low 3 bits are the color format; the game sets other flag
        // bits too (the top screen reports 0x41 = BGR8 | 0x40).
        layout.format = (GSPFormat)(screen.Format & 7);
        return layout;
    }

    // Copies a whole screen into packed RGB888, row-major (width * 240 * 3 bytes).
    inline void ReadScreen(const Screen &screen, u32 width, u8 *out)
    {
        const FbLayout fb = GetLayout(screen);
        for (u32 y = 0; y < ScreenHeight; y++)
        {
            const u8 *p = fb.origin + (s32)y * fb.stepY;
            for (u32 x = 0; x < width; x++, p += fb.stepX, out += 3)
                DecodePixel(p, fb.format, out[0], out[1], out[2]);
        }
    }
}
