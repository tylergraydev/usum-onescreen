#pragma once

// Native Battle Video (scene BattleVideoPlayer, X menu page 2 → Battle
// Video). The game first shows "Open the Vs. Recorder!" with one button
// (A opens it); the recorder then lists the saved videos, or, with none,
// says "There is no Battle Video." on the top screen and leaves the bottom
// screen empty but for Back. B leaves either for the X menu. The native
// screen follows those states from the bottom screen's pixels.
//
// Only the empty recorder could be checked (this save has no videos). A
// recorder that shows anything else is drawn as "use the bottom screen" and
// the keys are left to the game, so the panel (L+R) can be used there. See
// docs/NATIVE-SCREENS.md.

#include "Screens/NativeScreen.hpp"

namespace OneScreen
{
    enum class VideoState : u8
    {
        Unknown,    // nothing decoded yet (or sliding)
        Intro,      // "Open the Vs. Recorder!" and its button
        Empty,      // the recorder with no videos
        Other,      // anything else, held for a while: a list (not checked)
    };
    const char *VideoStateName(VideoState state);

    const std::vector<SamplePoint> &VideoSamplePoints(void);
    // One frame's reading: Intro, Empty or Other (Unknown is never returned).
    VideoState DecodeVideo(const Sample *samples);

    static constexpr UI::Rect VideoArea = { 12, 8, 376, 224 };

    class VideoScreen : public NativeScreen
    {
    public:
        const char *Name(void) const override { return "video"; }
        const char *Scene(void) const override { return "BattleVideoPlayer"; }
        UI::Rect Area(void) const override { return VideoArea; }
        u32 CaptureKeys(void) const override;
        const std::vector<SamplePoint> &Samples(void) const override { return VideoSamplePoints(); }

        void Begin(std::vector<std::string> &log) override;
        void Sampled(const Sample *samples, std::vector<std::string> &log) override;
        bool Ready(void) const override { return m_state != VideoState::Unknown; }
        const ActionDef *Key(u32 key, std::vector<std::string> &log) override;
        void ActionDone(std::vector<std::string> &log) override;

        u32 DrawKey(void) const override;
        void Draw(UI::Canvas &canvas) const override;
        std::string Status(void) const override;

        VideoState State(void) const { return m_state; }

        // Readings in a row before a state counts. Other waits longer: the
        // screens slide between states and look like nothing known meanwhile.
        static const u32 StableSamples = 3;
        static const u32 StableOther = 150;

    private:
        VideoState m_state = VideoState::Unknown;
        VideoState m_candidate = VideoState::Unknown;
        u32 m_candidateCount = 0;
        bool m_opening = false;
    };
}
