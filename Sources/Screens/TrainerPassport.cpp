#include "Screens/TrainerPassport.hpp"

#include <cstdio>

#include "UI/Layout.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    std::string FormatTrainerId(u32 raw)
    {
        char out[16];
        std::snprintf(out, sizeof(out), "%06u", (unsigned)(raw % 1000000u));
        return out;
    }

    std::string FormatMoney(u32 money)
    {
        std::string digits = std::to_string(money);
        std::string out;
        for (size_t i = 0; i < digits.size(); i++)
        {
            if (i > 0 && (digits.size() - i) % 3 == 0)
                out += ',';
            out += digits[i];
        }
        return "$" + out;
    }

    std::string FormatPlayTime(u32 hours, u32 minutes)
    {
        char out[16];
        std::snprintf(out, sizeof(out), "%u:%02u", (unsigned)hours, (unsigned)minutes);
        return out;
    }

    // Bottom screen: the page's paper is light beige (241,241,224) at the
    // top; an earned stamp has dark strokes in its emblem (x 180-300,
    // y 20-150), which a blank page lacks (409 dark pixels on page 1, none
    // on pages 2-3 at a 2 px step).
    static const int PaperSamples = 2;
    static const int EmblemCols = 10, EmblemRows = 11;
    static const int EarnedDark = 8;

    const std::vector<SamplePoint> &PassportSamplePoints(void)
    {
        static std::vector<SamplePoint> points;
        if (points.empty())
        {
            points.push_back(SamplePoint{ 100, 20 });
            points.push_back(SamplePoint{ 160, 20 });
            for (int row = 0; row < EmblemRows; row++)
                for (int col = 0; col < EmblemCols; col++)
                    points.push_back(SamplePoint{ (s16)(186 + col * 12), (s16)(24 + row * 12) });
        }
        return points;
    }

    static bool IsPaper(const Sample &s)
    {
        return s.r > 220 && s.g > 220 && s.b > 195 && s.r - s.b < 40;
    }

    PassportShown DecodePassport(const Sample *samples)
    {
        PassportShown shown;
        shown.valid = IsPaper(samples[0]) && IsPaper(samples[1]);
        int dark = 0;
        for (int i = 0; i < EmblemCols * EmblemRows; i++)
            dark += IsDark(samples[PaperSamples + i]) ? 1 : 0;
        shown.earned = shown.valid && dark >= EarnedDark;
        return shown;
    }

    // --- The screen ---

    u32 PassportScreen::CaptureKeys(void) const
    {
        // A would open Portrait Photo; the native screen has no use for it.
        return ScreenKey::A | ScreenKey::DPad | ScreenKey::CPad | ScreenKey::L | ScreenKey::R | ScreenKey::Start;
    }

    void PassportScreen::Begin(std::vector<std::string> &log)
    {
        m_shown = PassportShown();
        m_candidate = PassportShown();
        m_candidateCount = 0;
        m_loggedPage = -2;
        m_loggedEarned = false;
        m_turning = false;
        m_data = PassportData();
        (void)log;
    }

    void PassportScreen::SetData(const PassportData &data, std::vector<std::string> &log)
    {
        m_data = data;
        if (!data.found)
            return;
        log.push_back("passport: " + m_data.name + ", ID No. " + (m_data.idValid ? FormatTrainerId(m_data.id) : "?")
                      + ", money " + (m_data.moneyValid ? FormatMoney(m_data.money) : "?") + ", play time "
                      + (m_data.timeValid ? FormatPlayTime(m_data.hours, m_data.minutes) : "?"));
    }

    static std::string OneLine(std::string text)
    {
        for (char &c : text)
            if (c == '\n')
                c = ' ';
        return text;
    }

    void PassportScreen::Sampled(const Sample *samples, std::vector<std::string> &log)
    {
        const PassportShown now = DecodePassport(samples);
        if (now.valid == m_candidate.valid && now.earned == m_candidate.earned)
            m_candidateCount++;
        else
        {
            m_candidate = now;
            m_candidateCount = 1;
        }
        if (m_candidateCount < StableSamples || !now.valid)
            return;
        m_shown = now;
        if (m_page < 0 || m_turning || !m_data.found || (m_page == m_loggedPage && now.earned == m_loggedEarned))
            return;
        m_loggedPage = m_page;
        m_loggedEarned = now.earned;
        std::string line = "passport: page " + std::to_string(m_page + 1) + "/" + std::to_string(PassportPages);
        if (!now.earned)
            line += ", not earned";
        else
            line += ", earned: " + (m_page < PassportPages ? OneLine(m_data.stampName[m_page]) : std::string("?"));
        log.push_back(line);
    }

    const ActionDef *PassportScreen::Key(u32 key, std::vector<std::string> &log)
    {
        if (m_page < 0 || m_turning)
            return nullptr;
        int dir = 0;
        if (key == ScreenKey::Left || key == ScreenKey::L)
            dir = -1;
        else if (key == ScreenKey::Right || key == ScreenKey::R)
            dir = 1;
        // No wrap: the game draws no ◀ on page 1 and no ▶ on page 15.
        if (dir == 0 || m_page + dir < 0 || m_page + dir >= PassportPages)
            return nullptr;
        m_turning = true;
        log.push_back("passport: turn to page " + std::to_string(m_page + dir + 1));
        // The page slides for ~15 frames.
        return dir < 0 ? &ScreenPressAction("passport-prev", Scene(), ScreenKey::Left, 24)
                       : &ScreenPressAction("passport-next", Scene(), ScreenKey::Right, 24);
    }

    void PassportScreen::ActionDone(std::vector<std::string> &log)
    {
        (void)log;
        m_turning = false;
        m_candidateCount = 0;
    }

    u32 PassportScreen::DrawKey(void) const
    {
        return (u32)(m_page + 2) * 8 + (m_shown.earned ? 1u : 0u) + (m_turning ? 2u : 0u) + (m_shown.valid ? 4u : 0u);
    }

    // The passport's page, measured on the game's bottom screen: beige
    // paper with a tan band at the top, the stamp's name on a peach plate,
    // its text on a white strip, dark text, blue page arrows.
    namespace PassportStyle
    {
        constexpr UI::Color Paper     = UI::Rgb(0xF5, 0xF5, 0xE4);
        constexpr UI::Color BandTop   = UI::Rgb(0xF7, 0xC4, 0x80);
        constexpr UI::Color BandEnd   = UI::Rgb(0xF7, 0xDF, 0xBE);
        constexpr UI::Color Plate     = UI::Rgb(0xFF, 0xD7, 0xBE);
        constexpr UI::Color PlateLine = UI::Rgb(0xA2, 0x80, 0x6F);
        constexpr UI::Color Strip     = UI::Rgb(0xFF, 0xF9, 0xF9);
        constexpr UI::Color Ink       = UI::Rgb(0x30, 0x2A, 0x26);
        constexpr UI::Color InkDim    = UI::Rgb(0x8C, 0x80, 0x76);
        constexpr UI::Color Arrow     = UI::Rgb(0x33, 0xAA, 0xFF);
        constexpr UI::Color Label     = UI::Rgb(0xE6, 0xD3, 0xA8);   // field labels on the dark window
    }

    static void DrawField(UI::Canvas &canvas, const UI::Rect &r, const std::string &label, const std::string &value)
    {
        using namespace UI::Theme;
        TextStyle l, v;
        l.color = PassportStyle::Label;
        v.color = WindowText;
        DrawText(canvas, label, UI::Rect{ r.x, r.y, 84, r.h }, l);
        DrawText(canvas, value, UI::Rect{ r.x + 88, r.y, r.w - 88, r.h }, v);
    }

    static void DrawPageArrow(UI::Canvas &canvas, s32 x, s32 centerY, bool left)
    {
        const float y = (float)centerY + 0.5f, tip = left ? (float)x : (float)x + 10, base = left ? (float)x + 10 : (float)x;
        canvas.FillTriangle(base, y - 10, tip, y, base, y + 10, PassportStyle::Arrow);
    }

    void PassportScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace UI::Theme;
        using namespace PassportStyle;
        const UI::Rect area = PassportArea;
        canvas.FillRect(area, Paper);
        for (s32 dy = 0; dy < 12; dy++)
        {
            const float t = dy / 11.f;
            canvas.HLine(area.x, area.y + dy, area.w, UI::Rgb((u8)(BandTop.r + (BandEnd.r - BandTop.r) * t),
                                                              (u8)(BandTop.g + (BandEnd.g - BandTop.g) * t),
                                                              (u8)(BandTop.b + (BandEnd.b - BandTop.b) * t)));
        }
        const UI::Rect body = DrawScreenPrompts(canvas, area, { { Button::B, "Back" } },
                                                { { Button::L, "" }, { Button::R, "Page" } });

        // The trainer's card: two columns of two values in the game's window.
        const PassportData &d = m_data;
        const s32 lineH = LineHeight(TextSize::Body);
        const UI::Rect card = { area.x + 6, area.y + 8, area.w - 12, 2 * lineH + 2 * WindowPad + 4 };
        DrawWindow(canvas, card);
        const UI::Rect fields = UI::Inset(card, WindowPad + 4, WindowPad + 2, WindowPad + 4, WindowPad + 2);
        const s32 colW = fields.w / 2 - 4;
        auto pick = [](const std::string &s, const char *fallback) { return s.empty() ? std::string(fallback) : s; };
        DrawField(canvas, UI::Rect{ fields.x, fields.y, colW, lineH }, pick(d.labelName, "Name"), d.found ? d.name : "?");
        DrawField(canvas, UI::Rect{ fields.x, fields.y + lineH, colW, lineH }, pick(d.labelId, "ID No."),
                  d.idValid ? FormatTrainerId(d.id) : "?");
        DrawField(canvas, UI::Rect{ fields.x + colW + 8, fields.y, colW, lineH }, pick(d.labelMoney, "Money"),
                  d.moneyValid ? FormatMoney(d.money) : "?");
        DrawField(canvas, UI::Rect{ fields.x + colW + 8, fields.y + lineH, colW, lineH }, pick(d.labelTime, "Play time"),
                  d.timeValid ? FormatPlayTime(d.hours, d.minutes) : "?");

        // The stamp page: name plate, text strip, page number, arrows.
        const UI::Rect page = { area.x + 22, card.Bottom() + 8, area.w - 44, body.Bottom() - card.Bottom() - 8 };
        const UI::Rect plate = { page.x, page.y, page.w, lineH + 14 };
        canvas.FillRoundRect(plate, 4, PlateLine);
        canvas.FillRoundRect(UI::Inset(plate, 1), 3, Plate);
        TextStyle name;
        name.color = Ink;
        name.align = UI::Align::Center;
        const bool known = !m_turning && m_page >= 0;
        const std::string title = !known ? "..." : !m_shown.earned ? "Not earned yet." : OneLine(d.stampName[m_page]);
        if (known && !m_shown.earned)
            name.color = InkDim;
        DrawText(canvas, title, UI::Inset(plate, 8, 0, 8, 0), name);

        if (known && m_shown.earned)
        {
            // The text is one or two lines ("\n" in the game's text).
            std::vector<std::string> lines;
            std::string rest = d.stampText[m_page];
            while (!rest.empty() && lines.size() < 2)
            {
                const size_t nl = rest.find('\n');
                lines.push_back(rest.substr(0, nl));
                rest = nl == std::string::npos ? "" : rest.substr(nl + 1);
            }
            if (!lines.empty())
            {
                const UI::Rect strip = { page.x - 8, plate.Bottom() + 10, page.w + 16, (s32)lines.size() * lineH + 10 };
                canvas.FillRect(strip, Strip);
                canvas.HLine(strip.x, strip.y, strip.w, PlateLine);
                canvas.HLine(strip.x, strip.Bottom() - 1, strip.w, PlateLine);
                TextStyle text;
                text.color = Ink;
                for (size_t i = 0; i < lines.size(); i++)
                    DrawText(canvas, lines[i], UI::Rect{ strip.x + 8, strip.y + 5 + (s32)i * lineH, strip.w - 16, lineH }, text);
            }
        }

        TextStyle number;
        number.color = Ink;
        number.align = UI::Align::Right;
        const std::string count = (m_page >= 0 ? std::to_string(m_page + 1) : std::string("?")) + "/" + std::to_string(PassportPages);
        DrawText(canvas, count, UI::Rect{ page.x, page.Bottom() - lineH, page.w, lineH }, number);
        const s32 cy = page.y + page.h / 2;
        if (m_page > 0)
            DrawPageArrow(canvas, area.x + 4, cy, true);
        if (m_page >= 0 && m_page < PassportPages - 1)
            DrawPageArrow(canvas, area.Right() - 14, cy, false);
    }

    std::string PassportScreen::Status(void) const
    {
        return "passport: page " + (m_page >= 0 ? std::to_string(m_page + 1) : std::string("?")) + "/"
               + std::to_string(PassportPages) + (m_shown.valid ? (m_shown.earned ? ", earned" : ", not earned") : ", not read")
               + (m_turning ? ", turning" : "");
    }
}
