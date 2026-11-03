#include "Screens/BattleVideo.hpp"

#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    const char *VideoStateName(VideoState state)
    {
        switch (state)
        {
        case VideoState::Intro: return "intro";
        case VideoState::Empty: return "empty";
        case VideoState::Other: return "other";
        default:                return "unknown";
        }
    }

    // Bottom screen. Intro: the dark message box at the top (41,43,34 at
    // y 20) and the light button (215,255,255 at y 112, x 40 and 260; its
    // label is dark in the middle). Empty recorder: only the blue water
    // background, which is much bluer than red everywhere sampled.
    static const int BoxSamples = 2, ButtonSamples = 2;
    static const int WaterCols = 7, WaterRows = 9;

    const std::vector<SamplePoint> &VideoSamplePoints(void)
    {
        static std::vector<SamplePoint> points;
        if (points.empty())
        {
            points.push_back(SamplePoint{ 40, 20 });
            points.push_back(SamplePoint{ 280, 20 });
            points.push_back(SamplePoint{ 40, 112 });
            points.push_back(SamplePoint{ 260, 112 });
            for (int row = 0; row < WaterRows; row++)
                for (int col = 0; col < WaterCols; col++)
                    points.push_back(SamplePoint{ (s16)(40 + col * 40), (s16)(30 + row * 20) });
        }
        return points;
    }

    static bool IsButton(const Sample &s)
    {
        return s.r > 190 && s.g > 235 && s.b > 235;
    }

    static bool IsWater(const Sample &s)
    {
        // Measured on two empty recorders: b - r >= 107, r <= 112, b >= 158.
        return s.b > s.r + 70 && s.r < 150 && s.b > 130;
    }

    VideoState DecodeVideo(const Sample *samples)
    {
        if (IsDark(samples[0]) && IsDark(samples[1]) && IsButton(samples[2]) && IsButton(samples[3]))
            return VideoState::Intro;
        const Sample *water = samples + BoxSamples + ButtonSamples;
        for (int i = 0; i < WaterCols * WaterRows; i++)
            if (!IsWater(water[i]))
                return VideoState::Other;
        return VideoState::Empty;
    }

    // --- The screen ---

    u32 VideoScreen::CaptureKeys(void) const
    {
        // Anything not checked: the game's own keys.
        if (m_state == VideoState::Other)
            return 0;
        return ScreenKey::A | ScreenKey::DPad | ScreenKey::CPad | ScreenKey::L | ScreenKey::R | ScreenKey::Start;
    }

    void VideoScreen::Begin(std::vector<std::string> &log)
    {
        (void)log;
        m_state = m_candidate = VideoState::Unknown;
        m_candidateCount = 0;
        m_opening = false;
    }

    void VideoScreen::Sampled(const Sample *samples, std::vector<std::string> &log)
    {
        const VideoState now = DecodeVideo(samples);
        if (now == m_candidate)
            m_candidateCount++;
        else
        {
            m_candidate = now;
            m_candidateCount = 1;
        }
        const u32 needed = now == VideoState::Other ? StableOther : StableSamples;
        if (m_candidateCount < needed || now == m_state)
            return;
        m_state = now;
        if (now == VideoState::Empty)
            log.push_back("video: recorder empty (no Battle Videos)");
        else if (now == VideoState::Other)
            log.push_back("video: recorder shows something not checked, keys left to the game");
        else
            log.push_back("video: intro (Open the Vs. Recorder)");
    }

    static const ActionDef &OpenAction(void)
    {
        // The game's cursor is on its only button; A opens the recorder,
        // which takes about a second to fade in.
        static const ActionDef def = { "video-open", "BattleVideoPlayer: A (Open the Vs. Recorder)",
                                       InScene("BattleVideoPlayer"), { StepPress(ScreenKey::A), StepWait(40) } };
        return def;
    }

    const ActionDef *VideoScreen::Key(u32 key, std::vector<std::string> &log)
    {
        if (key != ScreenKey::A || m_state != VideoState::Intro || m_opening)
            return nullptr;
        m_opening = true;
        log.push_back("video: open the Vs. Recorder");
        return &OpenAction();
    }

    void VideoScreen::ActionDone(std::vector<std::string> &log)
    {
        (void)log;
        m_opening = false;
    }

    u32 VideoScreen::DrawKey(void) const
    {
        return (u32)m_state * 2 + (m_opening ? 1u : 0u);
    }

    void VideoScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace ScreenStyle;
        UI::Rect body = DrawScreenFrame(canvas, VideoArea, "Battle Video");
        const char *hints = m_state == VideoState::Intro ? "A Open    B Back"
                          : m_state == VideoState::Empty ? "B Back"
                                                         : "L+R: bottom screen on top";
        body = DrawScreenFooter(canvas, body, hints);

        UI::TextStyle text;
        text.color = TextMain;
        const s32 lineH = 20;
        auto line = [&](int i, const std::string &s) {
            UI::DrawText(canvas, s, UI::Rect{ body.x + 8, body.y + 4 + i * lineH, body.w - 16, lineH }, text);
        };

        if (m_state == VideoState::Intro)
        {
            line(0, "Watch battles recorded with the Vs. Recorder.");
            const UI::Rect button = { body.x + 30, body.y + 4 + 3 * lineH, body.w - 60, 32 };
            canvas.FillRoundRect(button, 12, RowBg);
            canvas.StrokeRoundRect(UI::Inset(button, -2), 14, Selected, 2);
            UI::TextStyle b = text;
            b.align = UI::Align::Center;
            UI::DrawText(canvas, m_opening ? "Opening..." : "Open the Vs. Recorder", button, b);
        }
        else if (m_state == VideoState::Empty)
        {
            line(0, "There are no Battle Videos on this save.");
            text.color = TextDim;
            line(2, "Videos are recorded in Link Battles,");
            line(3, "at the Battle Spot and so on.");
        }
        else
        {
            line(0, "The Vs. Recorder shows something this screen");
            line(1, "doesn't know yet (a list of videos?).");
            text.color = TextDim;
            line(3, "Use the bottom screen: L+R shows it on top.");
            line(4, "Keys go to the game here.");
        }
    }

    std::string VideoScreen::Status(void) const
    {
        return std::string("video: ") + VideoStateName(m_state) + (m_opening ? ", opening" : "");
    }
}
