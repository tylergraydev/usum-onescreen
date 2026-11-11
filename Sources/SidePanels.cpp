#include "SidePanels.hpp"
#include "Battle.hpp"
#include "Party.hpp"
#include "Panel.hpp"
#include "Scene.hpp"
#include "Widescreen.hpp"
#include "Screens/Map.hpp"
#include "Screens/BattleScreen.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

namespace OneScreen
{
    using namespace CTRPluginFramework;
    static Mutex s_lock;
    static SidePanelView s_view;
    static bool s_visible = false;
    static unsigned s_version = 0, s_frames = 0;
    // Two 96-row CPU caches, 43,008 bytes TOTAL. Full-height hi-res staging
    // remains in the existing atlas; fallback backgrounds need only one column.
    struct Strip
    {
        u8 fallback[56 * 96 * 4] = {};
        UI::Layer layer;
        bool configured = false, dirty = true, hires = false;
        unsigned drawn = ~0u;
        void Hide() { layer.Hide(); configured = false; }
    };
    static Strip s_strips[2];
    static u8 s_background[240*4];
    static int s_backgroundFormat = -1;

    static void CpuBackground(const UI::Surface &fb, const UI::Rect &area)
    {
        const unsigned bpp = UI::BytesPerPixel(fb.format);
        if (s_backgroundFormat != (int)fb.format)
        {
            for (int y=0;y<240;++y) UI::WritePixel(s_background+y*bpp,fb.format,20,29,32);
            s_backgroundFormat = (int)fb.format;
        }
        for(int x=area.x;x<area.Right();++x)
        {
            if (fb.stepY == (int)bpp || fb.stepY == -(int)bpp)
                std::memcpy(fb.origin+x*fb.stepX+(fb.stepY<0 ? 239*fb.stepY : 0),s_background,240*bpp);
            else for(int y=0;y<240;++y)
                std::memcpy(fb.origin+x*fb.stepX+y*fb.stepY,s_background+y*bpp,bpp);
        }
    }

    static bool PresentStrip(const UI::Surface &fb, float squeeze, bool right)
    {
        Strip &strip = s_strips[right ? 1 : 0];
        const UI::Rect full = SidePanelArea(squeeze,right);
        const UI::Rect small = SidePanelContent(full);
        const auto &backend = UI::Layer::s_hiRes;
        const bool available = backend.ready && backend.ready() && !SidePanelGpuDestination(full,squeeze).Empty();
        const auto old = strip.layer.Area();
        if (!strip.configured || !strip.layer.Matches(fb) || strip.hires != available
            || old.x != full.x || old.w != (strip.hires ? full.w : small.w))
        {
            const unsigned slot = right ? UI::RotomSlot : UI::BadgeSlot;
            strip.configured = available && strip.layer.SetupHiRes(fb,full,0,slot,strip.fallback,sizeof(strip.fallback));
            strip.hires = strip.configured && strip.layer.Pixels().height != full.h;
            if (!strip.hires)
                strip.configured = strip.layer.Setup(fb,small,0,strip.fallback,sizeof(strip.fallback));
            strip.dirty = true;
        }
        if (!strip.configured) return false;
        const auto &pixels = strip.layer.Pixels();
        if (strip.dirty || strip.drawn != s_version)
        {
            auto canvas = strip.layer.Begin();
            if (strip.hires)
            {
                // 57*2.5 rounds to 143: initialize every physical edge texel
                // opaque, including the fractional last column/row.
                UI::Canvas background(pixels);
                background.FillRect({0,0,pixels.width,pixels.height},UI::Rgb(20,29,32));
            }
            DrawSidePanel(canvas,strip.hires ? full : small,s_view,right);
            strip.dirty = true;
            strip.drawn = s_version;
        }
        if (strip.hires)
        {
            const unsigned slot = right ? UI::RotomSlot : UI::BadgeSlot;
            if (backend.bands) backend.bands(slot,fb,full);
            if (backend.present(slot,pixels,SidePanelGpuDestination(full,squeeze),strip.dirty)) strip.dirty=false;
            return false;
        }
        CpuBackground(fb,full);
        const unsigned bpp = UI::BytesPerPixel(fb.format);
        for(int x=0;x<small.w;++x) for(int y=0;y<small.h;++y)
            std::memcpy(fb.origin+(small.x+x)*fb.stepX+y*fb.stepY,
                        pixels.origin+x*pixels.stepX+y*pixels.stepY,bpp);
        strip.dirty=false;
        return true;
    }

    static bool OnScreen(const Screen &screen)
    {
        if (!screen.IsTop || s_lock.TryLock()) return false;
        const char *holder = UI::ScreenBuffer::Holder();
        const bool covered = holder && std::strcmp(holder,"battle");
        const float squeeze = WidescreenSqueeze();
        if (!s_visible || covered || GetPanelMode()!=PanelMode::Off || MapIsOpen() || SidePanelArea(squeeze).Empty())
        {
            for(auto &strip:s_strips) strip.Hide();
            s_lock.Unlock(); return false;
        }
        auto fb = SurfaceFromScreen(screen);
        fb.squeeze=1.f; fb.pillarbox=Pillarbox::Off;
        bool wrote=PresentStrip(fb,squeeze,false);
        if (s_view.battle) wrote=PresentStrip(fb,squeeze,true) || wrote;
        else s_strips[1].Hide();
        s_lock.Unlock();
        return wrote;
    }

    void StartSidePanels(void) { OSD::Run(OnScreen); }

    static SidePanelRow BattleRow(const BattleMon &mon)
    {
        SidePanelRow row;
        row.present=mon.species!=0;
        row.hp=mon.hp; row.maxHp=mon.maxHp; row.level=mon.level;
        const Pk7 param=ReadPokemonParam(mon.pokemonParam);
        row.statusKnown=param.form!=Pk7Form::Invalid && param.species==mon.species && param.calcValid;
        if(row.statusKnown) row.status=param.status;
        if(row.present)
        {
            const std::string name=SpeciesName(mon.species);
            if(name.empty()) std::snprintf(row.name,sizeof(row.name),"#%u",mon.species);
            else std::snprintf(row.name,sizeof(row.name),"%s",name.c_str());
        }
        return row;
    }

    void SidePanelsUpdate(unsigned sceneManager)
    {
        if (GetSettings().sidePanels == SidePanelMode::Off || WidescreenSqueeze() <= 1.f)
        {
            s_lock.Lock(); s_visible = false; s_lock.Unlock();
            return;
        }
        // Resolve current scene here, not main's 15-frame logging cache.
        const std::string scene = SceneModule(sceneManager);
        const bool battle = scene == "Battle";
        bool visible = SidePanelsVisible(GetSettings().sidePanels, WidescreenSqueeze(), battle,
            scene == "FieldRo" && FieldState(sceneManager) == 0,
            GetPanelMode() != PanelMode::Off || MapIsOpen());
        if (battle && !BattlePanelsHudActive()) visible = false;
        BattleState state;
        if (visible && battle)
        {
            state = ReadBattle(sceneManager);
            visible = (!BattleScreenEnabled() || SidePanelsBattleOverlayAllows(BattleScreenStatus()))
                && state.valid && (state.menu == BattleMenu::None || state.menu == BattleMenu::Command
                || state.menu == BattleMenu::Moves);
        }
        s_lock.Lock();
        const bool entering = visible && (!s_visible || s_view.battle != battle);
        if (!visible) s_visible = false;
        s_lock.Unlock();
        if (!visible || (++s_frames % 6 && !entering)) return;

        SidePanelView next;
        next.battle = battle;
        if (battle) next.foe = BattleRow(state.opponent);
        const PartyState party = battle ? PartyState{} : ReadParty(sceneManager);
        for (unsigned i = 0; i < 6; ++i)
        {
            auto &row = next.rows[i];
            unsigned short species = 0;
            if (battle && i < state.partyCount[0])
            {
                row = BattleRow(state.party[i]);
                continue;
            }
            else if (!battle && party.valid && i < party.count)
            {
                const auto &mon = party.slots[i].mon;
                row.present = mon.form != Pk7Form::Invalid;
                species = mon.species;
                if (mon.calcValid) { row.hp = mon.hp; row.maxHp = mon.maxHp; row.status = mon.status; }
                row.statusKnown = mon.calcValid;
            }
            if (row.present)
            {
                const std::string name = SpeciesName(species);
                if (name.empty()) std::snprintf(row.name, sizeof(row.name), "#%u", species);
                else std::snprintf(row.name, sizeof(row.name), "%s", name.c_str());
            }
        }
        s_lock.Lock();
        s_visible = true;
        if (!SameSidePanelView(next, s_view)) { s_view = next; ++s_version; }
        s_lock.Unlock();
    }
}
