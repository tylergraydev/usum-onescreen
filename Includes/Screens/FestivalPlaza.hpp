#pragma once
#include "Screens/NativeScreen.hpp"

namespace OneScreen
{
    constexpr UI::Rect FestivalPlazaArea = {4,216,392,24};
    const u32 WorldFestivalActive = 1u << 26;
    const u32 WorldFestivalHub = 1u << 27;
    const u32 WorldFestivalQuit = 1u << 28;
    const u32 WorldFestivalField = 1u << 29; // FieldRo, state 0, no hub responder
    const u32 WorldFestivalMask = WorldFestivalActive | WorldFestivalHub | WorldFestivalQuit | WorldFestivalField;
    constexpr u32 FestivalSampleCount = 9;
    enum class FestivalView : u8 { Other, Hub, Quit };
    bool IsFestivalPlaza(const std::string &scene, bool subScreenLoaded);
#ifdef __3DS__
    bool FestivalPlazaDetected(u32 sceneManager, const std::string &scene);
#endif
    const std::vector<SamplePoint> &FestivalPlazaSamplePoints();
    FestivalView DecodeFestivalPlaza(const Sample *samples);
    // Hub must be consecutive; quit tolerates short misses, never another known view.
    class FestivalPlazaProbe
    {
    public:
        u32 Update(FestivalView view);
    private:
        u32 m_hubHits = 0, m_quitHits = 0, m_quitAge = 0;
    };
    const ActionDef &FestivalPlazaLeaveAction();
    class FestivalPlazaScreen : public NativeScreen
    {
    public:
        const char *Name() const override { return "festival-plaza"; }
        const char *Scene() const override { return "FieldRo"; }
        UI::Rect Area() const override { return FestivalPlazaArea; }
        u32 CaptureKeys() const override;
        const std::vector<SamplePoint> &Samples() const override { return FestivalPlazaSamplePoints(); }
        void Begin(std::vector<std::string> &log) override;
        void Sampled(const Sample *samples, std::vector<std::string> &log) override;
        bool Ready() const override { return m_hub && !m_busy; }
        const ActionDef *Key(u32 key, std::vector<std::string> &log) override;
        void ActionDone(std::vector<std::string> &log) override { Begin(log); }
        u32 DrawKey() const override { return 0; }
        void Draw(UI::Canvas &canvas) const override;
        std::string Status() const override;
    private:
        FestivalPlazaProbe m_probe;
        bool m_hub = false, m_busy = false;
    };
}
