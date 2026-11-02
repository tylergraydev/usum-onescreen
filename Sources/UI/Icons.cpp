#include "UI/Icons.hpp"

#include <algorithm>

namespace OneScreen
{
namespace UI
{
    void DrawIcon(Canvas &canvas, Icon icon, const Rect &box, Color c)
    {
        const float s = (float)std::min(box.w, box.h);
        const float ox = box.x + (box.w - s) * 0.5f, oy = box.y + (box.h - s) * 0.5f;
        // Icon shapes are written in a 0..1 square.
        auto X = [&](float u) { return ox + u * s; };
        auto Y = [&](float v) { return oy + v * s; };
        const float stroke = std::max(1.5f, s / 7.f);

        switch (icon)
        {
        case Icon::ArrowLeft:
            canvas.FillTriangle(X(0.70f), Y(0.20f), X(0.70f), Y(0.80f), X(0.25f), Y(0.50f), c);
            break;
        case Icon::ArrowRight:
            canvas.FillTriangle(X(0.30f), Y(0.20f), X(0.30f), Y(0.80f), X(0.75f), Y(0.50f), c);
            break;
        case Icon::ArrowUp:
            canvas.FillTriangle(X(0.20f), Y(0.70f), X(0.80f), Y(0.70f), X(0.50f), Y(0.25f), c);
            break;
        case Icon::ArrowDown:
            canvas.FillTriangle(X(0.20f), Y(0.30f), X(0.80f), Y(0.30f), X(0.50f), Y(0.75f), c);
            break;
        case Icon::Check:
            canvas.Line(X(0.18f), Y(0.52f), X(0.42f), Y(0.76f), stroke, c);
            canvas.Line(X(0.42f), Y(0.76f), X(0.84f), Y(0.26f), stroke, c);
            break;
        case Icon::Close:
            canvas.Line(X(0.24f), Y(0.24f), X(0.76f), Y(0.76f), stroke, c);
            canvas.Line(X(0.76f), Y(0.24f), X(0.24f), Y(0.76f), stroke, c);
            break;
        }
    }
}
}
