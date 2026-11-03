#include "Screens/KeyboardGrid.hpp"

#include <cstdlib>

#include "UI/Layout.hpp"
#include "UI/Text.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using UI::Rgb;

    const std::vector<KeyboardKey> &KeyboardKeys(void)
    {
        // The game's QWERTY page, row by row, left to right. Characters as
        // read back from the game's text object after tapping each key with
        // shift off and on (docs/KEYBOARD.md).
        static const std::vector<KeyboardKey> keys =
        {
            { "excl", KeyKind::Char, '!', '!', 0 },
            { "1", KeyKind::Char, '1', GameMale, 0 },
            { "2", KeyKind::Char, '2', GameFemale, 0 },
            { "3", KeyKind::Char, '3', '+', 0 },
            { "4", KeyKind::Char, '4', '-', 0 },
            { "5", KeyKind::Char, '5', '*', 0 },
            { "6", KeyKind::Char, '6', '/', 0 },
            { "7", KeyKind::Char, '7', '=', 0 },
            { "8", KeyKind::Char, '8', '%', 0 },
            { "9", KeyKind::Char, '9', '(', 0 },
            { "0", KeyKind::Char, '0', ')', 0 },
            { "delete", KeyKind::Delete, 0, 0, 0 },

            { "q", KeyKind::Char, 'q', 'Q', 1 },
            { "w", KeyKind::Char, 'w', 'W', 1 },
            { "e", KeyKind::Char, 'e', 'E', 1 },
            { "r", KeyKind::Char, 'r', 'R', 1 },
            { "t", KeyKind::Char, 't', 'T', 1 },
            { "y", KeyKind::Char, 'y', 'Y', 1 },
            { "u", KeyKind::Char, 'u', 'U', 1 },
            { "i", KeyKind::Char, 'i', 'I', 1 },
            { "o", KeyKind::Char, 'o', 'O', 1 },
            { "p", KeyKind::Char, 'p', 'P', 1 },
            { "ok", KeyKind::Ok, 0, 0, 1 },

            { "a", KeyKind::Char, 'a', 'A', 2 },
            { "s", KeyKind::Char, 's', 'S', 2 },
            { "d", KeyKind::Char, 'd', 'D', 2 },
            { "f", KeyKind::Char, 'f', 'F', 2 },
            { "g", KeyKind::Char, 'g', 'G', 2 },
            { "h", KeyKind::Char, 'h', 'H', 2 },
            { "j", KeyKind::Char, 'j', 'J', 2 },
            { "k", KeyKind::Char, 'k', 'K', 2 },
            { "l", KeyKind::Char, 'l', 'L', 2 },
            { "apostrophe", KeyKind::Char, 0x2019, 0x2019, 2 },
            { "colon", KeyKind::Char, ':', ':', 2 },
            { "e_acute", KeyKind::Char, 0x00E9, 0x00C9, 2 },

            { "z", KeyKind::Char, 'z', 'Z', 3 },
            { "x", KeyKind::Char, 'x', 'X', 3 },
            { "c", KeyKind::Char, 'c', 'C', 3 },
            { "v", KeyKind::Char, 'v', 'V', 3 },
            { "b", KeyKind::Char, 'b', 'B', 3 },
            { "n", KeyKind::Char, 'n', 'N', 3 },
            { "m", KeyKind::Char, 'm', 'M', 3 },
            { "comma", KeyKind::Char, ',', ',', 3 },
            { "period", KeyKind::Char, '.', '.', 3 },
            { "question", KeyKind::Char, '?', '?', 3 },
            { "quote_open", KeyKind::Char, 0x201C, 0x201C, 3 },
            { "quote_close", KeyKind::Char, 0x201D, 0x201D, 3 },

            { "space", KeyKind::Space, ' ', ' ', 4 },
            { "shift", KeyKind::Shift, 0, 0, 4 },
        };
        return keys;
    }

    int KeyboardFind(const std::string &name)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        for (size_t i = 0; i < keys.size(); i++)
            if (name == keys[i].name)
                return (int)i;
        return -1;
    }

    static std::string ButtonName(const KeyboardKey &key)
    {
        return std::string("strinput.") + key.name;
    }

    UI::Rect KeyboardGameRect(int index)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        if (index < 0 || index >= (int)keys.size())
            return UI::Rect{ 0, 0, 0, 0 };
        const LayoutButton *b = FindLayoutButton(ButtonName(keys[index]).c_str());
        if (b == nullptr)
            return UI::Rect{ 0, 0, 0, 0 };
        return UI::Rect{ b->x0, b->y0, b->x1 - b->x0 + 1, b->y1 - b->y0 + 1 };
    }

    static int CentreX2(int index)
    {
        const UI::Rect r = KeyboardGameRect(index);
        return 2 * r.x + r.w;
    }

    int KeyboardMove(int from, int dx, int dy)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        if (from < 0 || from >= (int)keys.size())
            return 0;
        const int row = keys[from].row;
        int first = -1, last = -1;
        for (int i = 0; i < (int)keys.size(); i++)
            if (keys[i].row == row)
            {
                if (first < 0)
                    first = i;
                last = i;
            }

        if (dx != 0)
        {
            // Keys of a row are consecutive in the table, left to right.
            const int n = last - first + 1;
            return first + ((from - first + (dx > 0 ? 1 : -1)) % n + n) % n;
        }
        if (dy == 0)
            return from;

        const int rows = keys.back().row + 1;
        const int target = ((row + (dy > 0 ? 1 : -1)) % rows + rows) % rows;
        // Neighbouring rows are offset by half a key, so two keys are often
        // equally near: going down takes the right one, going up the left
        // one, so up then down comes back to the same key.
        const int cx = CentreX2(from);
        int best = from, bestDist = 0;
        for (int i = 0; i < (int)keys.size(); i++)
        {
            if (keys[i].row != target)
                continue;
            const int d = std::abs(CentreX2(i) - cx);
            if (best == from || d < bestDist || (d == bestDist && dy > 0))
            {
                best = i;
                bestDist = d;
            }
        }
        return best;
    }

    static void AppendUtf8(std::string &out, u32 c)
    {
        if (c < 0x80)
            out += (char)c;
        else if (c < 0x800)
        {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
        else
        {
            out += (char)(0xE0 | (c >> 12));
            out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        }
    }

    std::string KeyboardUtf8(const u16 *text, int count)
    {
        std::string out;
        for (int i = 0; i < count && text[i] != 0; i++)
        {
            u32 c = text[i];
            if (c == GameMale)
                c = 0x2642;
            else if (c == GameFemale)
                c = 0x2640;
            else if (c >= 0xD800 && c < 0xE000)
                c = '?';    // the keyboard has no surrogate pairs
            AppendUtf8(out, c);
        }
        return out;
    }

    std::string KeyboardKeyText(int index, bool shift)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        if (index < 0 || index >= (int)keys.size())
            return "";
        const KeyboardKey &k = keys[index];
        if (k.kind != KeyKind::Char && k.kind != KeyKind::Space)
            return "";
        const u16 c = shift ? k.upper : k.lower;
        return KeyboardUtf8(&c, 1);
    }

    KeyboardPress KeyboardPressKey(int index, const KeyboardGame &game, int pendingChars, int pendingDeletes)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        if (index < 0 || index >= (int)keys.size())
            return KeyboardPress::Empty;
        int length = game.length + pendingChars - pendingDeletes;
        if (length < 0)
            length = 0;
        switch (keys[index].kind)
        {
            case KeyKind::Char:
            case KeyKind::Space:
                return game.max > 0 && length >= game.max ? KeyboardPress::Full : KeyboardPress::Tap;
            case KeyKind::Delete:
                return length == 0 ? KeyboardPress::Empty : KeyboardPress::Tap;
            default:
                return KeyboardPress::Tap;
        }
    }

    static std::vector<ActionDef> MakeKeyboardActions(void)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        // Names, buttons and summaries must outlive the definitions.
        static std::vector<std::string> names, buttons, summaries;
        names.reserve(keys.size());
        buttons.reserve(keys.size());
        summaries.reserve(keys.size());

        std::vector<ActionDef> list;
        for (const KeyboardKey &k : keys)
        {
            names.push_back(std::string("keyboard-") + k.name);
            buttons.push_back(ButtonName(k));
            summaries.push_back("tap " + buttons.back());
            // One tap; the short wait keeps the next key a separate touch the
            // game sees (taps 8 frames apart were all typed in the key sweep).
            ActionDef def = { names.back().c_str(), summaries.back().c_str(), AnyScene(),
                              { StepTap(buttons.back().c_str()), StepWait(4) } };
            list.push_back(def);
        }
        return list;
    }

    const ActionDef &KeyboardAction(int index)
    {
        static const std::vector<ActionDef> actions = MakeKeyboardActions();
        return actions[index < 0 || index >= (int)actions.size() ? 0 : index];
    }

    const ActionDef &KeyboardToQwerty(void)
    {
        // The page slides over; give it time before the next key.
        static const ActionDef def = { "keyboard-to-qwerty", "tap strinput_grid.to_qwerty", AnyScene(),
                                       { StepTap("strinput_grid.to_qwerty"), StepWait(20) } };
        return def;
    }

    // --- Drawing ---

    // The game's name-entry keyboard (StrInput, bottom screen), measured
    // from screenshots (docs/KEYBOARD.md, "Look"): a light grey board under
    // a sky-blue name band, pale-to-sky-blue character keys with black
    // labels, green Delete / OK / Space keys with white labels, Shift green
    // (off) or orange (on), and a yellow frame round the selected key.
    static constexpr UI::Color Board      = Rgb(0xF2, 0xF2, 0xF2);
    static constexpr UI::Color BoardEdge  = Rgb(0x9D, 0x9D, 0x9D);
    static constexpr UI::Color BandTop    = Rgb(0xB4, 0xE0, 0xFF);
    static constexpr UI::Color BandBottom = Rgb(0xDC, 0xF1, 0xFF);
    static constexpr UI::Color BandLine   = Rgb(0x7F, 0x7F, 0x7F);
    static constexpr UI::Color Strip      = Rgb(0xFA, 0xFA, 0xFA);
    static constexpr UI::Color SlotLine   = Rgb(0x6C, 0x6D, 0x6C);   // a free character slot
    static constexpr UI::Color SlotNext   = Rgb(0xFF, 0xEB, 0x00);   // the slot typed into next
    static constexpr UI::Color SlotEdge   = Rgb(0x20, 0x1D, 0x00);
    static constexpr UI::Color Caret      = Rgb(0x00, 0x96, 0xFF);
    static constexpr UI::Color NameInk    = Rgb(0x00, 0x00, 0x00);
    static constexpr UI::Color MessageInk = Rgb(0x5C, 0x5C, 0x5D);

    struct KeyColors
    {
        UI::Color rim, top, bottom, base, edge;
    };
    static constexpr KeyColors BlueKey   = { Rgb(0xB7, 0xF4, 0xF8), Rgb(0xB9, 0xF9, 0xFF), Rgb(0x50, 0xA8, 0xE6),
                                             Rgb(0x40, 0x8B, 0xC1), Rgb(0x28, 0x59, 0x7D) };
    static constexpr KeyColors GreenKey  = { Rgb(0xD8, 0xFA, 0x9A), Rgb(0xB9, 0xF4, 0x49), Rgb(0x6B, 0xA0, 0x08),
                                             Rgb(0x5A, 0x88, 0x05), Rgb(0x37, 0x54, 0x02) };
    static constexpr KeyColors OrangeKey = { Rgb(0xFF, 0xD0, 0x98), Rgb(0xFF, 0xA8, 0x47), Rgb(0xEA, 0x45, 0x07),
                                             Rgb(0xC0, 0x38, 0x05), Rgb(0x7F, 0x22, 0x02) };
    static constexpr UI::Color KeyInk    = Rgb(0x00, 0x00, 0x00);
    static constexpr UI::Color KeyInkOn  = Rgb(0xFF, 0xFF, 0xFF);   // on green and orange keys
    // The game's frame is #FDC52E; a touch lighter here so it stands out
    // on the grey board at the top screen's size.
    static constexpr UI::Color Frame     = Rgb(0xFF, 0xD2, 0x2E);
    static constexpr UI::Color FrameGlow = Rgb(0xF9, 0xF2, 0xA7, 0xB0);
    static constexpr int KeyRadius = 5;

    // The game's keys span x 20-289, y 60-228 on the bottom screen; the
    // native ones are the same layout scaled to 13/10 across, 17/20 down.
    static const int GameLeft = 20, GameTop = 60;
    static const int KeysX = KeyboardArea.x + 12;
    static const int KeysY = KeyboardArea.y + 52;
    static const int BandY = KeyboardArea.y + 8, BandH = 36;
    static const int SlotPitch = 16, SlotW = 13, SlotH = 4;
    static const int NameMax = 16;     // KeyboardGame::text
    static const int FooterY = KeyboardArea.Bottom() - 4 - UI::Theme::PromptHeight;

    UI::Rect KeyboardKeyRect(int index)
    {
        const UI::Rect g = KeyboardGameRect(index);
        const int x0 = KeysX + (g.x - GameLeft) * 13 / 10;
        const int x1 = KeysX + (g.x + g.w - GameLeft) * 13 / 10;
        const int y0 = KeysY + (g.y - GameTop) * 17 / 20;
        const int y1 = KeysY + (g.y + g.h - GameTop) * 17 / 20;
        return UI::Rect{ x0, y0, x1 - x0, y1 - y0 };
    }

    static UI::Color Mix(UI::Color a, UI::Color b, int t, int n)
    {
        if (n <= 0)
            return a;
        return Rgb((u8)(a.r + (b.r - a.r) * t / n), (u8)(a.g + (b.g - a.g) * t / n), (u8)(a.b + (b.b - a.b) * t / n));
    }

    // How far row dy of a rounded rect is cut in at each end.
    static int Corner(const UI::Rect &r, int radius, int dy)
    {
        const int fromEdge = dy < r.h - 1 - dy ? dy : r.h - 1 - dy;
        if (fromEdge >= radius)
            return 0;
        static const int cut[6] = { 2, 1, 1, 0, 0, 0 };    // radius 5 less the antialiased outline
        return fromEdge < 6 ? cut[fromEdge] : 0;
    }

    static void FillVertical(UI::Canvas &canvas, const UI::Rect &r, int radius, UI::Color top, UI::Color bottom)
    {
        for (int dy = 0; dy < r.h; dy++)
        {
            const int in = Corner(r, radius, dy);
            canvas.HLine(r.x + in, r.y + dy, r.w - 2 * in, Mix(top, bottom, dy, r.h - 1));
        }
    }

    // A key: dark edge and base line along the bottom, a pale rim, then a
    // top-to-bottom gradient (the game's keys are lit from the top).
    static void DrawKeyCap(UI::Canvas &canvas, const UI::Rect &r, const KeyColors &c)
    {
        canvas.FillRoundRect(r, KeyRadius, c.edge);
        canvas.FillRoundRect(UI::Inset(r, 0, 0, 0, 1), KeyRadius, c.base);
        canvas.FillRoundRect(UI::Inset(r, 0, 0, 0, 3), KeyRadius, c.rim);
        FillVertical(canvas, UI::Inset(r, 1, 1, 1, 3), KeyRadius - 1, c.top, c.bottom);
    }

    // Shift's arrow: outlined while shift is off, filled while it is on
    // (as the game draws it).
    static void DrawShiftArrow(UI::Canvas &canvas, const UI::Rect &r, bool on)
    {
        const float cx = r.x + r.w / 2.f, cy = r.y + (r.h - 3) / 2.f;
        const float head = 7.f, stem = 3.5f;
        const float top = cy - 7.f, mid = cy, bottom = cy + 6.f;
        if (on)
        {
            canvas.FillTriangle(cx - head, mid, cx, top, cx + head, mid, KeyInkOn);
            canvas.FillRect(UI::Rect{ (int)(cx - stem), (int)mid, (int)(2 * stem), (int)(bottom - mid) }, KeyInkOn);
            return;
        }
        const float w = 1.6f;
        canvas.Line(cx - head, mid, cx, top, w, KeyInkOn);
        canvas.Line(cx, top, cx + head, mid, w, KeyInkOn);
        canvas.Line(cx - head, mid, cx - stem, mid, w, KeyInkOn);
        canvas.Line(cx + stem, mid, cx + head, mid, w, KeyInkOn);
        canvas.Line(cx - stem, mid, cx - stem, bottom, w, KeyInkOn);
        canvas.Line(cx + stem, mid, cx + stem, bottom, w, KeyInkOn);
        canvas.Line(cx - stem, bottom, cx + stem, bottom, w, KeyInkOn);
    }

    // OK's return arrow (↵), which the game's font doesn't have.
    static void DrawReturnArrow(UI::Canvas &canvas, const UI::Rect &r)
    {
        const float cx = r.x + r.w / 2.f, cy = r.y + (r.h - 3) / 2.f;
        const float right = cx + 7.f, left = cx - 7.f, y = cy + 3.f;
        canvas.Line(right, cy - 5.f, right, y, 2.f, KeyInkOn);
        canvas.Line(right, y, left + 2.f, y, 2.f, KeyInkOn);
        canvas.FillTriangle(left - 1.f, y, left + 5.f, y - 5.f, left + 5.f, y + 5.f, KeyInkOn);
    }

    static void DrawKey(UI::Canvas &canvas, int index, const KeyboardView &view)
    {
        const KeyboardKey &k = KeyboardKeys()[index];
        const UI::Rect r = KeyboardKeyRect(index);
        const bool shiftOn = k.kind == KeyKind::Shift && view.game.shift;
        DrawKeyCap(canvas, r, k.kind == KeyKind::Char ? BlueKey : shiftOn ? OrangeKey : GreenKey);

        // Labels sit on the lit part, above the base line.
        const UI::Rect face = UI::Inset(r, 1, 0, 1, 3);
        UI::Theme::TextStyle style;
        style.align = UI::Align::Center;
        switch (k.kind)
        {
            case KeyKind::Char:
                style.color = KeyInk;
                UI::Theme::DrawText(canvas, KeyboardKeyText(index, view.game.shift), face, style);
                break;
            case KeyKind::Delete:
            case KeyKind::Space:
                style.color = KeyInkOn;
                UI::Theme::DrawText(canvas, k.kind == KeyKind::Delete ? "Delete" : "Space", face, style);
                break;
            case KeyKind::Ok:
                DrawReturnArrow(canvas, r);
                break;
            case KeyKind::Shift:
                DrawShiftArrow(canvas, r, shiftOn);
                break;
        }

        if (index == view.cursor)
        {
            canvas.StrokeRoundRect(UI::Inset(r, -3), KeyRadius + 3, FrameGlow, 1);
            canvas.StrokeRoundRect(UI::Inset(r, -2), KeyRadius + 2, Frame, 2);
        }
    }

    // The name band: the name so far, one character per slot over a line
    // per character the limit allows (the next one yellow), the caret
    // before the next slot.
    static void DrawName(UI::Canvas &canvas, const KeyboardView &view)
    {
        const UI::Rect band = { KeyboardArea.x + 2, BandY, KeyboardArea.w - 4, BandH };
        canvas.HLine(band.x, band.y - 1, band.w, BandLine);
        FillVertical(canvas, band, 0, BandTop, BandBottom);
        canvas.FillRect(UI::Rect{ band.x, band.Bottom(), band.w, KeysY - 4 - band.Bottom() }, Strip);

        const int length = view.game.length < NameMax ? view.game.length : NameMax;
        UI::Theme::TextStyle ink;
        ink.color = NameInk;
        const UI::Rect text = { 0, band.y + 4, 0, band.h - 4 - SlotH - 4 };
        const bool caret = view.game.max == 0 || length < view.game.max;
        const int slots = view.game.max > 0 && view.game.max <= NameMax ? view.game.max : 0;
        if (slots == 0)
        {
            // No known limit: the name left to right, the caret after it.
            const UI::Rect line = { band.x + 12, text.y, band.w - 24, text.h };
            const int w = UI::Theme::DrawText(canvas, KeyboardUtf8(view.game.text, length), line, ink);
            if (caret)
                canvas.FillRect(UI::Rect{ line.x + w + 1, text.y, 2, text.h }, Caret);
            return;
        }

        const int x0 = KeyboardArea.x + (KeyboardArea.w - slots * SlotPitch) / 2;
        const int slotY = band.Bottom() - SlotH - 2;
        ink.align = UI::Align::Center;
        for (int i = 0; i < slots; i++)
        {
            const int x = x0 + i * SlotPitch;
            if (i == length)
            {
                canvas.FillRect(UI::Rect{ x - 1, slotY - 1, SlotW + 2, SlotH + 2 }, SlotEdge);
                canvas.FillRect(UI::Rect{ x, slotY, SlotW, SlotH }, SlotNext);
            }
            else
                canvas.FillRect(UI::Rect{ x, slotY, SlotW, SlotH }, SlotLine);
            if (i < length)
                UI::Theme::DrawText(canvas, KeyboardUtf8(&view.game.text[i], 1), UI::Rect{ x - 2, text.y, SlotW + 4, text.h }, ink);
        }
        if (caret)
            canvas.FillRect(UI::Rect{ x0 + length * SlotPitch - 2, text.y, 2, text.h }, Caret);
    }

    // The button prompts, centred along the bottom; a note ("Full")
    // takes their place while it shows.
    static void DrawFooter(UI::Canvas &canvas, const KeyboardView &view)
    {
        const UI::Rect footer = { KeyboardArea.x + 8, FooterY, KeyboardArea.w - 16, UI::Theme::PromptHeight };
        if (view.message != nullptr)
        {
            UI::Theme::TextStyle note;
            note.color = MessageInk;
            note.align = UI::Align::Center;
            UI::Theme::DrawText(canvas, view.message, footer, note);
            return;
        }
        using UI::Theme::Button;
        struct Prompt { Button button; const char *label; };
        static const Prompt prompts[] = { { Button::A, "Type" }, { Button::B, "Delete" }, { Button::Y, "Shift" },
                                          { Button::X, "Space" }, { Button::Start, "OK" } };
        const int count = sizeof(prompts) / sizeof(prompts[0]);
        const int MinGap = 2, MaxGap = 8;
        // With the wider system font (game font not loaded) they may not
        // all fit: leave out the first ones (A types: the most obvious).
        int first = 0, total = 0;
        for (const Prompt &p : prompts)
            total += UI::Theme::PromptWidth(p.button, p.label);
        while (first < count - 1 && total + MinGap * (count - first - 1) > footer.w)
        {
            total -= UI::Theme::PromptWidth(prompts[first].button, prompts[first].label);
            first++;
        }
        const int gaps = count - first - 1;
        int gap = gaps > 0 ? (footer.w - total) / gaps : 0;
        if (gap > MaxGap)
            gap = MaxGap;
        int x = footer.x + (footer.w - total - gap * gaps) / 2;
        for (int i = first; i < count; i++)
            x += UI::Theme::DrawButtonPrompt(canvas, x, footer.y, prompts[i].button, prompts[i].label) + gap;
    }

    void DrawKeyboard(UI::Canvas &canvas, const KeyboardView &view)
    {
        canvas.FillRoundRect(KeyboardArea, KeyboardRadius, BoardEdge);
        canvas.FillRoundRect(UI::Inset(KeyboardArea, 1), KeyboardRadius - 1, Board);
        DrawName(canvas, view);
        for (int i = 0; i < (int)KeyboardKeys().size(); i++)
            DrawKey(canvas, i, view);
        DrawFooter(canvas, view);
    }
}
