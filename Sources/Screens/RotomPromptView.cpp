#include "Screens/RotomPromptView.hpp"

#include <cstdio>
#include <cstring>

#include "UI/GameAssets.hpp"
#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using UI::Rgb;

    int RotomEntryIndex(u32 table, u32 pointer)
    {
        if (pointer < table || (pointer - table) % RotomEntryBytes != 0)
            return -1;
        const u32 index = (pointer - table) / RotomEntryBytes;
        return index < RotomTableEntries ? (int)index : -1;
    }

    bool RotomEntryFromWords(const u16 words[8], RotomEntry &entry)
    {
        entry.question = words[0];
        entry.answer[0] = words[1];
        entry.answer[1] = words[2];
        entry.reply[0] = words[3];
        entry.reply[1] = words[4];
        return entry.question != RotomNoLine && entry.answer[0] != RotomNoLine && entry.answer[1] != RotomNoLine;
    }

    RotoLotoStep RotoLotoStepFrom(u32 state)
    {
        switch (state & 0xFF)
        {
        case 3: return RotoLotoStep::Intro;
        case 5: return RotoLotoStep::Spinning;
        case 6:
        case 7: return RotoLotoStep::Result;
        case 8: return RotoLotoStep::Fading;
        case 9: return RotoLotoStep::Power;
        default: return RotoLotoStep::None;
        }
    }

    // Tags (0x0010, word count, tag ID, arguments) that Rotom's lines use.
    static const u16 TagPlayerName = 0x0100;
    static const u16 TagRotomName = 0x0189;     // what Rotom calls the player
    static const u16 TagNextLine = 0xBE00;
    static const u16 TagNextPage = 0xBE01;

    std::string RotomLineText(const u16 *encrypted, u32 length, u32 line, const std::string &name)
    {
        // Same as the game's 0x4B877C (docs/RE2-text.md).
        std::vector<u16> plain(length);
        u16 key = (u16)(0x2983 * (line + 3));
        for (u32 i = 0; i < length; i++)
        {
            plain[i] = encrypted[i] ^ key;
            key = (u16)((key << 3) | (key >> 13));
        }

        std::string out;
        std::vector<u16> run;
        const auto flush = [&]() {
            if (run.empty())
                return;
            run.push_back(0);
            std::vector<char> utf8(run.size() * 3 + 1);
            Utf16ToUtf8(run.data(), (u32)run.size(), utf8.data(), (u32)utf8.size());
            out += utf8.data();
            run.clear();
        };
        for (u32 i = 0; i < length && plain[i] != 0;)
        {
            const u16 c = plain[i];
            if (c == 0x0010)
            {
                const u32 words = i + 1 < length ? plain[i + 1] : 0;
                const u16 id = i + 2 < length && words > 0 ? plain[i + 2] : 0;
                flush();
                if (id == TagPlayerName || id == TagRotomName)
                    out += name;
                else if (id == TagNextLine || id == TagNextPage)
                    out += '\n';
                i += 2 + words;
                continue;
            }
            switch (c)
            {
            case 0xE07F: run.push_back(' '); break;     // narrow space
            case 0xE08D: run.push_back(0x2026); break;  // …
            case 0xE08E: run.push_back(0x2642); break;  // ♂
            case 0xE08F: run.push_back(0x2640); break;  // ♀
            default: run.push_back(c); break;
            }
            i++;
        }
        flush();
        return out;
    }

    static UI::TextStyle BodyStyle(void)
    {
        UI::TextStyle style;
        return style;
    }

    // Width of text as the box draws it (UI::Theme's Body size: the game's
    // font at 1:1 once it is loaded, else the system font). Reads the font's
    // width table directly: this runs on the plugin thread, and the theme's
    // glyph cache belongs to the OSD callback.
    static int MeasureBody(const std::string &text)
    {
        const UI::GameAssets *assets = UI::GetGameAssets();
        if (assets == nullptr || !assets->font.Ready())
            return UI::MeasureText(text, BodyStyle());
        int w = 0;
        for (size_t i = 0; i < text.size();)
        {
            UI::s32 left = 0, glyphW = 0, advance = 0;
            assets->font.Widths(assets->font.IndexOf(UI::NextCodePoint(text.data(), text.size(), i)), left, glyphW, advance);
            w += advance;
        }
        return w;
    }

    std::vector<std::string> RotomWrap(const std::string &text, int width)
    {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string rest = text.substr(start, end - start);
            while (!rest.empty() && MeasureBody(rest) > width)
            {
                // The longest head that fits, cut at a space if there is one.
                size_t fit = 0, space = std::string::npos;
                for (size_t i = 1; i <= rest.size(); i++)
                {
                    if (i < rest.size() && (((u8)rest[i]) & 0xC0) == 0x80)
                        continue;
                    if (MeasureBody(rest.substr(0, i)) > width)
                        break;
                    fit = i;
                    if (i < rest.size() && rest[i] == ' ')
                        space = i;
                }
                if (fit == 0)
                    break;
                const size_t cut = space != std::string::npos ? space : fit;
                lines.push_back(rest.substr(0, cut));
                rest = rest.substr(cut < rest.size() && rest[cut] == ' ' ? cut + 1 : cut);
            }
            lines.push_back(rest);
            start = end + 1;
        }
        while (!lines.empty() && lines.back().empty())
            lines.pop_back();
        return lines;
    }

    static void Copy(char *dst, size_t size, const std::string &src)
    {
        std::snprintf(dst, size, "%s", src.c_str());
    }

    bool operator==(const RotomView &a, const RotomView &b)
    {
        if (a.mode != b.mode || a.cursor != b.cursor || a.busy != b.busy || a.lines != b.lines || a.loto != b.loto
            || std::strcmp(a.key, b.key) != 0)
            return false;
        for (int i = 0; i < RotomMaxLines; i++)
            if (std::strcmp(a.text[i], b.text[i]) != 0)
                return false;
        return std::strcmp(a.answer[0], b.answer[0]) == 0 && std::strcmp(a.answer[1], b.answer[1]) == 0;
    }

    // Inner width of the box's text.
    static const int Pad = 10;
    static const int TextW = RotomBoxW - 2 * Pad;
    static const int LabelH = 14;
    static const int LineH = 19;        // the game font's line
    static const int AnswerW = 260, AnswerH = 26, AnswerGap = 4;
    static const int HintH = 20;

    void RotomSetText(RotomView &view, const std::string &text)
    {
        const std::vector<std::string> lines = RotomWrap(text, TextW);
        view.lines = 0;
        std::memset(view.text, 0, sizeof(view.text));
        for (const std::string &l : lines)
        {
            if (view.lines == RotomMaxLines)
            {
                // Too long for the box: the last line ends in "...".
                std::string last = view.text[RotomMaxLines - 1];
                Copy(view.text[RotomMaxLines - 1], sizeof(view.text[0]),
                     UI::Ellipsize(last + " " + l + "...", TextW, BodyStyle()));
                break;
            }
            Copy(view.text[view.lines++], sizeof(view.text[0]), l);
        }
    }

    RotomKey RotomKeys(const RotomView &view, u32 pressed, u32 talkKey)
    {
        switch (view.mode)
        {
        case RotomMode::Choice:
            if (view.busy)
                return RotomKey::None;
            if (pressed & RotomKeyA)
                return RotomKey::Answer;
            if (pressed & RotomKeyUp)
                return RotomKey::Up;
            if (pressed & RotomKeyDown)
                return RotomKey::Down;
            return RotomKey::None;
        case RotomMode::Loto:
            return !view.busy && view.loto != RotoLotoStep::Fading && (pressed & RotomKeyA) ? RotomKey::Next
                                                                                            : RotomKey::None;
        case RotomMode::Ready:
            return talkKey != 0 && (pressed & talkKey) == talkKey ? RotomKey::Talk : RotomKey::None;
        default:
            return RotomKey::None;
        }
    }

    static std::string Joined(const RotomView &view)
    {
        std::string out;
        for (int i = 0; i < view.lines; i++)
            out += (i ? " / " : "") + std::string(view.text[i]);
        return out;
    }

    std::string DescribeRotomView(const RotomView &view)
    {
        switch (view.mode)
        {
        case RotomMode::None:
            return "none";
        case RotomMode::Ready:
            return std::string("ready (talk key ") + view.key + ")";
        case RotomMode::Choice:
            return "choice \"" + Joined(view) + "\" | 1 \"" + view.answer[0] + "\" | 2 \"" + view.answer[1]
                 + "\" | cursor " + std::to_string(view.cursor + 1) + (view.busy ? " (answering)" : "");
        case RotomMode::Reply:
            return "reply \"" + Joined(view) + "\"";
        case RotomMode::Loto:
        {
            static const char *const steps[] = { "none", "intro", "spinning", "result", "fading", "power" };
            return std::string("roto loto ") + steps[(int)view.loto] + " \"" + Joined(view) + "\"";
        }
        }
        return "?";
    }

    static int BoxHeight(const RotomView &view)
    {
        int h = Pad + LabelH + view.lines * LineH + 4;    // "Rotom" label, then the text
        if (view.mode == RotomMode::Choice)
            h += 2 * AnswerH + AnswerGap + 4;
        h += HintH + Pad - 4;
        return h > RotomBoxMaxH ? RotomBoxMaxH : h;
    }

    UI::Rect RotomArea(const RotomView &view)
    {
        if (view.mode == RotomMode::None)
            return UI::Rect{ 0, 0, 0, 0 };
        if (view.mode == RotomMode::Ready)
            return RotomBadge;
        const int h = BoxHeight(view);
        return UI::Rect{ (UI::ScreenW - RotomBoxW) / 2, UI::ScreenH - 8 - h, RotomBoxW, h };
    }

    UI::Rect RotomAnswerRect(const RotomView &view, int i)
    {
        // Right-aligned, as the game lines up its answers over Rotom's box.
        const UI::Rect area = RotomArea(view);
        const int top = area.y + Pad + LabelH + view.lines * LineH + 4;
        return UI::Rect{ area.Right() - Pad - AnswerW, top + i * (AnswerH + AnswerGap), AnswerW, AnswerH };
    }

    // Rotom's text box as the game draws it on the bottom screen (measured
    // on its chat): orange line, pale yellow fill getting lighter downwards,
    // dark grey text.
    static constexpr UI::Color BoxLine  = Rgb(0xFA, 0xB3, 0x00);
    static constexpr UI::Color BoxTop   = Rgb(0xFD, 0xE1, 0x66);
    static constexpr UI::Color BoxMid   = Rgb(0xFF, 0xFA, 0xA0);
    static constexpr UI::Color BoxFill  = Rgb(0xFF, 0xFF, 0xAA);
    static constexpr UI::Color BoxText  = Rgb(0x32, 0x32, 0x32);
    static constexpr UI::Color RotomRed = Rgb(0xDE, 0x3C, 0x34);

    static UI::Color Mix(UI::Color a, UI::Color b, int num, int den)
    {
        return Rgb((u8)(a.r + (b.r - a.r) * num / den), (u8)(a.g + (b.g - a.g) * num / den),
                   (u8)(a.b + (b.b - a.b) * num / den));
    }

    static void DrawBox(UI::Canvas &canvas, const UI::Rect &r, int radius)
    {
        canvas.FillRoundRect(r, radius, BoxLine);
        const UI::Rect inner = UI::Inset(r, 2);
        canvas.FillRoundRect(inner, radius - 2, BoxFill);
        // The game's box is a little deeper yellow along its top edge.
        const int fade = inner.h < 14 ? inner.h / 2 : 7;
        for (int dy = 0; dy < fade; dy++)
        {
            const UI::Color c = dy < 3 ? Mix(BoxTop, BoxMid, dy, 3) : Mix(BoxMid, BoxFill, dy - 3, fade - 3);
            const int in = dy < radius - 2 ? radius - 2 - dy : 0;
            canvas.FillRect(UI::Rect{ inner.x + in, inner.y + dy, inner.w - 2 * in, 1 }, c);
        }
    }

    static UI::Theme::TextStyle Style(UI::Theme::TextSize size, UI::Color color, UI::Align align = UI::Align::Left)
    {
        UI::Theme::TextStyle style;
        style.size = size;
        style.color = color;
        style.align = align;
        return style;
    }

    static void DrawBadge(UI::Canvas &canvas, const RotomView &view)
    {
        DrawBox(canvas, RotomBadge, RotomBadge.h / 2);
        const UI::Rect inner = UI::Inset(RotomBadge, 12, 0, 10, 0);
        UI::Theme::DrawText(canvas, "Rotom wants to talk", UI::Rect{ inner.x, inner.y, inner.w - 22, inner.h },
                            Style(UI::Theme::TextSize::Body, BoxText));
        UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ inner.Right() - 16, inner.y + 6, 14, 14 }, RotomRed);
        (void)view;
    }

    // The A prompt's label, or "" for none.
    static const char *Hint(const RotomView &view)
    {
        switch (view.mode)
        {
        case RotomMode::Choice:
            return "Answer";
        case RotomMode::Loto:
            switch (view.loto)
            {
            case RotoLotoStep::Spinning: return "Stop the reel";
            case RotoLotoStep::Fading:   return "";
            default:                     return "Next";
            }
        default:
            return "";
        }
    }

    void DrawRotomPrompt(UI::Canvas &canvas, const RotomView &view)
    {
        if (view.mode == RotomMode::None)
            return;
        if (view.mode == RotomMode::Ready)
        {
            DrawBadge(canvas, view);
            return;
        }
        const UI::Rect area = RotomArea(view);
        DrawBox(canvas, area, RotomRadius);

        UI::Theme::DrawText(canvas, view.mode == RotomMode::Loto ? "ROTOM - ROTO LOTO" : "ROTOM",
                            UI::Rect{ area.x + Pad, area.y + Pad - 4, TextW, LabelH }, Style(UI::Theme::TextSize::Small, RotomRed));

        const UI::Theme::TextStyle body = Style(UI::Theme::TextSize::Body, BoxText);
        int y = area.y + Pad + LabelH;
        for (int i = 0; i < view.lines; i++, y += LineH)
            UI::Theme::DrawText(canvas, view.text[i], UI::Rect{ area.x + Pad, y, TextW, LineH }, body);

        if (view.mode == RotomMode::Choice)
            for (int i = 0; i < 2; i++)
                UI::Theme::DrawRow(canvas, RotomAnswerRect(view, i), UI::Theme::RowKind::Option, view.answer[i],
                                   i == view.cursor);

        const int hintY = area.Bottom() - Pad + 4 - HintH;
        const std::string hint = Hint(view);
        if (view.busy)
            UI::Theme::DrawText(canvas, view.mode == RotomMode::Choice ? "Answering..." : "...",
                                UI::Rect{ area.x + Pad, hintY, TextW, HintH }, Style(UI::Theme::TextSize::Small, BoxText, UI::Align::Right));
        else if (!hint.empty())
        {
            const int w = UI::Theme::PromptWidth(UI::Theme::Button::A, hint);
            UI::Theme::DrawButtonPrompt(canvas, area.Right() - Pad - w, hintY + (HintH - UI::Theme::PromptHeight) / 2,
                                        UI::Theme::Button::A, hint);
        }
    }
}
