#pragma once

#include "UI/Raster.hpp"

namespace OneScreen { namespace UI {
    // Four live layers; the rightmost twelve columns hold the band phases.
    // One unused texel separates allocations, including staging surfaces.
    class QuadAllocator
    {
    public:
        static constexpr u32 Slots = 4;
        static constexpr s32 Width = 1011, Height = 1024;
        Rect regions[Slots] = {};

        void Release(u32 slot) { if (slot < Slots) regions[slot] = {}; }
        bool Allocate(u32 slot, s32 w, s32 h)
        {
            if (slot >= Slots) return false;
            Release(slot);
            if (w <= 0 || h <= 0 || w > Width || h > Height) return false;
            // Every bottom-left packing candidate touches an atlas edge or
            // another rectangle's edge. Small fixed search, no heap/work queue.
            for (u32 yi = 0; yi <= Slots; yi++)
                for (u32 xi = 0; xi <= Slots; xi++)
                {
                    if ((xi && regions[xi - 1].Empty()) || (yi && regions[yi - 1].Empty())) continue;
                    const s32 x = xi ? regions[xi - 1].Right() + 1 : 0;
                    const s32 y = yi ? regions[yi - 1].Bottom() + 1 : 0;
                    const Rect candidate{ x, y, w, h };
                    if (candidate.Right() > Width || candidate.Bottom() > Height) continue;
                    bool free = true;
                    for (const Rect &r : regions)
                        if (!r.Empty() && candidate.x <= r.Right() && candidate.Right() >= r.x
                            && candidate.y <= r.Bottom() && candidate.Bottom() >= r.y) free = false;
                    if (free) { regions[slot] = candidate; return true; }
                }
            return false;
        }
    };
} }
