#include "Screens/NativeScreen.hpp"

#include <cstdio>
#include <list>

#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    const ActionDef &ScreenPressAction(const char *name, const char *scene, u32 keys, u32 settle)
    {
        // A list keeps the definitions (and the strings they point to) in
        // place as more are added.
        struct Entry
        {
            std::string name, scene, summary;
            u32 keys, settle;
            ActionDef def;
        };
        static std::list<Entry> defs;
        for (const Entry &e : defs)
            if (e.name == name && e.scene == scene && e.keys == keys && e.settle == settle)
                return e.def;

        defs.push_back(Entry{ name, scene, "", keys, settle, ActionDef{ nullptr, nullptr, {}, {} } });
        Entry &e = defs.back();
        char summary[64];
        std::snprintf(summary, sizeof(summary), "%s: press %08X", scene, (unsigned)keys);
        e.summary = summary;
        e.def.name = e.name.c_str();
        e.def.summary = e.summary.c_str();
        e.def.needs = InScene(e.scene.c_str());
        e.def.steps.push_back(StepPress(keys));
        if (settle > 0)
            e.def.steps.push_back(StepWait(settle));
        return e.def;
    }

    static const s32 TitleH = 26, FooterH = 20;

    UI::Rect DrawScreenFrame(UI::Canvas &canvas, const UI::Rect &area, const std::string &title)
    {
        using namespace ScreenStyle;
        canvas.FillRoundRect(area, Radius, PanelBg);
        canvas.StrokeRoundRect(area, Radius, Border, 2);

        UI::TextStyle style;
        style.color = TextMain;
        const UI::Rect titleBox = { area.x + 14, area.y + 6, area.w - 28, TitleH - 6 };
        UI::DrawText(canvas, title, titleBox, style);
        canvas.HLine(area.x + 10, area.y + TitleH + 2, area.w - 20, Divider);
        return UI::Inset(area, 10, TitleH + 6, 10, 6);
    }

    UI::Rect DrawScreenFooter(UI::Canvas &canvas, const UI::Rect &area, const std::string &hints)
    {
        UI::TextStyle style;
        style.color = ScreenStyle::TextDim;
        const UI::Rect line = { area.x + 4, area.Bottom() - FooterH, area.w - 8, FooterH };
        UI::DrawText(canvas, hints, line, style);
        return UI::Rect{ area.x, area.y, area.w, area.h - FooterH - 2 };
    }
}
