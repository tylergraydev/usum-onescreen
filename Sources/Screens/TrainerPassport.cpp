#include "Screens/TrainerPassport.hpp"

#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

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

    static void DrawField(UI::Canvas &canvas, const UI::Rect &r, const std::string &label, const std::string &value)
    {
        UI::TextStyle l, v;
        l.color = ScreenStyle::TextDim;
        v.color = ScreenStyle::TextMain;
        UI::DrawText(canvas, label, UI::Rect{ r.x, r.y, 84, r.h }, l);
        UI::DrawText(canvas, value, UI::Rect{ r.x + 88, r.y, r.w - 88, r.h }, v);
    }

    void PassportScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace ScreenStyle;
        UI::Rect body = DrawScreenFrame(canvas, PassportArea, "Trainer Passport");
        body = DrawScreenFooter(canvas, body, "Left/Right, L/R  Page        B Back");

        // The card: two columns of two values.
        const PassportData &d = m_data;
        const s32 lineH = 19;
        const s32 colW = body.w / 2 - 4;
        auto pick = [](const std::string &s, const char *fallback) { return s.empty() ? std::string(fallback) : s; };
        DrawField(canvas, UI::Rect{ body.x + 4, body.y, colW, lineH }, pick(d.labelName, "Name"), d.found ? d.name : "?");
        DrawField(canvas, UI::Rect{ body.x + 4, body.y + lineH, colW, lineH }, pick(d.labelId, "ID No."),
                  d.idValid ? FormatTrainerId(d.id) : "?");
        DrawField(canvas, UI::Rect{ body.x + colW + 12, body.y, colW, lineH }, pick(d.labelMoney, "Money"),
                  d.moneyValid ? FormatMoney(d.money) : "?");
        DrawField(canvas, UI::Rect{ body.x + colW + 12, body.y + lineH, colW, lineH }, pick(d.labelTime, "Play time"),
                  d.timeValid ? FormatPlayTime(d.hours, d.minutes) : "?");

        // The stamp page.
        const UI::Rect stamp = { body.x, body.y + 2 * lineH + 8, body.w, body.h - 2 * lineH - 8 };
        canvas.FillRoundRect(stamp, 10, RowBg);
        UI::TextStyle head, text;
        head.color = TextDim;
        text.color = TextMain;
        const UI::Rect inner = UI::Inset(stamp, 12, 6, 12, 6);
        const std::string page = m_page >= 0 ? "Stamp " + std::to_string(m_page + 1) + " / " + std::to_string(PassportPages)
                                             : std::string("Stamp ?");
        UI::DrawText(canvas, page, UI::Rect{ inner.x, inner.y, inner.w, lineH }, head);
        if (m_page > 0)
            UI::DrawIcon(canvas, UI::Icon::ArrowLeft, UI::Rect{ inner.Right() - 40, inner.y + 3, 12, 12 }, Selected);
        if (m_page >= 0 && m_page < PassportPages - 1)
            UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ inner.Right() - 14, inner.y + 3, 12, 12 }, Selected);

        s32 y = inner.y + lineH + 6;
        if (m_turning || m_page < 0)
            UI::DrawText(canvas, "...", UI::Rect{ inner.x, y, inner.w, lineH }, text);
        else if (!m_shown.earned)
        {
            text.color = TextDim;
            UI::DrawText(canvas, "Not earned yet.", UI::Rect{ inner.x, y, inner.w, lineH }, text);
        }
        else
        {
            UI::TextStyle name;
            name.font = UI::Font::Large;
            name.color = Selected;
            const s32 nameH = UI::LineHeight(name);
            UI::DrawText(canvas, m_page < PassportPages ? OneLine(d.stampName[m_page]) : "?",
                         UI::Rect{ inner.x, y, inner.w, nameH }, name);
            y += nameH + 6;
            // The text is one or two lines ("\n" in the game's text).
            std::string rest = m_page < PassportPages ? d.stampText[m_page] : "";
            while (!rest.empty() && y + lineH <= inner.Bottom())
            {
                const size_t nl = rest.find('\n');
                UI::DrawText(canvas, rest.substr(0, nl), UI::Rect{ inner.x, y, inner.w, lineH }, text);
                rest = nl == std::string::npos ? "" : rest.substr(nl + 1);
                y += lineH;
            }
        }
    }

    std::string PassportScreen::Status(void) const
    {
        return "passport: page " + (m_page >= 0 ? std::to_string(m_page + 1) : std::string("?")) + "/"
               + std::to_string(PassportPages) + (m_shown.valid ? (m_shown.earned ? ", earned" : ", not earned") : ", not read")
               + (m_turning ? ", turning" : "");
    }
}
