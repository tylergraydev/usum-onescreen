#include "UI/Layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace OneScreen
{
namespace UI
{
    // Column-contiguous screens (the 3DS ones: stored rotated, so a screen
    // column is one run of memory) get a layer with the same order.
    static bool Columnar(const Surface &s)
    {
        const s32 bpp = (s32)BytesPerPixel(s.format);
        return s.stepY == bpp || s.stepY == -bpp;
    }

    size_t Layer::BytesFor(const Surface &like, const Rect &area)
    {
        return (size_t)(area.w * area.h) * BytesPerPixel(like.format);
    }

    bool Layer::Setup(const Surface &like, const Rect &area, s32 radius, u8 *buffer, size_t size)
    {
        m_ready = false;
        if (area.Empty() || area.w > MaxW || area.x < 0 || area.y < 0 || area.Right() > like.width
            || area.Bottom() > like.height || size < BytesFor(like, area))
            return false;

        const s32 bpp = (s32)BytesPerPixel(like.format);
        m_surface.format = like.format;
        m_surface.width = area.w;
        m_surface.height = area.h;
        if (Columnar(like))
        {
            m_surface.stepX = area.h * bpp;
            m_surface.stepY = like.stepY;
            m_surface.origin = buffer + (like.stepY < 0 ? (area.h - 1) * bpp : 0);
        }
        else
        {
            m_surface.stepX = bpp;
            m_surface.stepY = area.w * bpp;
            m_surface.origin = buffer;
        }
        m_buffer = buffer;
        m_area = area;
        m_likeStepX = like.stepX;
        m_likeStepY = like.stepY;

        // A corner pixel is copied if its center is inside the corner circle,
        // i.e. it is at least half covered (same measure as FillRoundRect).
        const s32 rad = std::max<s32>(0, std::min(radius, std::min(area.w, area.h) / 2));
        for (s32 i = 0; i < area.w; i++)
        {
            const float fx = i + 0.5f;
            const float dx = std::max(0.f, std::max(rad - fx, fx - (area.w - rad)));
            s32 top = 0;
            while (top < rad)
            {
                const float dy = rad - (top + 0.5f);
                if (dx * dx + dy * dy <= (float)(rad * rad))
                    break;
                top++;
            }
            m_top[i] = (u8)top;
        }
        m_ready = true;
        return true;
    }

    bool Layer::Matches(const Surface &screen) const
    {
        return m_ready && screen.format == m_surface.format && screen.stepX == m_likeStepX
            && screen.stepY == m_likeStepY && m_area.Right() <= screen.width && m_area.Bottom() <= screen.height;
    }

    Canvas Layer::Begin(void)
    {
        Canvas canvas(m_surface, m_area.x, m_area.y);
        // Black is all-zero bytes in every format but RGBA8 (alpha byte 0xFF).
        if (m_surface.format == PixelFormat::RGBA8)
            canvas.FillRect(m_area, Rgb(0, 0, 0));
        else
            std::memset(m_buffer, 0, BytesFor(m_surface, m_area));
        return canvas;
    }

    void Layer::Present(const Surface &screen) const
    {
        if (!Matches(screen))
            return;
        const s32 bpp = (s32)BytesPerPixel(screen.format);
        const bool columnar = Columnar(screen);
        for (s32 i = 0; i < m_area.w; i++)
        {
            const s32 y0 = m_top[i], y1 = m_area.h - m_top[i];
            if (y1 <= y0)
                continue;
            const u8 *src = m_surface.origin + i * m_surface.stepX;
            u8 *dst = screen.origin + (m_area.x + i) * screen.stepX + m_area.y * screen.stepY;
            if (columnar)
            {
                // The run starts at whichever end of the column is lower in memory.
                const s32 first = screen.stepY < 0 ? y1 - 1 : y0;
                std::memcpy(dst + first * screen.stepY, src + first * m_surface.stepY, (size_t)((y1 - y0) * bpp));
                continue;
            }
            for (s32 y = y0; y < y1; y++)
                std::memcpy(dst + y * screen.stepY, src + y * m_surface.stepY, (size_t)bpp);
        }
    }
}
}
