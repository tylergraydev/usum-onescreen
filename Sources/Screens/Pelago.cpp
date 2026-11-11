#include "Screens/Pelago.hpp"
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#include "Scene.hpp"
#endif

namespace OneScreen
{
    bool IsPelago(const std::string &scene, bool resortLoaded)
    {
        return scene == PelagoScene && resortLoaded;
    }
#ifdef __3DS__
    bool PelagoDetected(u32 sceneManager, const std::string &scene)
    {
        if (scene != PelagoScene) return false;
        using CTRPluginFramework::Process;
        u32 manager = 0;
        if (!Process::Read32(sceneManager, manager) || manager == 0) return false;
        // Resort responders follow the two main-image wrapper objects in
        // the observed scene stack. Resolve their CRO, never its load address.
        for (u32 i = 0; i < 12; ++i)
        {
            u32 object = 0, vt = 0;
            if (!Process::Read32(manager + 4 * i, object) || object == 0) break;
            if (Process::Read32(object, vt) && CroModuleName(CroModuleOf(vt)) == "Resort")
                return true;
        }
        return false;
    }
#endif

    const std::vector<SamplePoint> &PelagoSamplePoints()
    {
        // Three glyph points on each arrow, mirrored around x=159.5:
        // interior, lower band and upper edge. Identical in all five
        // pl3-isle captures and pl2-left; no surrounding scenery sampled.
        // Keep the Yes/No and below-No probes for the guarded leave flow.
        static const std::vector<SamplePoint> points = {
            {16,117}, {303,117}, {16,120}, {303,120},
            {40,90}, {280,90}, {40,150}, {280,150}, {40,170}, {280,170},
            {20,113}, {299,113}
        };
        return points;
    }
    static bool Near(const Sample &s, const Sample &target, int tolerance)
    {
        return (int)s.r >= (int)target.r - tolerance && (int)s.r <= (int)target.r + tolerance
            && (int)s.g >= (int)target.g - tolerance && (int)s.g <= (int)target.g + tolerance
            && (int)s.b >= (int)target.b - tolerance && (int)s.b <= (int)target.b + tolerance;
    }
    static bool Beige(const Sample &s) { return Near(s, {247,231,198}, 20); }
    static bool Pill(const Sample &s) { return Near(s, {220,255,255}, 20); }
    PelagoView DecodePelago(const Sample *s)
    {
        const bool arrows = Near(s[0], {136,204,255}, 24) && Near(s[1], {136,204,255}, 24)
            && Near(s[2], {170,221,255}, 24) && Near(s[3], {170,221,255}, 24)
            && Near(s[10], {102,187,255}, 24) && Near(s[11], {102,187,255}, 24);
        if (arrows) return PelagoView::Isle;
        const bool pills = Pill(s[4]) && Pill(s[5]) && Pill(s[6]) && Pill(s[7]);
        if (pills && Beige(s[0]) && Beige(s[1]) && Beige(s[2]) && Beige(s[3])
            && Beige(s[8]) && Beige(s[9]) && Beige(s[10]) && Beige(s[11])) return PelagoView::Ask;
        return PelagoView::Other;
    }
    bool PelagoClear(const Sample *s)
    {
        // pl4-afteryes-bottom: isle under Mohn's modal dimmer, not beige.
        // Require all six dimmed glyph points; reject bright views and fades.
        return DecodePelago(s) == PelagoView::Other
            && Near(s[0], {35,52,65}, 20) && Near(s[1], {35,52,65}, 20)
            && Near(s[2], {43,56,65}, 20) && Near(s[3], {43,56,65}, 20)
            && Near(s[10], {26,48,65}, 20) && Near(s[11], {26,48,65}, 20);
    }
    u32 PelagoBottomFlags(const Sample *samples)
    {
        const PelagoView view = DecodePelago(samples);
        return view == PelagoView::Isle ? WorldPelagoIsle : view == PelagoView::Ask ? WorldPelagoAsk
             : PelagoClear(samples) ? WorldPelagoClear : 0;
    }
    u32 PelagoBottomProbe::Update(u32 flags)
    {
        if (flags == WorldPelagoAsk)
        {
            if (m_askHits < 3) ++m_askHits;
            m_askAge = 0;
            m_candidate = m_hits = 0;
            return m_askHits >= 3 ? WorldPelagoAsk : 0;
        }
        if (flags == 0 && m_askHits != 0 && ++m_askAge <= AskHoldSamples)
            return m_askHits >= 3 ? WorldPelagoAsk : 0;
        // A recognised isle/goodbye invalidates Ask immediately; unknown
        // expires after the short hold. This never schedules another Yes.
        m_askHits = m_askAge = 0;
        m_hits = flags == m_candidate ? (m_hits < 3 ? m_hits + 1 : 3) : 1;
        m_candidate = flags;
        return m_hits >= 3 ? flags : 0;
    }
    const std::vector<SamplePoint> &PelagoMessagePoints()
    {
        // Mohn's blue nameplate (away from its text), then cyan message
        // edges, measured in pl-ask-top.png. All above the bar at y=216.
        static const std::vector<SamplePoint> points = { {48,174}, {140,174}, {40,210}, {360,210} };
        return points;
    }
    bool PelagoMessage(const Sample *s)
    {
        return Near(s[0], {5,78,254}, 24) && Near(s[1], {5,78,254}, 24)
            && Near(s[2], {207,249,240}, 24) && Near(s[3], {196,255,255}, 24);
    }

    const ActionDef &PelagoLeaveAction(bool close)
    {
        static const ActionDef leave = [] {
            const ActionCond isle = InScene(PelagoScene).With(WorldPelagoIsle);
            const ActionCond ask = InScene(PelagoScene).With(WorldPelagoAsk | WorldPelagoMessage).Without(WorldPelagoIsle);
            const ActionCond goodbye = InScene(PelagoScene).With(WorldPelagoMessage | WorldPelagoClear).Without(WorldPelagoIsle | WorldPelagoAsk);
            return ActionDef{ "pelago-leave", "Pelago: Back, Yes once, A, wait for field", isle, {
                StepTap("pelago.back").Guard(isle),
                StepWaitFor(ask, 240), StepWait(20),
                StepTap("pelago.yes").Guard(ask),
                StepWaitFor(goodbye, 240), StepWait(45),
                StepPress(ScreenKey::A).Guard(goodbye),
                StepWaitFor(InScene(SceneField), 1200)
            } };
        }();
        static const ActionDef closeDef = [] {
            ActionDef def = leave;
            def.name = "close";
            def.summary = "Pelago leave, then close the returned X menu";
            def.steps.push_back(StepBackOut(900));
            return def;
        }();
        return close ? closeDef : leave;
    }

    u32 PelagoScreen::CaptureKeys() const
    {
        return Ready() ? ScreenKey::L | ScreenKey::R | ScreenKey::Start | ScreenKey::B : 0;
    }
    void PelagoScreen::Begin(std::vector<std::string> &log)
    {
        (void)log;
        m_view = PelagoView::Other; m_hits = 0; m_busy = false;
    }
    void PelagoScreen::Sampled(const Sample *samples, std::vector<std::string> &log)
    {
        (void)log;
        const PelagoView view = DecodePelago(samples);
        m_hits = view == m_view ? (m_hits < 3 ? m_hits + 1 : 3) : 1;
        m_view = view; // unknown/modal releases keys immediately
    }
    const ActionDef *PelagoScreen::Key(u32 key, std::vector<std::string> &log)
    {
        if (!Ready()) return nullptr;
        static const ActionDef previous = { "pelago-previous", "Pelago: previous isle", InScene(PelagoScene).With(WorldPelagoIsle),
            { StepTap("pelago.previous").Guard(InScene(PelagoScene).With(WorldPelagoIsle)), StepWait(30) } };
        static const ActionDef next = { "pelago-next", "Pelago: next isle", InScene(PelagoScene).With(WorldPelagoIsle),
            { StepTap("pelago.next").Guard(InScene(PelagoScene).With(WorldPelagoIsle)), StepWait(30) } };
        static const ActionDef mohn = { "pelago-mohn", "Pelago: Mohn", InScene(PelagoScene).With(WorldPelagoIsle),
            { StepTap("pelago.mohn").Guard(InScene(PelagoScene).With(WorldPelagoIsle)), StepWait(30) } };
        const ActionDef *def = key == ScreenKey::L ? &previous : key == ScreenKey::R ? &next
            : key == ScreenKey::Start ? &mohn : key == ScreenKey::B ? &PelagoLeaveAction() : nullptr;
        if (def) { m_busy = true; log.push_back(std::string("pelago: ") + def->name); }
        return def;
    }
    void PelagoScreen::ActionDone(std::vector<std::string> &log)
    {
        Begin(log); // fresh samples before accepting another input, no queued Yes
    }
    void PelagoScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace UI::Theme;
        canvas.FillRoundRect(PelagoArea, 4, WindowFill);
        s32 x = PelagoArea.x + 5;
        x += DrawButtonPrompt(canvas, x, 218, Button::L, "< Isle") + 5;
        x += DrawButtonPrompt(canvas, x, 218, Button::R, ">") + 12;
        x += DrawButtonPrompt(canvas, x, 218, Button::Start, "Mohn") + 12;
        DrawButtonPrompt(canvas, x, 218, Button::B, "Leave");
    }
    std::string PelagoScreen::Status() const
    {
        return m_busy ? "action running" : Ready() ? "isle; L/R isle, Start Mohn, B Leave" : "unrecognised/menu; game controls";
    }
}
