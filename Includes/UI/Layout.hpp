#pragma once

#include <algorithm>

#include "UI/Raster.hpp"

namespace OneScreen
{
namespace UI
{
    inline Rect Inset(const Rect &r, s32 left, s32 top, s32 right, s32 bottom)
    {
        return Rect{ r.x + left, r.y + top, std::max<s32>(0, r.w - left - right), std::max<s32>(0, r.h - top - bottom) };
    }

    inline Rect Inset(const Rect &r, s32 pad)
    {
        return Inset(r, pad, pad, pad, pad);
    }

    inline Rect CenterIn(const Rect &outer, s32 w, s32 h)
    {
        return Rect{ outer.x + (outer.w - w) / 2, outer.y + (outer.h - h) / 2, w, h };
    }

    // Hands out consecutive slices of an area, top to bottom (Column) or left
    // to right (Row), with a gap between slices. Slices can also be taken from
    // the far end, for footers or right-aligned items; Rest() is what is left.
    class Stack
    {
    public:
        enum Direction { Column, Row };

        Stack(const Rect &area, Direction dir, s32 gap = 0) : m_rest(area), m_dir(dir), m_gap(gap) {}

        Rect Next(s32 size)
        {
            Rect out = m_rest;
            if (m_dir == Column)
            {
                out.h = std::min(size, m_rest.h);
                Shrink(out.h, true);
            }
            else
            {
                out.w = std::min(size, m_rest.w);
                Shrink(out.w, true);
            }
            return out;
        }

        Rect NextFromEnd(s32 size)
        {
            Rect out = m_rest;
            if (m_dir == Column)
            {
                out.h = std::min(size, m_rest.h);
                out.y = m_rest.Bottom() - out.h;
                Shrink(out.h, false);
            }
            else
            {
                out.w = std::min(size, m_rest.w);
                out.x = m_rest.Right() - out.w;
                Shrink(out.w, false);
            }
            return out;
        }

        const Rect &Rest(void) const { return m_rest; }

    private:
        void Shrink(s32 used, bool fromStart)
        {
            const s32 take = std::min(used + m_gap, m_dir == Column ? m_rest.h : m_rest.w);
            if (m_dir == Column)
            {
                if (fromStart) m_rest.y += take;
                m_rest.h -= take;
            }
            else
            {
                if (fromStart) m_rest.x += take;
                m_rest.w -= take;
            }
        }

        Rect      m_rest;
        Direction m_dir;
        s32       m_gap;
    };
}
}
