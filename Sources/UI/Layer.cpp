#include "UI/Layer.hpp"
#include "UI/ColumnMapping.hpp"
#include "UI/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace OneScreen
{
namespace UI
{
    static const Layer *s_owners[HiResSlots] = {};
    // Column-contiguous screens (the 3DS ones: stored rotated, so a screen
    // column is one run of memory) get a layer with the same order.
    static bool Columnar(const Surface &s)
    {
        const s32 bpp = (s32)BytesPerPixel(s.format);
        return s.stepY == bpp || s.stepY == -bpp;
    }

    void WriteBandColumn(u8 *column, int phase, PixelFormat format, bool reverse)
    {
        const Color fill = Theme::WindowFill;
        const s32 bpp = (s32)BytesPerPixel(format);
        for (int y = 0; y < ScreenH; y++)
        {
            // Half-bright window olive; stripes differ by just 3 levels.
            const int extra = phase < 8 ? (((phase + y) & 7) < 2 ? 3 : 0)
                            : phase == 8 ? 0 : (phase - 8) * 4;
            u8 *pixel = column + (reverse ? ScreenH - 1 - y : y) * bpp;
            WritePixel(pixel, format, (u8)(fill.r / 2 + extra),
                       (u8)(fill.g / 2 + extra), (u8)(fill.b / 2 + extra));
        }
    }

    // Shared by all layers; OSD callbacks are serial (ScreenBuffer.hpp).
    // Eight diagonal phases, one solid base, three soft edge columns.
    // No heap allocation and no framebuffer reads, including on cache rebuild.
    static void PresentBands(const Surface &screen, const ColumnRange &range)
    {
        if (!range.active) return;
        static u8 columns[12][ScreenH * 4];
        static int cachedFormat = -1;
        static bool cachedReverse = false;
        const bool reverse = screen.stepY < 0;
        const s32 bpp = (s32)BytesPerPixel(screen.format);
        if (cachedFormat != (int)screen.format || cachedReverse != reverse)
        {
            for (int phase = 0; phase < 12; phase++)
                WriteBandColumn(columns[phase], phase, screen.format, reverse);
            cachedFormat = (int)screen.format;
            cachedReverse = reverse;
        }
        const bool columnar = Columnar(screen);
        const auto band = [&](int begin, int end, bool left) {
            for (int x = begin; x < end; x++)
            {
                const int distance = left ? range.begin - 1 - x : x - range.end;
                const int phase = distance < 3 ? 11 - distance
                                : screen.pillarbox == Pillarbox::Pattern ? x & 7 : 8;
                u8 *dst = screen.origin + x * screen.stepX;
                if (columnar)
                    std::memcpy(dst + (reverse ? ScreenH - 1 : 0) * screen.stepY,
                                columns[phase], (size_t)(ScreenH * bpp));
                else
                    for (int y = 0; y < ScreenH; y++)
                        std::memcpy(dst + y * screen.stepY,
                                    columns[phase] + (reverse ? ScreenH - 1 - y : y) * bpp, (size_t)bpp);
            }
        };
        band(0, range.begin, true);
        band(range.end, ScreenW, false);
    }

    size_t Layer::BytesFor(const Surface &like, const Rect &area)
    {
        return (size_t)(area.w * area.h) * BytesPerPixel(like.format);
    }

    bool Layer::Setup(const Surface &like, const Rect &area, s32 radius, u8 *buffer, size_t size)
    {
        Hide();
        m_hiRes = m_preferHiRes = false;
        m_begun = false;
        m_ready = false;
        if (area.Empty() || area.w > MaxW || area.x < 0 || area.y < 0 || area.Right() > like.width
            || area.Bottom() > like.height || size < BytesFor(like, area))
            return false;

        const s32 bpp = (s32)BytesPerPixel(like.format);
        m_surface = {};
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
        m_glyphs = GetSettings().glyphs;
        m_ready = true;
        return true;
    }

    HiResBackend Layer::s_hiRes = {};

    void Layer::Hide(void)
    {
        if (m_hiRes && m_slot < HiResSlots && s_owners[m_slot] == this)
        {
            if (s_hiRes.hide != nullptr) s_hiRes.hide(m_slot);
            s_owners[m_slot] = nullptr;
        }
    }

    bool Layer::SetupHiRes(const Surface &like, const Rect &area, s32 radius, u32 slot, u8 *fallback, size_t size)
    {
        const bool legacy = Setup(like, area, radius, fallback, size);
        m_preferHiRes = true;
        m_slot = slot;
        if (slot >= HiResSlots || like.width != ScreenW || s_hiRes.ready == nullptr || !s_hiRes.ready()) return legacy;
        Surface pixels = {};
        if (area.Empty() || area.x < 0 || area.y < 0 || area.Right() > like.width || area.Bottom() > like.height) return false;
        if (!s_hiRes.setup(slot, (s32)std::lround(area.w * HiResScale),
                                      (s32)std::lround(area.h * HiResScale), pixels)) return legacy;
        m_surface = pixels;
        m_area = area;
        m_likeStepX = like.stepX; m_likeStepY = like.stepY;
        m_glyphs = GetSettings().glyphs;
        m_ready = true;
        m_hiRes = true;
        s_owners[slot] = this;
        m_generation = s_hiRes.generation != nullptr ? s_hiRes.generation() : 0;
        m_radius = std::max<s32>(0, std::min(radius, std::min(area.w, area.h) / 2));
        m_dirty = true;
        return true;
    }

    bool Layer::Matches(const Surface &screen) const
    {
        if (m_glyphs != GetSettings().glyphs) return false;
        const bool available = m_preferHiRes && s_hiRes.ready != nullptr && s_hiRes.ready();
        if (m_preferHiRes && m_hiRes != available) return false;
        if (m_hiRes && s_hiRes.generation != nullptr && m_generation != s_hiRes.generation()) return false;
        if (m_hiRes && s_hiRes.valid != nullptr && !s_hiRes.valid(m_slot, m_surface)) return false;
        if (m_hiRes) return m_ready && available && s_owners[m_slot] == this && screen.width == ScreenW && m_area.Bottom() <= screen.height
            && screen.stepX == m_likeStepX && screen.stepY == m_likeStepY;
        return m_ready && screen.format == m_surface.format && screen.stepX == m_likeStepX
            && screen.stepY == m_likeStepY && m_area.Right() <= screen.width && m_area.Bottom() <= screen.height;
    }

    Canvas Layer::Begin(void)
    {
        Canvas canvas(m_surface, m_area.x, m_area.y, m_hiRes ? HiResScale : 1.f);
        if (m_hiRes)
        {
            m_begun = true;
            for (s32 y = 0; y < m_surface.height; y++)
                std::memset(m_surface.origin + y * m_surface.stepY, 0, (size_t)m_surface.width * 4);
            m_dirty = true;
            return canvas;
        }
        // Black is all-zero bytes in every format but RGBA8 (alpha byte 0xFF).
        if (m_surface.format == PixelFormat::RGBA8)
            canvas.FillRect(m_area, Rgb(0, 0, 0));
        else
            std::memset(m_buffer, 0, BytesFor(m_surface, m_area));
        return canvas;
    }

    bool Layer::Present(const Surface &screen) const
    {
        return Present(screen, Rect{ 0, 0, 0, 0 });
    }

    Layer::GpuPresentFn Layer::s_gpuPresent = nullptr;

    bool Layer::Present(const Surface &screen, const Rect &skip) const
    {
        // Matches asks callers to retry hi-res allocation on the next frame.
        // A failed allocation still has a valid legacy surface for THIS frame.
        const bool legacyFallback = m_preferHiRes && !m_hiRes && m_ready
            && screen.format == m_surface.format && screen.stepX == m_likeStepX
            && screen.stepY == m_likeStepY && m_area.Right() <= screen.width && m_area.Bottom() <= screen.height;
        if (!Matches(screen) && !legacyFallback)
            return false;
        if (m_hiRes)
        {
            if (!m_begun) return false;
            if (m_dirty && m_radius > 0)
            {
                // Clip even callers which fill a square background to the layer's silhouette.
                const float rad = m_radius * HiResScale;
                const s32 corner = (s32)std::ceil(rad + .5f);
                for (s32 cy = 0; cy < corner; cy++)
                    for (s32 cx = 0; cx < corner; cx++)
                    for (int quadrant = 0; quadrant < 4; quadrant++)
                    {
                        const s32 x = quadrant & 1 ? m_surface.width - 1 - cx : cx;
                        const s32 y = quadrant & 2 ? m_surface.height - 1 - cy : cy;
                        const float dx = std::max(0.f, std::max(rad - x - .5f, x + .5f - (m_surface.width - rad)));
                        const float dy = std::max(0.f, std::max(rad - y - .5f, y + .5f - (m_surface.height - rad)));
                        const float coverage = std::min(1.f, std::max(0.f, rad - std::sqrt(dx * dx + dy * dy) + .5f));
                        u8 &alpha = m_surface.origin[y * m_surface.stepY + x * 4];
                        alpha = std::min(alpha, (u8)(coverage * 255.f + .5f));
                    }
            }
            if (m_dirty && !skip.Empty())
            {
                // The game map is copied to the framebuffer later by its OSD
                // callback. Keep this window transparent in the earlier target draw.
                const Rect r = Intersect(skip, m_area);
                const s32 x0 = (s32)std::floor((r.x - m_area.x) * HiResScale);
                const s32 x1 = (s32)std::ceil((r.Right() - m_area.x) * HiResScale);
                const s32 y0 = (s32)std::floor((r.y - m_area.y) * HiResScale);
                const s32 y1 = (s32)std::ceil((r.Bottom() - m_area.y) * HiResScale);
                if (!r.Empty())
                    for (s32 y = y0; y < y1; y++)
                        for (s32 x = x0; x < x1; x++)
                            m_surface.origin[y * m_surface.stepY + x * 4] = 0;
            }
            if (s_hiRes.bands != nullptr) s_hiRes.bands(m_slot, screen, m_area);
            if (s_hiRes.present(m_slot, m_surface, m_area, m_dirty)) m_dirty = false;
            return false;
        }
        if (s_gpuPresent != nullptr && s_gpuPresent(*this, screen, skip))
            return false;
        PresentBands(screen, PillarboxColumns(screen, m_area));
        const s32 bpp = (s32)BytesPerPixel(screen.format);
        const bool columnar = Columnar(screen);
        // Rows [y0, y1) of column i (area coordinates).
        const auto copy = [&](s32 destination, s32 i, s32 y0, s32 y1) {
            if (y1 <= y0)
                return;
            const u8 *src = m_surface.origin + i * m_surface.stepX;
            u8 *dst = screen.origin + destination * screen.stepX + m_area.y * screen.stepY;
            if (columnar)
            {
                // The run starts at whichever end of the column is lower in memory.
                const s32 first = screen.stepY < 0 ? y1 - 1 : y0;
                std::memcpy(dst + first * screen.stepY, src + first * m_surface.stepY, (size_t)((y1 - y0) * bpp));
                return;
            }
            for (s32 y = y0; y < y1; y++)
                std::memcpy(dst + y * screen.stepY, src + y * m_surface.stepY, (size_t)bpp);
        };
        const float squeeze = screen.width == ScreenW ? screen.squeeze : 1.0f;
        const s32 begin = squeeze > 1.0f ? 0 : m_area.x;
        const s32 end = squeeze > 1.0f ? screen.width : m_area.Right();
        for (s32 destination = begin; destination < end; destination++)
        {
            const s32 x = SourceColumn(destination, squeeze);
            if (x < m_area.x || x >= m_area.Right())
                continue;
            const s32 i = x - m_area.x;
            const s32 y0 = m_top[i], y1 = m_area.h - m_top[i];
            if (skip.Empty() || x < skip.x || x >= skip.Right())
            {
                copy(destination, i, y0, y1);
                continue;
            }
            const s32 s0 = skip.y - m_area.y, s1 = skip.Bottom() - m_area.y;
            copy(destination, i, y0, s0 < y1 ? s0 : y1);
            copy(destination, i, s1 > y0 ? s1 : y0, y1);
        }
        return true;
    }
}
}
