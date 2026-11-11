#pragma once

#include "Settings.hpp"
#include "UI/Raster.hpp"

namespace OneScreen
{
    // Pure rules shared by runtime and host tests. squeeze = aspect / (5/3).
    bool SidePanelsInclude(SidePanelMode mode, bool battle);
    bool SidePanelsBattleHook(float squeeze);
    bool SidePanelsBattleProjection(float squeeze, SidePanelMode mode, float aspect, unsigned pivot);
    void SidePanelsScaleProjection(float *matrix, float squeeze);
    bool SidePanelsTopRoot(const char *name, unsigned parent, float width, float height);
    float SidePanelsHudFit(float left, float right);
    void SidePanelsScaleHud(float *matrix, float scale);
    UI::Rect SidePanelArea(float squeeze, bool right = false);
    UI::Rect SidePanelContent(const UI::Rect &area);
    UI::Rect SidePanelGpuDestination(const UI::Rect &area, float squeeze);
    bool SidePanelsVisible(SidePanelMode mode, float squeeze, bool battle,
                           bool walking, bool covered);

    bool SidePanelsBattleOverlayAllows(const std::string &status);

    // Child lists use pane+4 as their intrusive node, +0xC parent, +0x14 sentinel.
    // Read returns a pointer-sized value so the same bounded walk is host tested.
    template<class Read, class Visit>
    bool VisitSidePanelPanes(uintptr_t root, Read read, Visit visit)
    {
        uintptr_t pane = root;
        for (unsigned count=0;count<1024;++count)
        {
            visit(pane);
            const uintptr_t head=pane+0x14, child=read(head);
            if(child!=head)
            {
                if(child<4 || (child&3)) return false;
                const uintptr_t next=child-4;
                if(read(next+0xC)!=pane) return false;
                pane=next; continue;
            }
            while(pane!=root)
            {
                const uintptr_t parent=read(pane+0xC);
                if(!parent) return false;
                const uintptr_t sibling=read(pane+4);
                if(sibling!=parent+0x14)
                {
                    if(sibling<4 || (sibling&3)) return false;
                    pane=sibling-4;
                    if(read(pane+0xC)!=parent) return false;
                    break;
                }
                pane=parent;
            }
            if(pane==root) return true;
        }
        return false;
    }

    struct SidePanelRow
    {
        char name[32] = {};
        unsigned short hp = 0, maxHp = 0;
        unsigned char level = 0;
        unsigned status = 0;
        bool present = false, statusKnown = false;
    };
    struct SidePanelView
    {
        SidePanelRow rows[6];
        SidePanelRow foe;
        bool battle = false;
    };
    bool SameSidePanelView(const SidePanelView &a, const SidePanelView &b);
    // Whole UTF-8 prefix fitting measured 5x7 ink, with a one-device-pixel gap.
    std::string SidePanelName(const std::string &name, float width, float rasterScale);
    void DrawSidePanel(UI::Canvas &canvas, const UI::Rect &area, const SidePanelView &view, bool right = false);

#ifdef __3DS__
    void StartSidePanels(void);
    void SidePanelsUpdate(unsigned sceneManager);
#endif
}
