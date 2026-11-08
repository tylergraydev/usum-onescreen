#pragma once

#include <cstddef>

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    // Pure band pixels in memory order. phase: 0..7 stripes, 8 tint,
    // 9..11 soft edges. column must hold ScreenH * BytesPerPixel(format).
    void WriteBandColumn(u8 *column, int phase, PixelFormat format, bool reverse);

    // One raster scale for opt-in overlays. Other layers remain exactly 1x.
    static constexpr float HiResScale = 2.5f;
    enum HiResSlot : u32 { MenuSlot, RotomSlot, BadgeSlot, MoveLearnSlot, HiResSlots };
    struct HiResBackend
    {
        bool (*ready)(void);
        bool (*setup)(u32 slot, s32 width, s32 height, Surface &pixels);
        bool (*present)(u32 slot, const Surface &pixels, const Rect &area, bool dirty);
        void (*hide)(u32 slot);
        u32 (*generation)(void) = nullptr; // destructive debug texture changes
        void (*bands)(u32 slot, const Surface &screen, const Rect &area) = nullptr;
        bool (*valid)(u32 slot, const Surface &pixels) = nullptr;
    };

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
        // Opt-in, with the same supplied 1x buffer as a live fallback.
        bool SetupHiRes(const Surface &like, const Rect &area, s32 radius, u32 slot, u8 *fallback, size_t size);
        void Hide(void);
        static HiResBackend s_hiRes;
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
        // Full-screen areas also write cached pillarbox columns outside their
        // mapped horizontal extent, according to screen.pillarbox.
        // Both return true if the CPU wrote to the screen (false: nothing
        // written, or a GPU present, so the OSD callback can report no
        // CPU drawing and CTRPF leaves the framebuffer alone).
        bool   Present(const Surface &screen) const;
        // The same, leaving the pixels inside `skip` (screen coordinates)
        // alone, for something else to fill (a GPU copy, Screens/Map.cpp).
        // skip is unsqueezed: test the same source column as the layer.
        bool   Present(const Surface &screen, const Rect &skip) const;

        // The layer's own pixels, for a GPU present (UI/LayerGpu.cpp).
        const Surface &Pixels(void) const { return m_surface; }

        // Set by the plugin (UI/LayerGpu.cpp): presents the layer with GPU
        // transfers instead of CPU writes, so Azahar keeps the frame at its
        // internal resolution. Returns false to fall back to the CPU copy.
        using GpuPresentFn = bool (*)(const Layer &layer, const Surface &screen, const Rect &skip);
        static GpuPresentFn s_gpuPresent;

    private:
        static constexpr s32 MaxW = ScreenW;

        Surface m_surface = {};
        u8     *m_buffer = nullptr;
        Rect    m_area = {};
        s32     m_likeStepX = 0, m_likeStepY = 0;
        // Rows [m_top[i], m_area.h - m_top[i]) of column i are copied.
        u8      m_top[MaxW] = {};
        bool    m_ready = false;
        bool    m_hiRes = false, m_preferHiRes = false;
        u32     m_slot = 0, m_generation = 0;
        s32     m_radius = 0;
        mutable bool m_dirty = false;
        bool m_begun = false;
    };
}
}
