#pragma once

#include <cstdint>
#include "Settings.hpp"

// Drawing primitives for plugin-drawn menus. Nothing here depends on CTRPF or
// libctru, so the same code builds for the 3DS and for the PC host test.
namespace OneScreen
{
namespace UI
{
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using s32 = std::int32_t;

    static constexpr s32 ScreenW = 400;
    static constexpr s32 ScreenH = 240;

    // Same values as libctru's GSPGPU_FramebufferFormat, so a GSP format
    // (masked with & 7) can be cast straight in.
    enum class PixelFormat : u8
    {
        RGBA8  = 0,
        BGR8   = 1,
        RGB565 = 2,
        RGB5A1 = 3,
        RGBA4  = 4,
    };

    struct Color
    {
        u8 r, g, b, a;
    };

    constexpr Color Rgb(u8 r, u8 g, u8 b, u8 a = 255)
    {
        return Color{ r, g, b, a };
    }

    struct Rect
    {
        s32 x, y, w, h;

        s32  Right(void) const  { return x + w; }
        s32  Bottom(void) const { return y + h; }
        bool Empty(void) const  { return w <= 0 || h <= 0; }
    };

    Rect Intersect(const Rect &a, const Rect &b);

    // A framebuffer described by where pixel (0, 0) is and how far apart its
    // neighbours are. 3DS framebuffers are stored rotated, so the steps come
    // from the real screen (UI/Device.hpp) and may be negative.
    struct Surface
    {
        u8         *origin;
        s32         stepX;
        s32         stepY;
        PixelFormat format;
        s32         width;
        s32         height;
        // Presentation only: top-screen stretch, supplied by UI/Device.
        float       squeeze = 1.0f;
        Pillarbox   pillarbox = Pillarbox::Tint;
        bool        transparent = false; // straight-alpha RGBA target; legacy surfaces stay opaque
    };

    enum class ImageFilter : u8 { Nearest, Bilinear };
    enum class ImageFormat : u8 { RGBA8, RGB5A1 };
    // Immutable row-major art. RGBA8 uses ordinary R,G,B,A byte order;
    // RGB5A1 is the little-endian packed box-icon format, expanded to 0..255.
    struct ImageView
    {
        const u8 *pixels;
        s32 width, height, stride;
        ImageFormat format = ImageFormat::RGBA8;
    };

    u32  BytesPerPixel(PixelFormat format);
    void ReadPixel(const u8 *p, PixelFormat format, u8 &r, u8 &g, u8 &b);
    void WritePixel(u8 *p, PixelFormat format, u8 r, u8 g, u8 b);

    // Draws onto a Surface. Everything is clipped to the clip rect, which is
    // never larger than the surface. Colors with a < 255 blend onto the
    // pixels already there.
    class Canvas
    {
    public:
        explicit Canvas(const Surface &surface);
        // Surface pixel (0, 0) is drawn as point (originX, originY), so code
        // that draws in screen coordinates can draw into an off-screen Layer
        // that covers only part of the screen.
        Canvas(const Surface &surface, s32 originX, s32 originY, float scale = 1.0f);
        float Scale(void) const { return m_scale; }
        // Smooth mask with fractional screen dimensions (native font sheets).
        void DrawMaskSized(float x, float y, const u8 *mask, s32 w, s32 h, s32 stride,
                           float width, float height, Color c);
        // Samples once per physical destination pixel, preventing seams at
        // fractional Canvas scales. Bilinear filtering interpolates premultiplied
        // colours so transparent texels cannot produce dark fringes. Rotate
        // turns the source a quarter left before fitting it into dst.
        void DrawImage(const ImageView &image, const Rect &src, const Rect &dst,
                       ImageFilter filter = ImageFilter::Bilinear,
                       Color tint = Rgb(255, 255, 255), bool rotate = false);

        const Surface &GetSurface(void) const { return m_surface; }
        const Rect    &GetClip(void) const { return m_clip; }
        void           SetClip(const Rect &clip);
        void           ResetClip(void);

        bool GetPixel(s32 x, s32 y, Color &out) const;
        // Blends c at (x, y), scaled by coverage (255 = fully covered).
        void BlendPixel(s32 x, s32 y, Color c, u8 coverage = 255);

        void FillRect(const Rect &r, Color c);
        void StrokeRect(const Rect &r, Color c, s32 thickness = 1);
        void HLine(s32 x, s32 y, s32 length, Color c);
        void VLine(s32 x, s32 y, s32 length, Color c);

        // Anti-aliased corners. Radius is limited to half the shorter side;
        // stroke thickness is limited to the radius.
        void FillRoundRect(const Rect &r, s32 radius, Color c);
        void StrokeRoundRect(const Rect &r, s32 radius, Color c, s32 thickness = 1);

        // Anti-aliased line with round-ish ends and a filled triangle, in
        // pixel coordinates (pixel centers are at +0.5).
        void Line(float x0, float y0, float x1, float y1, float width, Color c);
        void FillTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color c);

        // Draws a coverage mask (0 = empty, 255 = solid) in color c, each mask
        // pixel blown up to scale x scale screen pixels.
        void DrawMask(s32 x, s32 y, const u8 *mask, s32 w, s32 h, s32 stride, s32 scale, Color c);

    private:
        Canvas DeviceCanvas(void) const;
        float DeviceX(float x) const { return (x - m_ox) * m_scale; }
        float DeviceY(float y) const { return (y - m_oy) * m_scale; }
        void ScaledRound(const Rect &r, s32 radius, Color c, s32 thickness);
        float m_scale = 1.0f;
        void Span(s32 x0, s32 x1, s32 y, Color c);
        u8  *At(s32 x, s32 y) const
        {
            return m_surface.origin + (x - m_ox) * m_surface.stepX + (y - m_oy) * m_surface.stepY;
        }

        Surface m_surface;
        s32     m_ox = 0, m_oy = 0;
        Rect    m_clip;
    };
}
}
