#pragma once

#include <cstddef>

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    // An off-screen copy of one rounded-rect area of a screen. Draw into it
    // only when its content changes (Begin), and copy it onto the framebuffer
    // every frame (Present). Present only writes: reading the framebuffer
    // makes Azahar sync it from the GPU, which costs host speed (docs/PERF.md),
    // so pixels outside the rounded rect are left alone rather than blended.
    //
    // The buffer is laid out like the screen it was set up for (same format,
    // same pixel order), so Present is one memcpy per screen column on the
    // 3DS's rotated framebuffers.
    class Layer
    {
    public:
        // area is in screen coordinates; corners with less than half
        // coverage of radius are not copied. The buffer must hold
        // BytesFor(like, area) bytes. Returns false if it doesn't fit.
        bool Setup(const Surface &like, const Rect &area, s32 radius, u8 *buffer, size_t size);
        static size_t BytesFor(const Surface &like, const Rect &area);

        // True if the layer was set up for a screen with this format and
        // layout; if not (or never set up), Setup again and redraw.
        bool Matches(const Surface &screen) const;

        const Rect &Area(void) const { return m_area; }

        // A canvas in screen coordinates, clipped to the area and cleared to
        // black (what antialiased edges at the corners blend against).
        Canvas Begin(void);
        // Copies the layer onto screen (which Matches it). Top (400px)
        // surfaces use screen.squeeze about x=200, sampling source corners.
        void   Present(const Surface &screen) const;
        // The same, leaving the pixels inside `skip` (screen coordinates)
        // alone, for something else to fill (a GPU copy, Screens/Map.cpp).
        // skip is unsqueezed: test the same source column as the layer.
        void   Present(const Surface &screen, const Rect &skip) const;

    private:
        static constexpr s32 MaxW = ScreenW;

        Surface m_surface = {};
        u8     *m_buffer = nullptr;
        Rect    m_area = {};
        s32     m_likeStepX = 0, m_likeStepY = 0;
        // Rows [m_top[i], m_area.h - m_top[i]) of column i are copied.
        u8      m_top[MaxW] = {};
        bool    m_ready = false;
    };
}
}
