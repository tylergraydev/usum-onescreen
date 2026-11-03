#include "Screens/RotomPromptView.hpp"

#include <cstdio>
#include <cstring>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

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

    std::vector<std::string> RotomWrap(const std::string &text, int width)
    {
        const UI::TextStyle style = BodyStyle();
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string rest = text.substr(start, end - start);
            while (!rest.empty() && UI::MeasureText(rest, style) > width)
            {
                // The longest head that fits, cut at a space if there is one.
                size_t fit = 0, space = std::string::npos;
                for (size_t i = 1; i <= rest.size(); i++)
                {
                    if (i < rest.size() && (((u8)rest[i]) & 0xC0) == 0x80)
                        continue;
                    if (UI::MeasureText(rest.substr(0, i), style) > width)
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
    static const int Pad = 12;
    static const int TextW = RotomBoxW - 2 * Pad;
    static const int LineH = 18;
    static const int AnswerH = 26, AnswerGap = 4;
    static const int HintH = 18;

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
        int h = Pad + 14 + view.lines * LineH + 6;    // "Rotom" label, then the text
        if (view.mode == RotomMode::Choice)
            h += 2 * AnswerH + AnswerGap + 6;
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
        const UI::Rect area = RotomArea(view);
        const int top = area.y + Pad + 14 + view.lines * LineH + 6;
        return UI::Rect{ area.x + Pad + 8, top + i * (AnswerH + AnswerGap), area.w - 2 * Pad - 8, AnswerH };
    }

    static constexpr UI::Color PanelBg  = Rgb(22, 30, 52);
    static constexpr UI::Color Border   = Rgb(120, 150, 220);
    static constexpr UI::Color RotomRed = Rgb(222, 60, 52);
    static constexpr UI::Color TextMain = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim  = Rgb(150, 165, 200);
    static constexpr UI::Color Pill     = Rgb(40, 58, 96);
    static constexpr UI::Color Selected = Rgb(255, 236, 120);

    static void DrawBadge(UI::Canvas &canvas, const RotomView &view)
    {
        canvas.FillRoundRect(RotomBadge, RotomBadge.h / 2, PanelBg);
        canvas.StrokeRoundRect(RotomBadge, RotomBadge.h / 2, RotomRed, 2);
        UI::TextStyle style;
        style.color = TextMain;
        const UI::Rect inner = UI::Inset(RotomBadge, 12, 0, 10, 0);
        UI::DrawText(canvas, "Rotom wants to talk", UI::Rect{ inner.x, inner.y, inner.w - 22, inner.h }, style);
        UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ inner.Right() - 16, inner.y + 6, 14, 14 }, Selected);
        (void)view;
    }

    static const char *Hint(const RotomView &view)
    {
        switch (view.mode)
        {
        case RotomMode::Choice:
            return view.busy ? "Answering..." : "Up/Down Choose    A Answer";
        case RotomMode::Reply:
            return "";
        case RotomMode::Loto:
            switch (view.loto)
            {
            case RotoLotoStep::Spinning: return view.busy ? "..." : "A Stop the reel";
            case RotoLotoStep::Fading:   return "";
            default:                     return view.busy ? "..." : "A Next";
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
        canvas.FillRoundRect(area, RotomRadius, PanelBg);
        canvas.StrokeRoundRect(area, RotomRadius, Border, 2);

        UI::TextStyle label;
        label.font = UI::Font::Small;
        label.color = RotomRed;
        UI::DrawText(canvas, view.mode == RotomMode::Loto ? "ROTOM - ROTO LOTO" : "ROTOM",
                     UI::Rect{ area.x + Pad, area.y + Pad - 2, TextW, 12 }, label);

        UI::TextStyle body;
        body.color = TextMain;
        int y = area.y + Pad + 14;
        for (int i = 0; i < view.lines; i++, y += LineH)
            UI::DrawText(canvas, view.text[i], UI::Rect{ area.x + Pad, y, TextW, LineH }, body);

        if (view.mode == RotomMode::Choice)
            for (int i = 0; i < 2; i++)
            {
                const UI::Rect r = RotomAnswerRect(view, i);
                const bool selected = i == view.cursor;
                canvas.FillRoundRect(r, 12, Pill);
                if (selected)
                {
                    canvas.StrokeRoundRect(UI::Inset(r, -2), 13, Selected, 2);
                    UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ r.x + 6, r.y + r.h / 2 - 6, 12, 12 },
                                 Selected);
                }
                UI::TextStyle a;
                a.color = selected ? TextMain : TextDim;
                UI::DrawText(canvas, view.answer[i], UI::Inset(r, 24, 0, 8, 0), a);
            }

        UI::TextStyle hint;
        hint.color = TextDim;
        hint.align = UI::Align::Right;
        UI::DrawText(canvas, Hint(view), UI::Rect{ area.x + Pad, area.Bottom() - Pad + 4 - HintH, TextW, HintH }, hint);
    }
}
