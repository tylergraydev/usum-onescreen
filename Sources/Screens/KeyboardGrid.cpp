#include "Screens/KeyboardGrid.hpp"

#include <cstdlib>

#include "UI/Layout.hpp"
#include "UI/Text.hpp"

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

    static constexpr UI::Color PanelBg   = Rgb(22, 30, 52);
    static constexpr UI::Color Border    = Rgb(120, 150, 220);
    static constexpr UI::Color TextMain  = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim   = Rgb(150, 165, 200);
    static constexpr UI::Color TextInk   = Rgb(24, 32, 56);
    static constexpr UI::Color Selected  = Rgb(255, 236, 120);
    static constexpr UI::Color KeyFill   = Rgb(52, 86, 150);
    static constexpr UI::Color KeyGreen  = Rgb(58, 150, 70);
    static constexpr UI::Color KeyShift  = Rgb(232, 110, 30);
    static constexpr UI::Color FieldFill = Rgb(236, 242, 252);
    static constexpr UI::Color SlotLine  = Rgb(150, 165, 200);

    // The game's keys span x 20-289, y 60-228 on the bottom screen; the
    // native ones are the same layout scaled to 13/10 across, 4/5 down.
    static const int GameLeft = 20, GameTop = 60;
    static const int KeysX = KeyboardArea.x + 12;
    static const int KeysY = KeyboardArea.y + 64;
    static const int TitleY = KeyboardArea.y + 6, TitleH = 18;
    static const int FieldY = KeyboardArea.y + 28, FieldH = 28;
    static const int FooterH = 18;

    UI::Rect KeyboardKeyRect(int index)
    {
        const UI::Rect g = KeyboardGameRect(index);
        const int x0 = KeysX + (g.x - GameLeft) * 13 / 10;
        const int x1 = KeysX + (g.x + g.w - GameLeft) * 13 / 10;
        const int y0 = KeysY + (g.y - GameTop) * 4 / 5;
        const int y1 = KeysY + (g.y + g.h - GameTop) * 4 / 5;
        return UI::Rect{ x0, y0, x1 - x0, y1 - y0 };
    }

    static const char *SpecialLabel(KeyKind kind)
    {
        switch (kind)
        {
            case KeyKind::Delete: return "Delete";
            case KeyKind::Ok:     return "OK";
            case KeyKind::Space:  return "Space";
            case KeyKind::Shift:  return "Shift";
            default:              return "";
        }
    }

    static void DrawKey(UI::Canvas &canvas, int index, const KeyboardView &view)
    {
        const KeyboardKey &k = KeyboardKeys()[index];
        const UI::Rect r = KeyboardKeyRect(index);
        UI::Color fill = KeyFill;
        if (k.kind == KeyKind::Shift && view.game.shift)
            fill = KeyShift;
        else if (k.kind != KeyKind::Char)
            fill = KeyGreen;
        canvas.FillRoundRect(r, 5, fill);
        if (index == view.cursor)
            canvas.StrokeRoundRect(UI::Inset(r, -2), 7, Selected, 2);

        UI::TextStyle style;
        style.color = TextMain;
        style.align = UI::Align::Center;
        const std::string label = k.kind == KeyKind::Char ? KeyboardKeyText(index, view.game.shift)
                                                          : SpecialLabel(k.kind);
        UI::DrawText(canvas, label, r, style);
    }

    void DrawKeyboard(UI::Canvas &canvas, const KeyboardView &view)
    {
        canvas.FillRoundRect(KeyboardArea, KeyboardRadius, PanelBg);
        canvas.StrokeRoundRect(KeyboardArea, KeyboardRadius, Border, 2);

        // Title and the count against the limit.
        const UI::Rect title = { KeysX, TitleY, KeyboardArea.Right() - 12 - KeysX, TitleH };
        UI::TextStyle dim;
        dim.color = TextDim;
        UI::DrawText(canvas, "Enter a name", title, dim);
        dim.align = UI::Align::Right;
        const int max = view.game.max > 0 ? view.game.max : 0;
        UI::DrawText(canvas, std::to_string(view.game.length) + " / " + std::to_string(max), title, dim);

        // The name so far, with a slot line per character the limit allows
        // and the caret after the last character.
        const UI::Rect field = { KeysX, FieldY, title.w, FieldH };
        canvas.FillRoundRect(field, 8, FieldFill);
        UI::TextStyle ink;
        ink.color = TextInk;
        const UI::Rect inner = UI::Inset(field, 10, 0, 10, 4);
        const int width = UI::DrawText(canvas, KeyboardUtf8(view.game.text, view.game.length), inner, ink);
        const int caretX = inner.x + width + 1;
        if (view.game.max == 0 || view.game.length < view.game.max)
            canvas.FillRect(UI::Rect{ caretX, field.y + 7, 2, field.h - 14 }, KeyFill);
        const int slots = max > 0 && max <= 16 ? max : 0;
        for (int i = 0; i < slots; i++)
        {
            const int sw = inner.w / 16;
            canvas.HLine(inner.x + i * sw, field.Bottom() - 5, sw - 3, i < view.game.length ? KeyFill : SlotLine);
        }

        for (int i = 0; i < (int)KeyboardKeys().size(); i++)
            DrawKey(canvas, i, view);

        const UI::Rect footer = { KeysX, KeyboardArea.Bottom() - FooterH - 4, title.w, FooterH };
        UI::TextStyle hint;
        hint.color = view.message != nullptr ? Selected : TextDim;
        UI::DrawText(canvas, view.message != nullptr ? view.message : "A Type   B Delete   Y Shift   X Space   START OK",
                     footer, hint);
    }
}
