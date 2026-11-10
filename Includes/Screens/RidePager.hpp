#pragma once

#include "Screens/NativeScreen.hpp"

namespace OneScreen
{
    constexpr int RidePagerRows = 7;

    struct RidePagerState
    {
        u32 object = 0;
        u32 count = 0, cursor = 0;
        u8 current = 0xFF;
        u8 ids[RidePagerRows] = {};
        bool dialog = false;
    };

    bool RidePagerValid(const RidePagerState &state);
    const char *RidePagerName(unsigned id);
    const char *RidePagerRowButton(unsigned row);
#ifdef __3DS__
    // Scans the scene stack; never recognises the Pager from field state alone.
    RidePagerState ReadRidePager(u32 sceneManager, const std::string &scene);
#endif

    class RidePagerScreen : public NativeScreen
    {
    public:
        const char *Name() const override { return "ride-pager"; }
        const char *Scene() const override { return "FieldRo"; }
        UI::Rect Area() const override { return { 4, 0, 392, 240 }; }
        u32 CaptureKeys() const override;
        const std::vector<SamplePoint> &Samples() const override;
        void Begin(std::vector<std::string> &log) override;
        void SetState(const RidePagerState &state);
        void Sampled(const Sample *samples, std::vector<std::string> &log) override;
        bool Ready() const override;
        const ActionDef *Key(u32 key, std::vector<std::string> &log) override;
        void ActionDone(std::vector<std::string> &log) override;
        u32 DrawKey() const override { return m_version; }
        void Draw(UI::Canvas &canvas) const override;
        std::string Status() const override;
        bool Available(unsigned row) const;
        bool Registered(unsigned row) const;
    private:
        RidePagerState m_state;
        unsigned m_usable = 0, m_registered = 0;
        unsigned m_candidate = ~0u, m_stable = 0;
        bool m_ready = false, m_busy = false;
        u32 m_version = 0;
    };
}
