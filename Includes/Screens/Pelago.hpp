#pragma once
#include "Screens/NativeScreen.hpp"

namespace OneScreen
{
    constexpr const char *PelagoScene = "main:0064787C";
    constexpr UI::Rect PelagoArea = { 4, 216, 392, 24 };
    const u32 WorldPelagoIsle = 1u << 22;
    const u32 WorldPelagoAsk = 1u << 23;
    const u32 WorldPelagoMessage = 1u << 24;
    const u32 WorldPelagoClear = 1u << 25; // stable dimmed-isle goodbye, neither bright arrows nor pills
    const u32 WorldPelagoMask = WorldPelagoIsle | WorldPelagoAsk | WorldPelagoMessage | WorldPelagoClear;

    enum class PelagoView : u8 { Other, Isle, Ask };
    constexpr u32 PelagoBottomSampleCount = 12;
    const std::vector<SamplePoint> &PelagoSamplePoints();
    PelagoView DecodePelago(const Sample *samples);
    bool PelagoClear(const Sample *samples);
    u32 PelagoBottomFlags(const Sample *samples);
    // Ask can flicker between matching and unknown. Require three matches,
    // hold for six missing observations, cancel on another recognised view.
    class PelagoBottomProbe
    {
    public:
        static constexpr u32 AskHoldSamples = 6;
        u32 Update(u32 flags);
    private:
        u32 m_candidate = 0, m_hits = 0, m_askHits = 0, m_askAge = 0;
    };
    bool PelagoMessage(const Sample *samples); // Mohn's blue nameplate + cyan message edges
    const std::vector<SamplePoint> &PelagoMessagePoints();
    bool IsPelago(const std::string &scene, bool resortLoaded);
#ifdef __3DS__
    bool PelagoDetected(u32 sceneManager, const std::string &scene);
#endif
    const ActionDef &PelagoLeaveAction(bool close = false);

    class PelagoScreen : public NativeScreen
    {
    public:
        const char *Name() const override { return "pelago"; }
        const char *Scene() const override { return PelagoScene; }
        UI::Rect Area() const override { return PelagoArea; }
        u32 CaptureKeys() const override;
        const std::vector<SamplePoint> &Samples() const override { return PelagoSamplePoints(); }
        void Begin(std::vector<std::string> &log) override;
        void Sampled(const Sample *samples, std::vector<std::string> &log) override;
        bool Ready() const override { return m_view == PelagoView::Isle && m_hits >= 3 && !m_busy; }
        const ActionDef *Key(u32 key, std::vector<std::string> &log) override;
        void ActionDone(std::vector<std::string> &log) override;
        u32 DrawKey() const override { return 0; }
        void Draw(UI::Canvas &canvas) const override;
        std::string Status() const override;
    private:
        PelagoView m_view = PelagoView::Other;
        u32 m_hits = 0;
        bool m_busy = false;
    };
}
