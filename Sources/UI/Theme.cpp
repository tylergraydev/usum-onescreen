#include "UI/Theme.hpp"

#include <algorithm>
#include <cmath>

#include "UI/GameAssets.hpp"
#include "UI/Layout.hpp"

namespace OneScreen
{
namespace UI
{
namespace Theme
{
    // --- Colour helpers ---

    static Color Lerp(Color a, Color b, float t)
    {
        t = std::min(1.f, std::max(0.f, t));
        return Rgb((u8)std::lround(a.r + (b.r - a.r) * t), (u8)std::lround(a.g + (b.g - a.g) * t),
                   (u8)std::lround(a.b + (b.b - a.b) * t), (u8)std::lround(a.a + (b.a - a.a) * t));
    }

    // Horizontal inset of a rounded rect's edge on row `dy` (0 = top row).
    static s32 CornerInset(const Rect &r, s32 radius, s32 dy)
    {
        radius = std::min(radius, std::min(r.w, r.h) / 2);
        const s32 fromEdge = std::min(dy, r.h - 1 - dy);
        if (radius <= 0 || fromEdge >= radius)
            return 0;
        const float d = radius - fromEdge - 0.5f;
        return (s32)std::lround(radius - std::sqrt(std::max(0.f, (float)radius * radius - d * d)));
    }

    // A rounded rect filled top to bottom: top -> mid over the first half,
    // mid -> bottom over the second. Edges are not antialiased: draw it
    // inside an antialiased outline.
    static void FillGradient(Canvas &canvas, const Rect &r, s32 radius, Color top, Color mid, Color bottom)
    {
        for (s32 dy = 0; dy < r.h; dy++)
        {
            const float t = r.h > 1 ? (float)dy / (r.h - 1) : 0.f;
            const Color c = t < 0.5f ? Lerp(top, mid, t * 2) : Lerp(mid, bottom, t * 2 - 1);
            const s32 in = CornerInset(r, radius, dy);
            canvas.HLine(r.x + in, r.y + dy, r.w - 2 * in, c);
        }
    }

    // One row of a rounded rect (for lines inside it).
    static void RowLine(Canvas &canvas, const Rect &r, s32 radius, s32 dy, Color c)
    {
        if (dy < 0 || dy >= r.h)
            return;
        const s32 in = CornerInset(r, radius, dy);
        canvas.HLine(r.x + in, r.y + dy, r.w - 2 * in, c);
    }

    // Draws an image tinted (multiplied) by `tint`; `rotate` turns it a
    // quarter left (the game's cursor texture points down).
    static void DrawTinted(Canvas &canvas, const Image &image, s32 x, s32 y, const Rect &src, Color tint, bool rotate)
    {
        const s32 w = rotate ? src.h : src.w, h = rotate ? src.w : src.h;
        for (s32 dy = 0; dy < h; dy++)
            for (s32 dx = 0; dx < w; dx++)
            {
                const s32 sx = rotate ? src.x + src.w - 1 - dy : src.x + dx;
                const s32 sy = rotate ? src.y + dx : src.y + dy;
                if (sx < 0 || sy < 0 || sx >= image.w || sy >= image.h)
                    continue;
                const u8 *p = &image.rgba[((size_t)sy * image.w + sx) * 4];
                if (p[3] == 0)
                    continue;
                canvas.BlendPixel(x + dx, y + dy, Rgb((u8)(p[0] * tint.r / 255), (u8)(p[1] * tint.g / 255), (u8)(p[2] * tint.b / 255)),
                                  (u8)(p[3] * tint.a / 255));
            }
    }

    static const Image *GameIconImage(GameIcon icon)
    {
        const GameAssets *assets = GetGameAssets();
        return assets != nullptr ? assets->Icon(icon) : nullptr;
    }

    // --- Text ---

    static GameTextCache s_body(1.0f);
    static GameTextCache s_small(0.8f);

    static GameTextCache *CacheFor(TextSize size)
    {
        const GameAssets *assets = GetGameAssets();
        const GameFont *font = assets != nullptr && assets->font.Ready() ? &assets->font : nullptr;
        GameTextCache &cache = size == TextSize::Small ? s_small : s_body;
        cache.SetFont(font);
        return font != nullptr ? &cache : nullptr;
    }

    // The system-font style used when the game font isn't loaded.
    static UI::TextStyle Fallback(const TextStyle &style)
    {
        UI::TextStyle s;
        s.font = style.size == TextSize::Small ? Font::Small : Font::System;
        s.scale = style.size == TextSize::Small ? 2 : 1;
        s.color = style.color;
        s.align = style.align;
        return s;
    }

    bool UsingGameFont(void)
    {
        return CacheFor(TextSize::Body) != nullptr;
    }

    s32 LineHeight(TextSize size)
    {
        GameTextCache *cache = CacheFor(size);
        if (cache != nullptr)
            return cache->LineHeight();
        return UI::LineHeight(Fallback(TextStyle{ size }));
    }

    static s32 Measure(GameTextCache &cache, const std::string &text)
    {
        s32 w = 0;
        for (size_t i = 0; i < text.size();)
            w += cache.Advance(NextCodePoint(text.data(), text.size(), i));
        return w;
    }

    s32 MeasureText(const std::string &text, TextSize size)
    {
        GameTextCache *cache = CacheFor(size);
        if (cache != nullptr)
            return Measure(*cache, text);
        return UI::MeasureText(text, Fallback(TextStyle{ size }));
    }

    static std::string Cut(GameTextCache &cache, const std::string &text, s32 maxWidth)
    {
        if (Measure(cache, text) <= maxWidth)
            return text;
        const std::string dots = "...";
        const s32 room = maxWidth - Measure(cache, dots);
        s32 w = 0;
        size_t end = 0;
        for (size_t i = 0; i < text.size();)
        {
            const size_t start = i;
            const s32 a = cache.Advance(NextCodePoint(text.data(), text.size(), i));
            if (w + a > room)
                break;
            w += a;
            end = i;
            (void)start;
        }
        return text.substr(0, end) + dots;
    }

    static void DrawRun(Canvas &canvas, GameTextCache &cache, const std::string &text, s32 x, s32 y, Color color)
    {
        for (size_t i = 0; i < text.size();)
        {
            const u32 cp = NextCodePoint(text.data(), text.size(), i);
            const Glyph *g = cache.Get(cp);
            if (g == nullptr)
                break;
            if (g->w > 0)
            {
                if (canvas.Scale() == 1.f)
                    canvas.DrawMask(x + g->left, y, g->mask, g->w, g->h, g->w, 1, color);
                else cache.Draw(canvas, cp, x, y, color);
            }
            x += g->advance;
        }
    }

    s32 DrawText(Canvas &canvas, const std::string &text, const Rect &box, const TextStyle &style)
    {
        const bool outlined = style.outline.a > 0;
        GameTextCache *cache = CacheFor(style.size);
        if (cache == nullptr)
        {
            UI::TextStyle s = Fallback(style);
            if (outlined)
            {
                UI::TextStyle o = s;
                o.color = style.outline;
                static const s32 offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
                for (const auto &d : offsets)
                    UI::DrawText(canvas, text, Rect{ box.x + d[0], box.y + d[1], box.w, box.h }, o);
            }
            return UI::DrawText(canvas, text, box, s);
        }
        const std::string shown = Cut(*cache, text, box.w);
        const s32 w = Measure(*cache, shown);
        s32 x = box.x;
        if (style.align == Align::Center)
            x += (box.w - w) / 2;
        else if (style.align == Align::Right)
            x += box.w - w;
        const s32 y = box.y + (box.h - cache->LineHeight()) / 2;
        if (outlined)
        {
            static const s32 offsets[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } };
            for (const auto &d : offsets)
                DrawRun(canvas, *cache, shown, x + d[0], y + d[1], style.outline);
        }
        DrawRun(canvas, *cache, shown, x, y, style.color);
        return w;
    }

    // --- Shapes ---

    void DrawWindow(Canvas &canvas, const Rect &r)
    {
        // Measured on the summary's detail window: 2 px black, 1 px tan, a
        // 3 px bevel fading into the fill.
        canvas.FillRoundRect(r, WindowRadius, WindowLine);
        canvas.FillRoundRect(Inset(r, 2), WindowRadius - 1, WindowRim);
        canvas.FillRoundRect(Inset(r, 3), WindowRadius - 2, WindowBevel);
        canvas.FillRoundRect(Inset(r, 4), WindowRadius - 2, Lerp(WindowBevel, WindowFill, 0.5f));
        canvas.FillRoundRect(Inset(r, 5), WindowRadius - 3, WindowFill);
    }

    void DrawHighlightFrame(Canvas &canvas, const Rect &r, s32 radius)
    {
        canvas.StrokeRoundRect(r, radius, Highlight, 2);
    }

    void DrawRowBackground(Canvas &canvas, const Rect &r, RowKind kind)
    {
        const s32 radius = std::min(RowRadius, r.h / 2);
        if (kind == RowKind::List)
        {
            // Bag row, top to bottom: 2 px white rim, 2 px lime, white to
            // mint fill, 2 px dark green, 2 px light green.
            canvas.FillRoundRect(r, radius, Rgb(0xF2, 0xFF, 0xFF));
            canvas.FillRoundRect(Inset(r, 2), radius - 2, ListOutline);
            const Rect fill = Inset(r, 4, 4, 4, 2);
            FillGradient(canvas, fill, radius - 4, ListTop, ListMid, ListBottom);
            RowLine(canvas, fill, radius - 4, fill.h - 4, ListShadow);
            RowLine(canvas, fill, radius - 4, fill.h - 3, ListShadow);
            RowLine(canvas, fill, radius - 4, fill.h - 2, Rgb(0xAA, 0xEE, 0xAA));
            RowLine(canvas, fill, radius - 4, fill.h - 1, Rgb(0xAA, 0xEE, 0xAA));
        }
        else
        {
            // Menu option: 2 px black, white rim,
            // white to sky fill with a yellow glint on top, dark blue base.
            canvas.FillRoundRect(r, radius, OptionLine);
            canvas.FillRoundRect(Inset(r, 2), radius - 2, OptionTop);
            const Rect fill = Inset(r, 3, 3, 3, 3);
            FillGradient(canvas, fill, radius - 3, OptionTop, OptionMid, OptionBottom);
            const Color glint = Rgb(OptionGlint.r, OptionGlint.g, OptionGlint.b, 0xA0);
            for (s32 i = 1; i < 3; i++)
            {
                const s32 in = CornerInset(fill, radius - 3, i);
                canvas.FillRect(Rect{ fill.x + in + fill.w / 3, fill.y + i, fill.w - 2 * in - fill.w / 3, 1 }, glint);
            }
            RowLine(canvas, fill, radius - 3, fill.h - 4, OptionBase);
            RowLine(canvas, fill, radius - 3, fill.h - 3, OptionBase);
        }
    }

    void DrawRow(Canvas &canvas, const Rect &r, RowKind kind, const std::string &label, bool selected)
    {
        DrawRowBackground(canvas, r, kind);
        TextStyle style;
        style.color = kind == RowKind::List ? ListText : OptionText;
        style.align = kind == RowKind::List ? Align::Left : Align::Center;
        const Rect text = kind == RowKind::List ? Inset(r, RowTextInset, 2, 10, 4) : Inset(r, RowTextInset, 2, RowTextInset, 4);
        DrawText(canvas, label, text, style);
        if (selected)
            DrawCursor(canvas, r.x + 14, r.y + (r.h - 2) / 2);
    }

    void DrawCursor(Canvas &canvas, s32 tipX, s32 centerY)
    {
        const Image *icon = GameIconImage(GameIcon::Cursor);
        if (icon != nullptr)
        {
            // Common_Cursor_02 points down; the game turns it to point right.
            DrawTinted(canvas, *icon, tipX - icon->h + 1, centerY - icon->w / 2, Rect{ 0, 0, icon->w, icon->h },
                       Rgb(255, 255, 255), true);
            return;
        }
        const float x0 = (float)tipX - CursorW + 2, x1 = (float)tipX + 1, y = (float)centerY + 0.5f;
        canvas.FillTriangle(x0 - 1, y - 7.5f, x1 + 1, y, x0 - 1, y + 7.5f, CursorEdge);
        canvas.FillTriangle(x0, y - 6, x1 - 1, y, x0, y + 6, CursorRed);
    }

    static const char *ButtonLetter(Button b)
    {
        static const char *letters[] = { "A", "B", "X", "Y", "L", "R", "START", "SELECT" };
        return letters[(int)b];
    }

    static const Image *ButtonIcon(Button b)
    {
        switch (b)
        {
        case Button::A: return GameIconImage(GameIcon::ButtonA);
        case Button::B: return GameIconImage(GameIcon::ButtonB);
        case Button::X: return GameIconImage(GameIcon::ButtonX);
        case Button::Y: return GameIconImage(GameIcon::ButtonY);
        default:        return nullptr;
        }
    }

    // Width of the button's own badge (16 px circle for A/B/X/Y, a pill
    // with the name for the others).
    static s32 BadgeWidth(Button b)
    {
        if ((int)b <= (int)Button::Y)
            return 16;
        return UI::MeasureText(ButtonLetter(b), UI::TextStyle{ Font::Small, 1 }) + 8;
    }

    s32 PromptWidth(Button button, const std::string &label)
    {
        return 4 + BadgeWidth(button) + (label.empty() ? 0 : 4 + MeasureText(label, TextSize::Body)) + 6;
    }

    s32 DrawButtonPrompt(Canvas &canvas, s32 x, s32 y, Button button, const std::string &label)
    {
        const s32 w = PromptWidth(button, label);
        canvas.FillRoundRect(Rect{ x, y, w, PromptHeight }, PromptHeight / 2, PromptFill);
        const s32 bx = x + 4, by = y + (PromptHeight - 16) / 2;
        const Image *icon = ButtonIcon(button);
        if (icon != nullptr)
            DrawImage(canvas, *icon, bx, by);
        else
        {
            const Rect badge{ bx, by, BadgeWidth(button), 16 };
            canvas.FillRoundRect(badge, 8, Rgb(0xFF, 0xFF, 0xFF));
            canvas.FillRoundRect(Inset(badge, 1), 7, Rgb(0x10, 0x10, 0x10));
            UI::TextStyle s;
            s.font = Font::Small;
            s.color = Rgb(0xFF, 0xFF, 0xFF);
            s.align = Align::Center;
            UI::DrawText(canvas, ButtonLetter(button), badge, s);
        }
        if (!label.empty())
        {
            TextStyle s;
            s.color = PromptText;
            DrawText(canvas, label, Rect{ bx + BadgeWidth(button) + 4, y, w - BadgeWidth(button) - 8, PromptHeight }, s);
        }
        return w;
    }

    // --- Gauges ---

    GaugeLevel HpLevel(u32 value, u32 max)
    {
        if (value == 0 || max == 0)
            return GaugeLevel::Empty;
        if (value * 2 > max)
            return GaugeLevel::High;
        if (value * 5 > max)
            return GaugeLevel::Half;
        return GaugeLevel::Low;
    }

    GaugeLevel PpLevel(u32 value, u32 max)
    {
        if (value == 0 || max == 0)
            return GaugeLevel::Empty;
        if (value * 2 > max)
            return GaugeLevel::High;
        if (value * 4 > max)
            return GaugeLevel::Half;
        return GaugeLevel::Low;
    }

    s32 GaugeFill(u32 value, u32 max, s32 width)
    {
        if (max == 0 || value == 0 || width <= 0)
            return 0;
        if (value >= max)
            return width;
        const s32 fill = (s32)((uint64_t)value * width / max);
        return std::min(width - 1, std::max<s32>(1, fill));
    }

    static void DrawGauge(Canvas &canvas, const Rect &r, u32 value, u32 max, Color left, Color right)
    {
        canvas.FillRect(r, GaugeFrame);
        const Rect inner = Inset(r, 1);
        canvas.FillRect(inner, GaugeEmpty);
        const s32 fill = GaugeFill(value, max, inner.w);
        for (s32 i = 0; i < fill; i++)
            canvas.VLine(inner.x + i, inner.y, inner.h, Lerp(left, right, inner.w > 1 ? (float)i / (inner.w - 1) : 0.f));
    }

    void DrawHpBar(Canvas &canvas, const Rect &r, u32 hp, u32 maxHp)
    {
        switch (HpLevel(hp, maxHp))
        {
        case GaugeLevel::High:  DrawGauge(canvas, r, hp, maxHp, HpGreenL, HpGreenR); break;
        case GaugeLevel::Half:  DrawGauge(canvas, r, hp, maxHp, HpYellowL, HpYellowR); break;
        default:                DrawGauge(canvas, r, hp, maxHp, HpRedL, HpRedR); break;
        }
    }

    void DrawPpBar(Canvas &canvas, const Rect &r, u32 pp, u32 maxPp)
    {
        switch (PpLevel(pp, maxPp))
        {
        case GaugeLevel::High:  DrawGauge(canvas, r, pp, maxPp, HpGreenL, HpGreenR); break;
        case GaugeLevel::Half:  DrawGauge(canvas, r, pp, maxPp, HpYellowR, HpYellowL); break;
        default:                DrawGauge(canvas, r, pp, maxPp, HpRedL, HpRedR); break;
        }
    }

    Color PpTextColor(u32 pp, u32 maxPp)
    {
        switch (PpLevel(pp, maxPp))
        {
        case GaugeLevel::High:  return Rgb(0xFF, 0xFF, 0xFF);
        case GaugeLevel::Half:  return Rgb(0xFF, 0xFF, 0x1F);
        case GaugeLevel::Low:   return Rgb(0xFF, 0x9E, 0x37);
        default:                return Rgb(0xFF, 0x40, 0x40);
        }
    }

    // --- Types and moves ---

    // Battle_PLT_LOW_002_color_00..17 (a/0/6/7 entry 8): Window_main and
    // Window_line material colours (black, white), by type ID.
    static const TypeColors s_types[TypeCount] = {
        { Rgb(0x38, 0x34, 0x32), Rgb(0x9B, 0x9B, 0x82), Rgb(0x2B, 0x1B, 0x00), Rgb(0xCA, 0xC8, 0xBA) },  // Normal
        { Rgb(0x5B, 0x24, 0x1D), Rgb(0xA8, 0x80, 0x53), Rgb(0x2B, 0x00, 0x00), Rgb(0xD2, 0xB9, 0xA8) },  // Fighting
        { Rgb(0x19, 0x10, 0x78), Rgb(0x75, 0xBE, 0xFA), Rgb(0x06, 0x00, 0x2B), Rgb(0x8F, 0xB6, 0xF6) },  // Flying
        { Rgb(0x31, 0x19, 0x66), Rgb(0xCC, 0x57, 0xF4), Rgb(0x27, 0x00, 0x2B), Rgb(0xC3, 0x9E, 0xDD) },  // Poison
        { Rgb(0x5E, 0x51, 0x03), Rgb(0xE3, 0xCE, 0x31), Rgb(0x2B, 0x25, 0x00), Rgb(0xC8, 0xBF, 0x85) },  // Ground
        { Rgb(0x45, 0x28, 0x04), Rgb(0xA1, 0x91, 0x41), Rgb(0x2B, 0x17, 0x00), Rgb(0xC3, 0xC2, 0xA5) },  // Rock
        { Rgb(0x33, 0x3E, 0x01), Rgb(0xC4, 0xDA, 0x00), Rgb(0x1E, 0x25, 0x00), Rgb(0xC5, 0xD1, 0x8C) },  // Bug
        { Rgb(0x3F, 0x1E, 0x77), Rgb(0xA3, 0xA9, 0xC0), Rgb(0x16, 0x00, 0x31), Rgb(0x8D, 0x6D, 0xD8) },  // Ghost
        { Rgb(0x06, 0x1D, 0x31), Rgb(0x86, 0xA4, 0xB9), Rgb(0x00, 0x0D, 0x31), Rgb(0x9C, 0xAE, 0xBB) },  // Steel
        { Rgb(0x41, 0x0B, 0x00), Rgb(0xE4, 0x70, 0x32), Rgb(0x31, 0x00, 0x00), Rgb(0xD5, 0x94, 0x7E) },  // Fire
        { Rgb(0x00, 0x44, 0x5F), Rgb(0x17, 0xD1, 0xF2), Rgb(0x00, 0x2B, 0x31), Rgb(0x95, 0xF4, 0xFF) },  // Water
        { Rgb(0x0F, 0x4E, 0x00), Rgb(0xB8, 0xE4, 0x0A), Rgb(0x00, 0x25, 0x03), Rgb(0xB7, 0xF1, 0x9C) },  // Grass
        { Rgb(0x55, 0x58, 0x00), Rgb(0xDA, 0xDA, 0x00), Rgb(0x23, 0x25, 0x00), Rgb(0xF1, 0xF1, 0x53) },  // Electric
        { Rgb(0x5F, 0x07, 0x30), Rgb(0xEB, 0x4D, 0x77), Rgb(0x31, 0x00, 0x17), Rgb(0xFD, 0x47, 0x6E) },  // Psychic
        { Rgb(0x18, 0x2D, 0x37), Rgb(0x63, 0xEA, 0xD1), Rgb(0x00, 0x24, 0x1F), Rgb(0xBD, 0xF0, 0xFB) },  // Ice
        { Rgb(0x27, 0x00, 0x6D), Rgb(0x6D, 0x79, 0xE5), Rgb(0x19, 0x00, 0x32), Rgb(0xA7, 0x98, 0xFF) },  // Dragon
        { Rgb(0x47, 0x1B, 0x07), Rgb(0x93, 0x7E, 0x70), Rgb(0x1A, 0x0C, 0x13), Rgb(0xBF, 0x8D, 0x7C) },  // Dark
        { Rgb(0x6C, 0x02, 0x53), Rgb(0xFA, 0x85, 0xC5), Rgb(0x28, 0x00, 0x24), Rgb(0xF5, 0x89, 0xF7) },  // Fairy
    };

    TypeColors TypeColor(u8 type)
    {
        return s_types[type < TypeCount ? type : 0];
    }

    std::string Upper(const std::string &text)
    {
        std::string out = text;
        for (size_t i = 0; i < out.size(); i++)
        {
            const u8 c = (u8)out[i];
            if (c >= 'a' && c <= 'z')
                out[i] = (char)(c - 32);
            // UTF-8 Latin-1 lower case (C3 A0-BE, not C3 B7 "÷"): 0x20 down.
            else if (c == 0xC3 && i + 1 < out.size())
            {
                const u8 d = (u8)out[i + 1];
                if (d >= 0xA0 && d <= 0xBE && d != 0xB7)
                    out[i + 1] = (char)(d - 0x20);
                i++;
            }
        }
        return out;
    }

    void DrawTypePlate(Canvas &canvas, const Rect &r, u8 type, const std::string &name)
    {
        const TypeColors c = TypeColor(type);
        const s32 radius = r.h / 2;
        canvas.FillRoundRect(r, radius, c.lineDark);
        canvas.FillRoundRect(Inset(r, 1), radius - 1, Rgb(0xFF, 0xFF, 0xFF));
        const Rect fill = Inset(r, 2);
        FillGradient(canvas, fill, radius - 2, Lerp(c.light, Rgb(0xFF, 0xFF, 0xFF), 0.25f), c.light, Lerp(c.light, c.dark, 0.3f));
        TextStyle s;
        s.size = TextSize::Small;
        s.color = Rgb(0xFF, 0xFF, 0xFF);
        s.outline = c.lineDark;
        s.align = Align::Center;
        DrawText(canvas, Upper(name), Inset(r, 3, 0, 3, 0), s);
    }

    void DrawMoveButton(Canvas &canvas, const Rect &r, u8 type, bool selected)
    {
        // The battle move buttons: dark outline, a light line on top, a
        // lighter upper half and a darker lower half.
        const TypeColors c = TypeColor(type);
        const s32 radius = 6;
        canvas.FillRoundRect(r, radius, c.lineDark);
        const Rect inner = Inset(r, 1);
        canvas.FillRoundRect(inner, radius - 1, c.lineLight);
        const Rect fill = Inset(inner, 0, 1, 0, 0);
        const s32 split = fill.h * 11 / 20;
        for (s32 dy = 0; dy < fill.h; dy++)
        {
            const float lift = selected ? 0.08f : 0.f;
            const float t = lift + (dy < split ? 0.8f - 0.1f * dy / std::max<s32>(1, split - 1)
                                               : 0.15f + 0.15f * (dy - split) / std::max<s32>(1, fill.h - split - 1));
            const s32 in = CornerInset(fill, radius - 1, dy);
            canvas.HLine(fill.x + in, fill.y + dy, fill.w - 2 * in, Lerp(c.dark, c.light, t));
        }
    }

    void DrawCategoryIcon(Canvas &canvas, s32 x, s32 y, u8 category)
    {
        const Image *icon = GameIconImage(GameIcon::Categories);
        const Rect src = CategoryIconRect(category);
        if (icon != nullptr)
        {
            DrawImage(canvas, *icon, x, y, &src);
            return;
        }
        static const Color colors[3] = { Rgb(0x9A, 0x9A, 0x9A), Rgb(0xFF, 0x5A, 0x00), Rgb(0x2A, 0x6A, 0xD8) };
        const Rect plate{ x + 1, y + 1, src.w - 2, src.h - 2 };
        canvas.FillRoundRect(plate, 3, Rgb(0x20, 0x20, 0x20));
        canvas.FillRoundRect(Inset(plate, 1), 2, colors[category < 3 ? category : 0]);
    }

    void DrawGender(Canvas &canvas, s32 x, s32 y, bool female)
    {
        // The icons are white; the game tints them (male blue sampled from
        // the Party screen).
        const Color tint = female ? Rgb(0xFF, 0x40, 0x60) : Rgb(0x00, 0x9A, 0xFB);
        const Image *icon = GameIconImage(female ? GameIcon::Female : GameIcon::Male);
        if (icon != nullptr)
        {
            DrawTinted(canvas, *icon, x, y, Rect{ 0, 0, icon->w, icon->h }, tint, false);
            return;
        }
        canvas.StrokeRoundRect(Rect{ x + 1, y + 1, 8, 8 }, 4, tint, 2);
        if (female)
            canvas.FillRect(Rect{ x + 4, y + 9, 2, 3 }, tint);
        else
            canvas.Line(x + 7.5f, y + 3.5f, x + 11.f, y + 0.5f, 2.f, tint);
    }

    void DrawShiny(Canvas &canvas, s32 x, s32 y)
    {
        // Five tips joined through an inset pentagon, within 10x10.
        static const float px[10] = { 5, 6.2f, 9.8f, 7, 8, 5, 2, 3, 0.2f, 3.8f };
        static const float py[10] = { 0, 3.4f, 3.4f, 5.6f, 9.5f, 7.2f, 9.5f, 5.6f, 3.4f, 3.4f };
        for (int i = 0; i < 10; i++)
        {
            const int next = (i + 1) % 10;
            canvas.FillTriangle(x + 5.f, y + 5.f, x + px[i], y + py[i], x + px[next], y + py[next], CursorRed);
        }
    }

    bool DrawBall(Canvas &canvas, s32 x, s32 y, u16 ballItem)
    {
        const GameAssets *assets = GetGameAssets();
        const Image *ball = assets != nullptr ? assets->Ball(ballItem) : nullptr;
        if (ball == nullptr)
            return false;
        DrawImage(canvas, *ball, x, y);
        return true;
    }
}
}
}
