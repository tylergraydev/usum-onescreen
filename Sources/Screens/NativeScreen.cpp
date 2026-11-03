#include "Screens/NativeScreen.hpp"

#include <cstdio>
#include <list>

#include "UI/Layout.hpp"
#include "UI/Theme.hpp"

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

    void DrawWaterBackground(UI::Canvas &canvas, const UI::Rect &area)
    {
        // Row averages of the game's empty recorder, every 16 px of its 240.
        static const UI::Color stops[] = {
            UI::Rgb(0x8B, 0xFA, 0xE6), UI::Rgb(0x5E, 0xF5, 0xD9), UI::Rgb(0x25, 0xE4, 0xDC), UI::Rgb(0x10, 0xAF, 0xE7),
            UI::Rgb(0x07, 0x7D, 0xF1), UI::Rgb(0x02, 0x58, 0xCF), UI::Rgb(0x00, 0x2F, 0x98), UI::Rgb(0x10, 0x28, 0x77),
        };
        const int n = sizeof(stops) / sizeof(stops[0]);
        for (s32 dy = 0; dy < area.h; dy++)
        {
            const float t = area.h > 1 ? (float)dy * (n - 1) / (area.h - 1) : 0.f;
            const int i = t >= n - 1 ? n - 2 : (int)t;
            const float f = t - i;
            const UI::Color a = stops[i], b = stops[i + 1];
            const UI::Color c = UI::Rgb((u8)(a.r + (b.r - a.r) * f), (u8)(a.g + (b.g - a.g) * f), (u8)(a.b + (b.b - a.b) * f));
            canvas.HLine(area.x, area.y + dy, area.w, c);
        }
    }

    UI::Rect DrawMessageWindow(UI::Canvas &canvas, const UI::Rect &area, const std::string &text)
    {
        using namespace UI::Theme;
        std::vector<std::string> lines;
        std::string rest = text;
        for (;;)
        {
            const size_t nl = rest.find('\n');
            lines.push_back(rest.substr(0, nl));
            if (nl == std::string::npos)
                break;
            rest = rest.substr(nl + 1);
        }
        while (lines.size() > 1 && lines.back().empty())
            lines.pop_back();
        const s32 lineH = LineHeight(TextSize::Body);
        const UI::Rect box = { area.x, area.y, area.w, (s32)lines.size() * lineH + 2 * WindowPad + 4 };
        DrawWindow(canvas, box);
        TextStyle style;
        style.color = WindowText;
        for (size_t i = 0; i < lines.size(); i++)
            DrawText(canvas, lines[i], UI::Rect{ box.x + WindowPad + 4, box.y + WindowPad + 2 + (s32)i * lineH,
                                                 box.w - 2 * WindowPad - 8, lineH }, style);
        return UI::Rect{ area.x, box.Bottom() + 4, area.w, area.h - box.h - 4 };
    }

    UI::Rect DrawScreenPrompts(UI::Canvas &canvas, const UI::Rect &area, const std::vector<ScreenPrompt> &left,
                               const std::vector<ScreenPrompt> &right)
    {
        using namespace UI::Theme;
        const s32 y = area.Bottom() - PromptHeight - 2;
        s32 x = area.x + 4;
        for (const ScreenPrompt &p : left)
            x += DrawButtonPrompt(canvas, x, y, p.button, p.label) + ScreenStyle::PromptGap;
        s32 width = 0;
        for (const ScreenPrompt &p : right)
            width += PromptWidth(p.button, p.label) + ScreenStyle::PromptGap;
        x = area.Right() - 4 - width + ScreenStyle::PromptGap;
        for (const ScreenPrompt &p : right)
            x += DrawButtonPrompt(canvas, x, y, p.button, p.label) + ScreenStyle::PromptGap;
        return UI::Rect{ area.x, area.y, area.w, y - 2 - area.y };
    }
}
