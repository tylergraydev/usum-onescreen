#include "SidePanels.hpp"
#include "UI/ColumnMapping.hpp"
#include "UI/Text.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace OneScreen
{
    bool SidePanelsInclude(SidePanelMode mode, bool battle)
    {
        return mode == SidePanelMode::Both || mode == (battle ? SidePanelMode::Battle : SidePanelMode::Field);
    }

    bool SidePanelsBattleHook(float squeeze) { return squeeze != 1.f; }

    bool SidePanelsBattleProjection(float squeeze, SidePanelMode mode, float aspect, unsigned pivot)
    {
        return std::isfinite(squeeze) && squeeze > 1.f && SidePanelsInclude(mode, true)
            && std::fabs(aspect - 400.f/240.f) <= .001f && (pivot == 0 || pivot == 4);
    }

    void SidePanelsScaleProjection(float *matrix, float squeeze)
    {
        for (unsigned i = 0; i < 4; ++i) matrix[i] /= squeeze;
    }

    bool SidePanelsTopRoot(const char *name, unsigned parent, float width, float height)
    {
        return !parent && !std::strcmp(name, "RootPane") && width == 400.f && height == 240.f;
    }

    float SidePanelsHudFit(float left, float right)
    {
        const float extent = std::max(-left, right);
        return std::isfinite(extent) && extent > 200.f ? 200.f / extent : 1.f;
    }

    void SidePanelsScaleHud(float *matrix, float scale)
    {
        for (unsigned i = 0; i < 4; ++i) matrix[i] *= scale;
    }

    UI::Rect SidePanelArea(float squeeze, bool right)
    {
        if (!std::isfinite(squeeze) || squeeze <= 1.f) return {};
        const auto centre = UI::SqueezedColumns(0, 400, squeeze);
        return right ? UI::Rect{centre.end, 0, 400-centre.end, 240}
                     : UI::Rect{0, 0, centre.begin, 240};
    }

    UI::Rect SidePanelContent(const UI::Rect &area)
    {
        // Both fallback caches together are smaller than the original 50-KiB strip.
        return {area.x, 0, std::min<UI::s32>(56, area.w), 96};
    }

    UI::Rect SidePanelGpuDestination(const UI::Rect &area, float squeeze)
    {
        // The existing quad pass squeezes EVERY quad. Undo it for this strip,
        // which already lives in framebuffer coordinates, without changing that pass.
        if (area.Empty() || !std::isfinite(squeeze) || squeeze < 1.f || squeeze > 16.f) return {};
        const int left = (int)std::lround(200.f + (area.x - 200.f) * squeeze);
        const int right = (int)std::lround(200.f + (area.Right() - 200.f) * squeeze);
        return {left, area.y, right - left, area.h};
    }

    bool SidePanelsVisible(SidePanelMode mode, float squeeze, bool battle, bool walking, bool covered)
    {
        return !covered && (battle || walking) && SidePanelsInclude(mode, battle)
            && !SidePanelArea(squeeze).Empty();
    }

    bool SidePanelsBattleOverlayAllows(const std::string &status)
    {
        // The existing read-only BattleScreenStatus reports its native page,
        // including Party/Bag retained across short game-menu transitions.
        // Unknown/new formats fail closed, so no battle-menu changes are needed.
        const auto start = status.find("; page ");
        if (start == std::string::npos) return false;
        const auto begin = start + 7;
        const auto end = status.find_first_of(",;", begin);
        const auto page = status.substr(begin, end - begin);
        return page == "hidden" || page == "command" || page == "moves";
    }

    static bool SameRow(const SidePanelRow &x, const SidePanelRow &y)
    {
        return !std::strcmp(x.name, y.name) && x.hp == y.hp && x.maxHp == y.maxHp
            && x.status == y.status && x.present == y.present && x.statusKnown == y.statusKnown
            && x.level == y.level;
    }

    bool SameSidePanelView(const SidePanelView &a, const SidePanelView &b)
    {
        if (a.battle != b.battle || !SameRow(a.foe, b.foe)) return false;
        for (int i = 0; i < 6; ++i) if (!SameRow(a.rows[i], b.rows[i])) return false;
        return true;
    }

    static void NameGlyph(const std::string &text, UI::u8 *mask, int &left, int &width)
    {
        UI::RenderSmallMask(text,mask,6,7,1);
        left=5;
        int right=-1;
        for(int x=0;x<5;++x) for(int y=0;y<7;++y)
            if(mask[y*6+x]) { left=std::min(left,x); right=std::max(right,x); }
        width=right<left ? 0 : right-left+1;
    }

    static float NameInkScale(const std::string &name, float width, float rasterScale)
    {
        float inkWidth=0.f;
        unsigned count=0;
        for(size_t i=0;i<name.size();)
        {
            const size_t begin=i;
            UI::NextCodePoint(name.data(),name.size(),i);
            UI::u8 mask[6*7];
            int left,ink;
            NameGlyph(name.substr(begin,i-begin),mask,left,ink);
            inkWidth+=ink ? ink : 2;
            ++count;
        }
        if(!count || !inkWidth) return 1.f;
        const float gap=1.f/std::max(1.f,rasterScale);
        // Keep native size when it fits; modest horizontal condensation is the
        // smaller-font fallback. More physical pixels retain finer 2.5x strokes.
        const float minimum=rasterScale>1.f ? .85f : .8f;
        return std::max(minimum,std::min(1.f,(width-(count-1)*gap)/inkWidth));
    }

    std::string SidePanelName(const std::string &name, float width, float rasterScale)
    {
        const float gap=1.f/std::max(1.f,rasterScale);
        const float inkScale=NameInkScale(name,width,rasterScale);
        float pen=0.f;
        size_t end=0;
        for(size_t i=0;i<name.size();)
        {
            const size_t begin=i;
            UI::NextCodePoint(name.data(),name.size(),i);
            UI::u8 mask[6*7];
            int left,ink;
            NameGlyph(name.substr(begin,i-begin),mask,left,ink);
            const float advance=(ink ? ink : 2)*inkScale;
            if(pen+advance>width+.0001f) break;
            pen+=advance+gap;
            end=i;
        }
        return name.substr(0,end);
    }

    static void DrawName(UI::Canvas &canvas, const std::string &name, const UI::Rect &box, UI::Color color)
    {
        const std::string fit=SidePanelName(name,(float)box.w,canvas.Scale());
        const float gap=1.f/std::max(1.f,canvas.Scale());
        const float inkScale=NameInkScale(name,(float)box.w,canvas.Scale());
        float pen=(float)box.x;
        for(size_t i=0;i<fit.size();)
        {
            const size_t begin=i;
            UI::NextCodePoint(fit.data(),fit.size(),i);
            UI::u8 mask[6*7];
            int left,ink;
            NameGlyph(fit.substr(begin,i-begin),mask,left,ink);
            if(ink) canvas.DrawMaskSized(pen,(float)box.y+1,mask+left,ink,7,6,ink*inkScale,7.f,color);
            pen+=(ink ? ink : 2)*inkScale+gap;
        }
    }

    void DrawSidePanel(UI::Canvas &canvas, const UI::Rect &area, const SidePanelView &view, bool right)
    {
        using namespace UI;
        const auto previousClip = canvas.GetClip();
        canvas.SetClip(Intersect(previousClip, area));
        canvas.FillRect(area, Rgb(20,29,32));
        const bool hires=canvas.Scale()>1.f;
        const Rect content = hires ? Rect{area.x,0,area.w,96} : SidePanelContent(area);
        const int x = content.x + 1, w = std::max<int>(1, content.w - 2);
        const TextStyle text{Font::Small, 1, Rgb(235,240,229), Align::Left};
        const bool wide = w >= 32;
        if (wide) DrawText(canvas, right ? "FOE" : view.battle ? "PARTY" : "TEAM", {x,3,w,9},text);
        const auto row = [&](const SidePanelRow &mon, int index, int y) {
            const char *status = !mon.present ? "-" : !mon.maxHp ? "?" : !mon.hp ? "F"
                : !mon.statusKnown ? "?" : mon.status == 0 ? "" : mon.status < 6 ? "PSFBP"+mon.status-1 : "?";
            // One line: shortened name, HP bar, one status glyph. Healthy has no glyph.
            char glyph[2] = {status[0], 0};
            const Color hp = mon.hp*4 <= mon.maxHp ? Rgb(233,85,80)
                           : mon.hp*2 <= mon.maxHp ? Rgb(232,194,72) : Rgb(87,205,132);
            const int barWidth = wide ? (hires ? 10 : 7) : w;
            const int statusWidth = wide && glyph[0] ? 6 : 0;
            const int barX = wide ? x+w-statusWidth-(statusWidth ? 1 : 0)-barWidth : x;
            if (wide)
            {
                const Rect nameBox{x,y,std::max(1,barX-x-1),9};
                canvas.SetClip(Intersect(previousClip,nameBox));
                DrawName(canvas,mon.present ? mon.name : "-",nameBox,text.color);
                canvas.SetClip(Intersect(previousClip,area));
                TextStyle condition = text;
                condition.color = !mon.hp ? Rgb(233,85,80) : !mon.statusKnown ? Rgb(160,160,160)
                    : mon.status==1 ? Rgb(232,194,72) : mon.status==3 ? Rgb(100,215,245)
                    : mon.status==4 ? Rgb(240,136,70) : Rgb(215,132,230);
                if(statusWidth) DrawText(canvas,glyph,{x+w-6,y,6,9},condition);
            }
            else
            {
                DrawText(canvas, std::to_string(index+1), {x,y,w,9},text);
            }
            const int by = wide ? y+3 : y+9;
            const int barHeight = wide ? 4 : 3;
            canvas.FillRect({barX,by,barWidth,barHeight},Rgb(62,73,76));
            if (mon.present && mon.maxHp && mon.hp)
                canvas.FillRect({barX,by,std::max(1,barWidth*std::min(mon.hp,mon.maxHp)/mon.maxHp),barHeight},hp);
            if (!wide && mon.present && (!mon.statusKnown || mon.status || !mon.hp))
                canvas.FillRect({x+w-1,y,1,7},mon.statusKnown ? Rgb(215,132,230) : Rgb(150,150,150));
        };
        if (right)
        {
            row(view.foe,0,18);
            if (wide)
            {
                DrawText(canvas, view.foe.present ? "Lv"+std::to_string(view.foe.level) : "No data",{x,32,w,9},text);
            }
        }
        else for (int i=0;i<6;++i) row(view.rows[i],i,17+i*12);
        canvas.SetClip(previousClip);
    }
}
