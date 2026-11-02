#include "UI/Raster.hpp"

#include <algorithm>
#include <cmath>

namespace OneScreen
{
namespace UI
{
    static inline u8 Coverage(float c)
    {
        if (c <= 0.f) return 0;
        if (c >= 1.f) return 255;
        return (u8)(c * 255.f + 0.5f);
    }

    // x / 255 rounded, for x <= 255 * 255 + 255, without a divide (the ARM11
    // has none, so `/ 255` is a library call per channel).
    static inline u32 Div255(u32 x)
    {
        x += 128;
        return (x + (x >> 8)) >> 8;
    }

    static inline u8 Mul255(u32 a, u32 b)
    {
        return (u8)Div255(a * b);
    }

    Rect Intersect(const Rect &a, const Rect &b)
    {
        const s32 x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
        const s32 x1 = std::min(a.Right(), b.Right()), y1 = std::min(a.Bottom(), b.Bottom());
        if (x1 <= x0 || y1 <= y0)
            return Rect{ x0, y0, 0, 0 };
        return Rect{ x0, y0, x1 - x0, y1 - y0 };
    }

    u32 BytesPerPixel(PixelFormat format)
    {
        switch (format)
        {
        case PixelFormat::RGBA8: return 4;
        case PixelFormat::BGR8:  return 3;
        default:                 return 2;
        }
    }

    // Same byte layouts as DecodePixel/EncodePixel in Framebuffer.hpp, kept
    // here so this file builds without CTRPF.
    void ReadPixel(const u8 *p, PixelFormat format, u8 &r, u8 &g, u8 &b)
    {
        switch (format)
        {
        case PixelFormat::RGBA8:    // bytes A, B, G, R
            r = p[3]; g = p[2]; b = p[1];
            break;
        case PixelFormat::BGR8:     // bytes B, G, R
            r = p[2]; g = p[1]; b = p[0];
            break;
        case PixelFormat::RGB565:
        {
            const u16 v = p[0] | (p[1] << 8);
            r = (v >> 11) << 3; g = ((v >> 5) & 0x3F) << 2; b = (v & 0x1F) << 3;
            break;
        }
        case PixelFormat::RGB5A1:
        {
            const u16 v = p[0] | (p[1] << 8);
            r = (v >> 11) << 3; g = ((v >> 6) & 0x1F) << 3; b = ((v >> 1) & 0x1F) << 3;
            break;
        }
        case PixelFormat::RGBA4:
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

    void WritePixel(u8 *p, PixelFormat format, u8 r, u8 g, u8 b)
    {
        switch (format)
        {
        case PixelFormat::RGBA8:
            p[0] = 0xFF; p[1] = b; p[2] = g; p[3] = r;
            break;
        case PixelFormat::BGR8:
            p[0] = b; p[1] = g; p[2] = r;
            break;
        case PixelFormat::RGB565:
        {
            const u16 v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            p[0] = v & 0xFF; p[1] = v >> 8;
            break;
        }
        case PixelFormat::RGB5A1:
        {
            const u16 v = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
            p[0] = v & 0xFF; p[1] = v >> 8;
            break;
        }
        case PixelFormat::RGBA4:
        {
            const u16 v = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | 0xF;
            p[0] = v & 0xFF; p[1] = v >> 8;
            break;
        }
        default:
            break;
        }
    }

    Canvas::Canvas(const Surface &surface) : m_surface(surface)
    {
        ResetClip();
    }

    Canvas::Canvas(const Surface &surface, s32 originX, s32 originY)
        : m_surface(surface), m_ox(originX), m_oy(originY)
    {
        ResetClip();
    }

    void Canvas::SetClip(const Rect &clip)
    {
        m_clip = Intersect(clip, Rect{ m_ox, m_oy, m_surface.width, m_surface.height });
    }

    void Canvas::ResetClip(void)
    {
        m_clip = Rect{ m_ox, m_oy, m_surface.width, m_surface.height };
    }

    bool Canvas::GetPixel(s32 x, s32 y, Color &out) const
    {
        if (x < m_ox || y < m_oy || x >= m_ox + m_surface.width || y >= m_oy + m_surface.height)
            return false;
        const u8 *p = At(x, y);
        ReadPixel(p, m_surface.format, out.r, out.g, out.b);
        out.a = 255;
        return true;
    }

    void Canvas::BlendPixel(s32 x, s32 y, Color c, u8 coverage)
    {
        if (x < m_clip.x || y < m_clip.y || x >= m_clip.Right() || y >= m_clip.Bottom())
            return;
        const u32 alpha = Mul255(c.a, coverage);
        if (alpha == 0)
            return;

        u8 *p = At(x, y);
        if (alpha == 255)
        {
            WritePixel(p, m_surface.format, c.r, c.g, c.b);
            return;
        }
        u8 r, g, b;
        ReadPixel(p, m_surface.format, r, g, b);
        const u32 inv = 255 - alpha;
        WritePixel(p, m_surface.format,
                   (u8)Div255(c.r * alpha + r * inv),
                   (u8)Div255(c.g * alpha + g * inv),
                   (u8)Div255(c.b * alpha + b * inv));
    }

    // Fills [x0, x1) on row y. Opaque spans encode the color once and copy it,
    // which is most of what a menu draws.
    void Canvas::Span(s32 x0, s32 x1, s32 y, Color c)
    {
        if (y < m_clip.y || y >= m_clip.Bottom())
            return;
        x0 = std::max(x0, m_clip.x);
        x1 = std::min(x1, m_clip.Right());
        if (x1 <= x0 || c.a == 0)
            return;

        u8 *p = At(x0, y);
        if (c.a != 255)
        {
            // Same result as BlendPixel per pixel, with the clip check, the
            // format switch and the colour product hoisted out of the loop:
            // full-screen shades and panel backgrounds are drawn this way.
            const u32 inv = 255 - c.a;
            const u32 pr = c.r * c.a, pg = c.g * c.a, pb = c.b * c.a;
            if (m_surface.format == PixelFormat::BGR8)
            {
                for (s32 x = x0; x < x1; x++, p += m_surface.stepX)
                {
                    p[0] = (u8)Div255(pb + p[0] * inv);
                    p[1] = (u8)Div255(pg + p[1] * inv);
                    p[2] = (u8)Div255(pr + p[2] * inv);
                }
                return;
            }
            for (s32 x = x0; x < x1; x++, p += m_surface.stepX)
            {
                u8 r, g, b;
                ReadPixel(p, m_surface.format, r, g, b);
                WritePixel(p, m_surface.format, (u8)Div255(pr + r * inv), (u8)Div255(pg + g * inv),
                           (u8)Div255(pb + b * inv));
            }
            return;
        }

        u8 encoded[4];
        WritePixel(encoded, m_surface.format, c.r, c.g, c.b);
        const u32 bpp = BytesPerPixel(m_surface.format);
        if (bpp == 3)
        {
            const u8 e0 = encoded[0], e1 = encoded[1], e2 = encoded[2];
            for (s32 x = x0; x < x1; x++, p += m_surface.stepX)
            {
                p[0] = e0;
                p[1] = e1;
                p[2] = e2;
            }
            return;
        }
        for (s32 x = x0; x < x1; x++, p += m_surface.stepX)
            for (u32 i = 0; i < bpp; i++)
                p[i] = encoded[i];
    }

    void Canvas::FillRect(const Rect &r, Color c)
    {
        const Rect area = Intersect(r, m_clip);
        for (s32 y = area.y; y < area.Bottom(); y++)
            Span(area.x, area.Right(), y, c);
    }

    void Canvas::StrokeRect(const Rect &r, Color c, s32 thickness)
    {
        const s32 t = std::min(thickness, std::min(r.w, r.h) / 2 + 1);
        if (t <= 0 || r.Empty())
            return;
        if (t * 2 >= r.h || t * 2 >= r.w)
        {
            FillRect(r, c);
            return;
        }
        // Four non-overlapping bands, so translucent strokes don't double up.
        FillRect(Rect{ r.x, r.y, r.w, t }, c);
        FillRect(Rect{ r.x, r.Bottom() - t, r.w, t }, c);
        FillRect(Rect{ r.x, r.y + t, t, r.h - 2 * t }, c);
        FillRect(Rect{ r.Right() - t, r.y + t, t, r.h - 2 * t }, c);
    }

    void Canvas::HLine(s32 x, s32 y, s32 length, Color c)
    {
        Span(x, x + length, y, c);
    }

    void Canvas::VLine(s32 x, s32 y, s32 length, Color c)
    {
        FillRect(Rect{ x, y, 1, length }, c);
    }

    // Coverage of local pixel (px, py) in a w x h rounded rect of radius rad,
    // measured from the nearest corner circle. inner > 0 cuts out a second,
    // smaller radius for strokes.
    static float CornerCoverage(s32 px, s32 py, s32 w, s32 h, float rad, float inner)
    {
        const float fx = px + 0.5f, fy = py + 0.5f;
        const float dx = std::max(0.f, std::max(rad - fx, fx - (w - rad)));
        const float dy = std::max(0.f, std::max(rad - fy, fy - (h - rad)));
        const float dist = std::sqrt(dx * dx + dy * dy);
        float cov = std::min(1.f, std::max(0.f, rad - dist + 0.5f));
        if (inner >= 0.f)
            cov -= std::min(1.f, std::max(0.f, inner - dist + 0.5f));
        return cov;
    }

    void Canvas::FillRoundRect(const Rect &r, s32 radius, Color c)
    {
        const s32 rad = std::max<s32>(0, std::min(radius, std::min(r.w, r.h) / 2));
        if (rad == 0)
        {
            FillRect(r, c);
            return;
        }
        for (s32 j = 0; j < r.h; j++)
        {
            if (j >= rad && j < r.h - rad)
            {
                Span(r.x, r.Right(), r.y + j, c);
                continue;
            }
            for (s32 i = 0; i < rad; i++)
            {
                BlendPixel(r.x + i, r.y + j, c, Coverage(CornerCoverage(i, j, r.w, r.h, (float)rad, -1.f)));
                const s32 ri = r.w - 1 - i;
                BlendPixel(r.x + ri, r.y + j, c, Coverage(CornerCoverage(ri, j, r.w, r.h, (float)rad, -1.f)));
            }
            Span(r.x + rad, r.Right() - rad, r.y + j, c);
        }
    }

    void Canvas::StrokeRoundRect(const Rect &r, s32 radius, Color c, s32 thickness)
    {
        const s32 rad = std::max<s32>(0, std::min(radius, std::min(r.w, r.h) / 2));
        const s32 t = std::min(thickness, rad);
        if (rad == 0 || t <= 0)
        {
            StrokeRect(r, c, thickness);
            return;
        }
        // Straight edges between the corner squares.
        FillRect(Rect{ r.x + rad, r.y, r.w - 2 * rad, t }, c);
        FillRect(Rect{ r.x + rad, r.Bottom() - t, r.w - 2 * rad, t }, c);
        FillRect(Rect{ r.x, r.y + rad, t, r.h - 2 * rad }, c);
        FillRect(Rect{ r.Right() - t, r.y + rad, t, r.h - 2 * rad }, c);

        const float inner = (float)(rad - t);
        for (s32 j = 0; j < rad; j++)
        {
            for (s32 i = 0; i < rad; i++)
            {
                const s32 ri = r.w - 1 - i, rj = r.h - 1 - j;
                BlendPixel(r.x + i,  r.y + j,  c, Coverage(CornerCoverage(i,  j,  r.w, r.h, (float)rad, inner)));
                BlendPixel(r.x + ri, r.y + j,  c, Coverage(CornerCoverage(ri, j,  r.w, r.h, (float)rad, inner)));
                BlendPixel(r.x + i,  r.y + rj, c, Coverage(CornerCoverage(i,  rj, r.w, r.h, (float)rad, inner)));
                BlendPixel(r.x + ri, r.y + rj, c, Coverage(CornerCoverage(ri, rj, r.w, r.h, (float)rad, inner)));
            }
        }
    }

    void Canvas::Line(float x0, float y0, float x1, float y1, float width, Color c)
    {
        const float hw = width * 0.5f;
        const Rect box = Intersect(Rect{ (s32)std::floor(std::min(x0, x1) - hw - 1.f),
                                         (s32)std::floor(std::min(y0, y1) - hw - 1.f),
                                         (s32)std::ceil(std::fabs(x1 - x0) + width + 3.f),
                                         (s32)std::ceil(std::fabs(y1 - y0) + width + 3.f) }, m_clip);
        const float dx = x1 - x0, dy = y1 - y0;
        const float len2 = dx * dx + dy * dy;

        for (s32 y = box.y; y < box.Bottom(); y++)
        {
            for (s32 x = box.x; x < box.Right(); x++)
            {
                // Distance from the pixel center to the segment.
                const float px = x + 0.5f - x0, py = y + 0.5f - y0;
                float t = len2 > 0.f ? (px * dx + py * dy) / len2 : 0.f;
                t = std::min(1.f, std::max(0.f, t));
                const float ex = px - t * dx, ey = py - t * dy;
                const float dist = std::sqrt(ex * ex + ey * ey);
                BlendPixel(x, y, c, Coverage(hw - dist + 0.5f));
            }
        }
    }

    void Canvas::FillTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color c)
    {
        // Make the winding counter-clockwise in screen space so "inside" is
        // positive for every edge.
        if ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax) < 0.f)
        {
            std::swap(bx, cx);
            std::swap(by, cy);
        }
        const float xs[3] = { ax, bx, cx }, ys[3] = { ay, by, cy };
        float nx[3], ny[3], d[3];
        for (int e = 0; e < 3; e++)
        {
            const int n = (e + 1) % 3;
            const float ex = xs[n] - xs[e], ey = ys[n] - ys[e];
            const float len = std::sqrt(ex * ex + ey * ey);
            if (len == 0.f)
                return;
            nx[e] = -ey / len;
            ny[e] = ex / len;
            d[e] = nx[e] * xs[e] + ny[e] * ys[e];
        }

        const float minX = std::min(ax, std::min(bx, cx)), maxX = std::max(ax, std::max(bx, cx));
        const float minY = std::min(ay, std::min(by, cy)), maxY = std::max(ay, std::max(by, cy));
        const Rect box = Intersect(Rect{ (s32)std::floor(minX) - 1, (s32)std::floor(minY) - 1,
                                         (s32)std::ceil(maxX - minX) + 3, (s32)std::ceil(maxY - minY) + 3 }, m_clip);

        for (s32 y = box.y; y < box.Bottom(); y++)
        {
            for (s32 x = box.x; x < box.Right(); x++)
            {
                const float px = x + 0.5f, py = y + 0.5f;
                float inside = 1e9f;
                for (int e = 0; e < 3; e++)
                    inside = std::min(inside, nx[e] * px + ny[e] * py - d[e]);
                BlendPixel(x, y, c, Coverage(inside + 0.5f));
            }
        }
    }

    void Canvas::DrawMask(s32 x, s32 y, const u8 *mask, s32 w, s32 h, s32 stride, s32 scale, Color c)
    {
        if (scale < 1)
            scale = 1;
        // Clip once to the mask pixels that touch the clip rect (BlendPixel
        // clips the scaled ones exactly), then blend row by row with the
        // colour products hoisted: glyph masks are drawn this way.
        const s32 i0 = std::max<s32>(0, (m_clip.x - x) / scale);
        const s32 i1 = std::min<s32>(w, (m_clip.Right() - x + scale - 1) / scale);
        const s32 j0 = std::max<s32>(0, (m_clip.y - y) / scale);
        const s32 j1 = std::min<s32>(h, (m_clip.Bottom() - y + scale - 1) / scale);
        const bool fast = scale == 1 && m_surface.format == PixelFormat::BGR8;
        for (s32 j = j0; j < j1; j++)
        {
            const u8 *row = mask + j * stride;
            if (fast)
            {
                u8 *p = At(x + i0, y + j);
                for (s32 i = i0; i < i1; i++, p += m_surface.stepX)
                {
                    const u32 cov = row[i];
                    if (cov == 0)
                        continue;
                    const u32 alpha = Mul255(c.a, cov), inv = 255 - alpha;
                    p[0] = (u8)Div255(c.b * alpha + p[0] * inv);
                    p[1] = (u8)Div255(c.g * alpha + p[1] * inv);
                    p[2] = (u8)Div255(c.r * alpha + p[2] * inv);
                }
                continue;
            }
            for (s32 i = i0; i < i1; i++)
            {
                const u8 cov = row[i];
                if (cov == 0)
                    continue;
                for (s32 sy = 0; sy < scale; sy++)
                    for (s32 sx = 0; sx < scale; sx++)
                        BlendPixel(x + i * scale + sx, y + j * scale + sy, c, cov);
            }
        }
    }
}
}
