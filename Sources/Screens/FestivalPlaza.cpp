#include "Screens/FestivalPlaza.hpp"
#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#include "Scene.hpp"
#endif

namespace OneScreen
{
    bool IsFestivalPlaza(const std::string &scene, bool subScreenLoaded)
    {
        return scene == "FieldRo" && subScreenLoaded;
    }
#ifdef __3DS__
    bool FestivalPlazaDetected(u32 sceneManager, const std::string &scene)
    {
        if (scene != "FieldRo") return false;
        using CTRPluginFramework::Process;
        u32 manager = 0;
        if (!Process::Read32(sceneManager, manager) || !manager) return false;
        // Observed after FieldRo: seven JoinFestaSubScreen responders.
        // Resolve ownership by CRO name; the research load address is not stable.
        for (u32 i = 0; i < 16; ++i)
        {
            u32 object = 0, vt = 0;
            if (!Process::Read32(manager + 4*i, object) || !object) break;
            if (Process::Read32(object, vt) && CroModuleName(CroModuleOf(vt)) == "JoinFestaSubScreen")
                return true;
        }
        return false;
    }
#endif
    const std::vector<SamplePoint> &FestivalPlazaSamplePoints()
    {
        // rs-fp1 and rs-fp-walk: identical circle interiors and icon colours.
        // Quit: both pill edges plus two dark message-border points (no text).
        static const std::vector<SamplePoint> points = {
            {40,113},{280,113},{103,203},{145,203},{187,203},
            {40,124},{280,124},{5,8},{5,38}
        };
        return points;
    }
    static bool Near(const Sample &s, const Sample &t)
    {
        const int tolerance = 20;
        return (int)s.r >= (int)t.r-tolerance && (int)s.r <= (int)t.r+tolerance
            && (int)s.g >= (int)t.g-tolerance && (int)s.g <= (int)t.g+tolerance
            && (int)s.b >= (int)t.b-tolerance && (int)s.b <= (int)t.b+tolerance;
    }
    FestivalView DecodeFestivalPlaza(const Sample *s)
    {
        if (Near(s[0],{229,55,14}) && Near(s[1],{47,212,229})
            && Near(s[2],{255,238,68}) && Near(s[3],{238,136,255}) && Near(s[4],{136,221,0}))
            return FestivalView::Hub;
        if (Near(s[0],{51,167,255}) && Near(s[1],{51,167,255})
            && Near(s[5],{113,187,253}) && Near(s[6],{113,187,253})
            && Near(s[7],{78,64,32}) && Near(s[8],{78,64,32}))
            return FestivalView::Quit;
        return FestivalView::Other;
    }
    u32 FestivalPlazaProbe::Update(FestivalView view)
    {
        m_hubHits = view == FestivalView::Hub ? (m_hubHits < 3 ? m_hubHits+1 : 3) : 0;
        if (view == FestivalView::Quit)
        {
            if (m_quitHits < 3) ++m_quitHits;
            m_quitAge = 0;
        }
        else if (view == FestivalView::Hub || (m_quitHits && ++m_quitAge > 6))
            m_quitHits = m_quitAge = 0;
        return m_hubHits >= 3 ? WorldFestivalHub : m_quitHits >= 3 ? WorldFestivalQuit : 0;
    }
    const ActionDef &FestivalPlazaLeaveAction()
    {
        static const ActionDef def = [] {
            const auto hub = InScene("FieldRo").With(WorldFestivalActive | WorldFestivalHub);
            const auto quit = InScene("FieldRo").With(WorldFestivalActive | WorldFestivalQuit).Without(WorldFestivalHub);
            return ActionDef{"festival-leave", "Festival Plaza: Back, Quit once, wait for walking field", hub, {
                StepTap("festival_plaza.back").Guard(hub), StepWaitFor(quit,240), StepWait(20),
                StepTap("festival_plaza.quit").Guard(quit),
                StepWaitFor(InScene("FieldRo").With(WorldFestivalField).Without(WorldFestivalActive),1200)
            }};
        }();
        return def;
    }
    u32 FestivalPlazaScreen::CaptureKeys() const
    {
        return Ready() ? ScreenKey::L | ScreenKey::R | ScreenKey::Start | ScreenKey::B : 0;
    }
    void FestivalPlazaScreen::Begin(std::vector<std::string> &log)
    {
        (void)log; m_probe = FestivalPlazaProbe{}; m_hub = m_busy = false;
    }
    void FestivalPlazaScreen::Sampled(const Sample *samples, std::vector<std::string> &log)
    {
        (void)log; m_hub = m_probe.Update(DecodeFestivalPlaza(samples)) == WorldFestivalHub;
    }
    const ActionDef *FestivalPlazaScreen::Key(u32 key, std::vector<std::string> &log)
    {
        if (!Ready()) return nullptr;
        const auto hub = InScene("FieldRo").With(WorldFestivalActive | WorldFestivalHub);
        static const ActionDef party = {"festival-party", "Festival Plaza: Pokemon", hub,
            {StepTap("festival_plaza.pokemon").Guard(hub),StepWait(30)}};
        static const ActionDef bag = {"festival-bag", "Festival Plaza: Bag", hub,
            {StepTap("festival_plaza.bag").Guard(hub),StepWait(30)}};
        static const ActionDef profile = {"festival-profile", "Festival Plaza: Profile", hub,
            {StepTap("festival_plaza.profile").Guard(hub),StepWait(30)}};
        const ActionDef *def = key == ScreenKey::L ? &party : key == ScreenKey::R ? &bag
            : key == ScreenKey::Start ? &profile : key == ScreenKey::B ? &FestivalPlazaLeaveAction() : nullptr;
        if (def) { m_busy = true; log.push_back(std::string("festival-plaza: ")+def->name); }
        return def;
    }
    void FestivalPlazaScreen::Draw(UI::Canvas &canvas) const
    {
        using namespace UI::Theme;
        canvas.FillRoundRect(FestivalPlazaArea,4,WindowFill);
        s32 x = FestivalPlazaArea.x+5;
        x += DrawButtonPrompt(canvas,x,218,Button::L,"Party")+3;
        x += DrawButtonPrompt(canvas,x,218,Button::R,"Bag")+3;
        x += DrawButtonPrompt(canvas,x,218,Button::Start,"Profile")+3;
        DrawButtonPrompt(canvas,x,218,Button::B,"Leave");
    }
    std::string FestivalPlazaScreen::Status() const
    {
        return m_busy ? "action running" : Ready() ? "hub; L Pokemon, R Bag, Start Profile, B Leave" : "unrecognised/menu; game controls";
    }
}
