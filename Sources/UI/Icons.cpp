#include "UI/Icons.hpp"

#include <algorithm>

namespace OneScreen
{
namespace UI
{
    // Stroke alphabet in a 4x6 unit cell. Spaces lift the pen; pairs are x/y.
    // These are line paths, never font masks or bitmap assets.
    static const char *LetterPath(char c)
    {
        switch (c)
        {
        case 'A': return "0602204246 0444";
        case 'B': return "06003041423303 033344453606";
        case 'C': return "40000646";
        case 'D': return "06003042443606";
        case 'E': return "40000646 0343";
        case 'H': return "0006 4046 0343";
        case 'I': return "0040 2026 0646";
        case 'L': return "000646";
        case 'M': return "0600224046";
        case 'N': return "06004640";
        case 'O': return "0040460600";
        case 'P': return "0600404303";
        case 'R': return "0600404303 2346";
        case 'S': return "400003434606";
        case 'T': return "0040 2026";
        case 'U': return "00064640";
        case 'V': return "002640";
        case 'W': return "0006234640";
        case 'X': return "0046 4006";
        case 'Y': return "002340 2326";
        case 'Z': return "00400646";
        case '1': return "022026 0646";
        case '2': return "0040430646";
        case '+': return "0343 2026";
        case '-': return "0343";
        default: return "";
        }
    }

    static void VectorLabel(Canvas &canvas, const Rect &box, const char *name, Color color)
    {
        size_t count = 0;
        while (name[count]) ++count;
        if (count == 0) return;
        const float unit = std::min(1.25f, (box.w - 4.f) / (count * 6.f - 2.f));
        const float left = box.x + (box.w - (count * 6.f - 2.f) * unit) / 2.f;
        const float top = box.y + (box.h - 6.f * unit) / 2.f;
        for (size_t i = 0; i < count; ++i)
        {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            const char *path = LetterPath(c);
            bool pen = false;
            float px = 0, py = 0;
            while (*path)
            {
                if (*path == ' ') { pen = false; ++path; continue; }
                const float x = left + (i * 6.f + (path[0] - '0')) * unit;
                const float y = top + (path[1] - '0') * unit;
                if (pen) canvas.Line(px, py, x, y, std::max(0.7f, unit), color);
                px = x; py = y; pen = true; path += 2;
            }
        }
    }

    void DrawControllerBadge(Canvas &canvas, const Rect &box, Theme::Button button,
                             GlyphFamily family, const char *name)
    {
        const Color white = Rgb(255, 255, 255), dark = Rgb(16, 16, 16);
        const bool face = (unsigned)button <= (unsigned)Theme::Button::Y;
        canvas.FillRoundRect(box, 8, white);
        canvas.FillRoundRect(Rect{ box.x + 1, box.y + 1, box.w - 2, box.h - 2 }, 7, dark);
        if (family == GlyphFamily::PlayStation && face)
        {
            const s32 sx = box.x + (box.w - 16) / 2;
            const float x = (float)sx, y = (float)box.y;
            switch (button)
            {
            case Theme::Button::A:
                canvas.StrokeRoundRect(Rect{ sx + 4, box.y + 4, 8, 8 }, 4, Rgb(255, 100, 110), 2);
                break;
            case Theme::Button::B:
                canvas.Line(x + 4, y + 4, x + 12, y + 12, 1.6f, Rgb(110, 175, 255));
                canvas.Line(x + 12, y + 4, x + 4, y + 12, 1.6f, Rgb(110, 175, 255));
                break;
            case Theme::Button::X:
                canvas.Line(x + 8, y + 3, x + 13, y + 12, 1.5f, Rgb(80, 225, 170));
                canvas.Line(x + 13, y + 12, x + 3, y + 12, 1.5f, Rgb(80, 225, 170));
                canvas.Line(x + 3, y + 12, x + 8, y + 3, 1.5f, Rgb(80, 225, 170));
                break;
            case Theme::Button::Y:
                canvas.StrokeRect(Rect{ sx + 4, box.y + 4, 9, 9 }, Rgb(240, 140, 210), 2);
                break;
            default: break;
            }
            return;
        }
        Color ink = white;
        if (family == GlyphFamily::Xbox && face)
        {
            switch (button)
            {
            case Theme::Button::A: ink = Rgb(230, 65, 65); break;   // physical B
            case Theme::Button::B: ink = Rgb(70, 200, 95); break;   // physical A
            case Theme::Button::X: ink = Rgb(245, 210, 50); break;  // physical Y
            case Theme::Button::Y: ink = Rgb(65, 145, 245); break;  // physical X
            default: break;
            }
            canvas.FillRoundRect(Rect{ box.x + 1, box.y + 1, box.w - 2, box.h - 2 }, 7, ink);
            ink = dark;
        }
        VectorLabel(canvas, box, name, ink);
    }

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
