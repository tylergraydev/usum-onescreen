#include "UI/Demo.hpp"
#include "UI/Theme.hpp"

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
namespace UI
{
    static constexpr Color Shade    = Rgb(0, 0, 0, 110);
    static constexpr Color PanelBg  = Rgb(22, 30, 52);
    static constexpr Color Border   = Rgb(120, 150, 220);
    static constexpr Color Divider  = Rgb(120, 150, 220, 90);
    static constexpr Color Highlight = Rgb(70, 110, 200, 210);
    static constexpr Color TextMain = Rgb(240, 244, 255);
    static constexpr Color TextDim  = Rgb(150, 165, 200);
    static constexpr Color Accent   = Rgb(110, 220, 140);
    static constexpr Color Danger   = Rgb(235, 110, 110);

    struct DemoRow
    {
        const char *label;
        const char *value;
        bool        checked;
    };

    static const DemoRow s_rows[DemoRows] =
    {
        { "Party",                                         "6/6",  false },
        { "Bag",                                           "",     false },
        { "Auto-save before battles",                      "On",   true  },
        { "Display and typography (gjpqy)",                "",     false },
        { "A very long entry that will not fit on one row", "",    false },
    };

    void DrawDemoShade(Canvas &canvas)
    {
        // Dims the game underneath so the panel reads as a modal menu. Blending
        // reads the framebuffer, which costs host speed in Azahar (docs/PERF.md).
        canvas.FillRect(Rect{ 0, 0, ScreenW, ScreenH }, Shade);
    }

    void DrawDemoMenu(Canvas &canvas, s32 selected)
    {
        const Rect panel = DemoPanel;
        canvas.FillRoundRect(panel, DemoRadius, PanelBg);
        canvas.StrokeRoundRect(panel, DemoRadius, Border, 2);

        Stack body(Inset(panel, 12, 6, 12, 8), Stack::Column, 4);

        // Title row: centered title, close icon on the right.
        Rect title = body.Next(26);
        TextStyle titleStyle;
        titleStyle.font = Font::Large;
        titleStyle.color = TextMain;
        titleStyle.align = Align::Center;
        DrawText(canvas, "Typography & layout", title, titleStyle);
        DrawIcon(canvas, Icon::Close, Rect{ title.Right() - 16, title.y + 5, 16, 16 }, Danger);
        const Rect rule = body.Next(1);
        canvas.HLine(rule.x, rule.y, rule.w, Divider);

        // Footer, taken from the bottom so the rows get whatever is left.
        const Rect footer = body.NextFromEnd(10);
        TextStyle hint;
        hint.font = Font::Small;
        hint.color = TextDim;
        DrawText(canvas, "uidemo off to close", footer, hint);
        hint.align = Align::Right;
        DrawText(canvas, Theme::FormatPromptText("A: select  B: back"), footer, hint);
        const Rect footRule = body.NextFromEnd(1);
        canvas.HLine(footRule.x, footRule.y, footRule.w, Divider);

        for (s32 i = 0; i < DemoRows; i++)
        {
            const DemoRow &row = s_rows[i];
            const Rect line = body.Next(22);
            if (i == selected)
            {
                canvas.FillRoundRect(line, 6, Highlight);
                DrawIcon(canvas, Icon::ArrowRight, Rect{ line.x + 2, line.y + 3, 16, 16 }, TextMain);
            }

            Stack cols(Inset(line, 22, 0, 6, 0), Stack::Row, 6);
            const Rect mark = cols.NextFromEnd(16);
            if (row.checked)
                DrawIcon(canvas, Icon::Check, Rect{ mark.x, mark.y + 3, 16, 16 }, Accent);
            else if (row.value[0] == '\0')
                DrawIcon(canvas, Icon::ArrowRight, Rect{ mark.x + 2, mark.y + 5, 12, 12 }, TextDim);

            TextStyle valueStyle;
            valueStyle.color = TextDim;
            valueStyle.align = Align::Right;
            if (row.value[0] != '\0')
                DrawText(canvas, row.value, cols.NextFromEnd(MeasureText(row.value, valueStyle)), valueStyle);

            TextStyle labelStyle;
            labelStyle.color = TextMain;
            DrawText(canvas, row.label, cols.Rest(), labelStyle);
        }

        // Scroll hints under the list, to show the up/down arrows.
        const Rect arrows = body.Next(12);
        DrawIcon(canvas, Icon::ArrowUp, Rect{ arrows.x + arrows.w / 2 - 14, arrows.y, 12, 12 }, TextDim);
        DrawIcon(canvas, Icon::ArrowDown, Rect{ arrows.x + arrows.w / 2 + 2, arrows.y, 12, 12 }, TextDim);
    }
}
}
