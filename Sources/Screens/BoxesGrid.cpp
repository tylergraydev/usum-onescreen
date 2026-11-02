#include "Screens/BoxesGrid.hpp"

#include <cstdio>
#include <vector>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    using UI::Rgb;

    const char *BoxesSlotButton(u32 cursor)
    {
        static const std::vector<std::string> names = []
        {
            std::vector<std::string> list;
            char buf[32];
            for (u32 i = 0; i < BoxSlots + BoxPartySlots; i++)
            {
                if (i < BoxSlots)
                    std::snprintf(buf, sizeof(buf), "box.slot_%u", (unsigned)i);
                else
                    std::snprintf(buf, sizeof(buf), "box.party_%u", (unsigned)(i - BoxSlots));
                list.push_back(buf);
            }
            return list;
        }();
        return cursor < names.size() ? names[cursor].c_str() : nullptr;
    }

    // Action names and summaries must outlive the definitions, so all are
    // made once.
    struct BoxesActions
    {
        std::vector<std::string> names, summaries;
        std::vector<ActionDef> pick, drop, turn;

        BoxesActions(void)
        {
            const u32 count = BoxSlots + BoxPartySlots;
            names.reserve(2 * count + 2);
            summaries.reserve(2 * count + 2);
            const ActionCond idle = InScene(SceneBox).With(WorldBoxIdle);
            const ActionCond modal = InScene(SceneBox).With(WorldBoxModal);
            const ActionCond carrying = InScene(SceneBox).With(WorldBoxCarrying);
            char buf[64];

            for (u32 i = 0; i < count; i++)
            {
                const char *slot = BoxesSlotButton(i);
                std::snprintf(buf, sizeof(buf), "box-pick-%u", (unsigned)i);
                names.push_back(buf);
                summaries.push_back(std::string("Box: tap ") + slot + ", tap box_menu.move");
                // The slot menu slides in (~10 frames) before Move takes a tap.
                pick.push_back({ names.back().c_str(), summaries.back().c_str(), idle, {
                    StepTap(slot),
                    StepWaitFor(modal, 45).Retry(0, 1),
                    StepWait(10),
                    StepTap("box_menu.move"),
                    StepWaitFor(carrying, 60).Retry(3, 1),
                } });
            }
            for (u32 i = 0; i < count; i++)
            {
                const char *slot = BoxesSlotButton(i);
                std::snprintf(buf, sizeof(buf), "box-drop-%u", (unsigned)i);
                names.push_back(buf);
                summaries.push_back(std::string("Box: holding a Pokémon, tap ") + slot);
                // No re-tap: if the game refuses the drop it shows a message,
                // and a second tap would land on that.
                drop.push_back({ names.back().c_str(), summaries.back().c_str(), carrying, {
                    StepTap(slot),
                    StepWaitFor(idle, 90),
                } });
            }
            for (int next = 0; next < 2; next++)
            {
                names.push_back(next ? "box-next" : "box-prev");
                summaries.push_back(next ? "Box: tap box.next_box" : "Box: tap box.prev_box");
                turn.push_back({ names.back().c_str(), summaries.back().c_str(), InScene(SceneBox), {
                    StepTap(next ? "box.next_box" : "box.prev_box"),
                    StepWait(20),
                } });
            }
        }
    };

    static const BoxesActions &TheActions(void)
    {
        static const BoxesActions actions;
        return actions;
    }

    const ActionDef &BoxesPickAction(u32 cursor)
    {
        const std::vector<ActionDef> &list = TheActions().pick;
        return list[cursor < list.size() ? cursor : 0];
    }

    const ActionDef &BoxesDropAction(u32 cursor)
    {
        const std::vector<ActionDef> &list = TheActions().drop;
        return list[cursor < list.size() ? cursor : 0];
    }

    const ActionDef &BoxesTurnAction(bool next)
    {
        return TheActions().turn[next ? 1 : 0];
    }

    BoxesCommand BoxesCommandFor(u32 key, BoxMode mode, u32 cursor, bool occupied)
    {
        if (mode != BoxMode::Idle && mode != BoxMode::Carrying)
            return BoxesCommand::None;
        const bool slot = BoxesSlotButton(cursor) != nullptr;
        if (key == BoxesKeyY)
            return BoxesCommand::Details;
        if (key == BoxesKeyL)
            return BoxesCommand::PrevBox;
        if (key == BoxesKeyR)
            return BoxesCommand::NextBox;
        if (key == BoxesKeyA && slot)
        {
            if (mode == BoxMode::Carrying)
                return BoxesCommand::Drop;
            if (occupied)
                return BoxesCommand::Pick;
        }
        return BoxesCommand::None;
    }

    // --- Drawing ---

    static constexpr UI::Color PanelBg   = Rgb(22, 30, 52);
    static constexpr UI::Color Border    = Rgb(120, 150, 220);
    static constexpr UI::Color Divider   = Rgb(120, 150, 220, 90);
    static constexpr UI::Color TextMain  = Rgb(250, 252, 255);
    static constexpr UI::Color TextDim   = Rgb(150, 165, 200);
    static constexpr UI::Color Selected  = Rgb(255, 236, 120);
    static constexpr UI::Color Holding   = Rgb(255, 150, 60);
    static constexpr UI::Color BoxFill   = Rgb(46, 112, 70);    // the game's green box wallpaper
    static constexpr UI::Color PartyFill = Rgb(40, 96, 160);
    static constexpr UI::Color EmptyLine = Rgb(120, 150, 220, 110);

    // Inside BoxesArea: header (box name), 5 rows of 6 slots, the party in
    // one row of 6 (the game's cursor order: 30, 31, ... 35), footer.
    static const int GridX = BoxesArea.x + 6;
    static const int HeaderY = BoxesArea.y + 6, HeaderH = 24;
    static const int GridY = HeaderY + HeaderH + 6;
    static const int CellW = 60, CellH = 25, Gap = 2;
    static const int PartyGap = 10;
    static const int FooterH = 18;

    UI::Rect BoxesCellRect(u32 index)
    {
        if (index < BoxSlots)
            return UI::Rect{ GridX + (int)(index % BoxColumns) * (CellW + Gap),
                             GridY + (int)(index / BoxColumns) * (CellH + Gap), CellW, CellH };
        if (index < BoxSlots + BoxPartySlots)
            return UI::Rect{ GridX + (int)(index - BoxSlots) * (CellW + Gap),
                             GridY + (int)BoxRows * (CellH + Gap) - Gap + PartyGap, CellW, CellH };
        // The box name, between the arrows.
        return UI::Rect{ GridX + CellW + Gap, HeaderY, 4 * CellW + 3 * Gap, HeaderH };
    }

    static UI::Rect FooterRect(void)
    {
        const UI::Rect party = BoxesCellRect(BoxSlots);
        return UI::Rect{ GridX, party.Bottom() + 5, (int)BoxColumns * (CellW + Gap) - Gap, FooterH };
    }

    std::string BoxesFooter(const BoxesView &view)
    {
        if (!view.busy.empty())
            return view.busy;
        switch (view.mode)
        {
        case BoxMode::Carrying:
            return "Moving " + (view.held.empty() ? std::string("a Pokémon") : view.held)
                   + ":  A Drop   B Cancel   L/R Box";
        case BoxMode::Modal:
            return "The game is asking on the bottom screen (L+R shows it)";
        case BoxMode::Unknown:
            return "Bottom screen busy (L+R shows it)";
        default:
            break;
        }
        if (view.cursor == BoxCursorBoxList)
            return "Box List: use the bottom screen (L+R)";
        return "A Move   Y Details   L/R Box   B Exit";
    }

    static void DrawCell(UI::Canvas &canvas, const UI::Rect &r, const std::string &label, UI::Color fill,
                         bool cursor, bool holding, bool dim, const std::string &held)
    {
        if (!label.empty())
            canvas.FillRoundRect(r, 6, dim ? Rgb(fill.r / 2, fill.g / 2, fill.b / 2) : fill);
        else
            canvas.StrokeRoundRect(r, 6, EmptyLine, 1);
        if (cursor)
            canvas.StrokeRoundRect(UI::Inset(r, -2), 8, holding ? Holding : Selected, 2);

        UI::TextStyle style;
        style.color = dim ? TextDim : TextMain;
        style.align = UI::Align::Center;
        if (!label.empty())
            UI::DrawText(canvas, label, UI::Inset(r, 1, 0, 1, 0), style);
        else if (cursor && holding && !held.empty())
        {
            // Where the held Pokémon would go.
            style.color = Holding;
            UI::DrawText(canvas, held, UI::Inset(r, 1, 0, 1, 0), style);
        }
    }

    void DrawBoxes(UI::Canvas &canvas, const BoxesView &view)
    {
        canvas.FillRoundRect(BoxesArea, BoxesRadius, PanelBg);
        canvas.StrokeRoundRect(BoxesArea, BoxesRadius, Border, 2);
        const bool carrying = view.mode == BoxMode::Carrying;

        // Header: < Box name >, and which box of how many.
        const UI::Rect name = BoxesCellRect(BoxCursorName);
        if (view.cursor == BoxCursorName)
            canvas.StrokeRoundRect(UI::Inset(name, -2), 8, Selected, 2);
        canvas.FillRoundRect(name, 8, Rgb(52, 70, 110));
        UI::DrawIcon(canvas, UI::Icon::ArrowLeft, UI::Rect{ name.x - 26, name.y + 4, 16, 16 }, TextMain);
        UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ name.Right() + 10, name.y + 4, 16, 16 }, TextMain);
        UI::TextStyle title;
        title.align = UI::Align::Center;
        UI::DrawText(canvas, view.boxName, name, title);
        char count[24];
        std::snprintf(count, sizeof(count), "%u/%u", (unsigned)(view.tray + 1), (unsigned)view.unlocked);
        UI::TextStyle small;
        small.font = UI::Font::Small;
        small.color = TextDim;
        small.align = UI::Align::Right;
        const int countX = name.Right() + 28;
        UI::DrawText(canvas, count, UI::Rect{ countX, name.y, GridX + (int)BoxColumns * (CellW + Gap) - Gap - countX,
                                              name.h }, small);

        for (u32 i = 0; i < BoxSlots + BoxPartySlots; i++)
        {
            const bool party = i >= BoxSlots;
            const std::string &label = party ? view.party[i - BoxSlots] : view.slots[i];
            // The held Pokémon's own slot: the game shows it empty until the
            // drop; dim it here.
            const bool source = carrying && view.heldIndex == (int)i
                                && (party || view.heldTray == (int)view.tray);
            DrawCell(canvas, BoxesCellRect(i), label, party ? PartyFill : BoxFill, i == view.cursor, carrying,
                     source || !view.busy.empty(), view.held);
        }

        const UI::Rect party = BoxesCellRect(BoxSlots);
        canvas.HLine(GridX, party.y - PartyGap / 2 - 1, (int)BoxColumns * (CellW + Gap) - Gap, Divider);

        UI::TextStyle hint;
        hint.color = carrying || !view.busy.empty() ? TextMain : TextDim;
        if (carrying && view.busy.empty())
            hint.color = Holding;
        UI::DrawText(canvas, BoxesFooter(view), FooterRect(), hint);
    }

    void DrawBoxesBadge(UI::Canvas &canvas)
    {
        canvas.FillRoundRect(BoxesBadgeArea, 8, Rgb(22, 30, 52, 230));
        canvas.StrokeRoundRect(BoxesBadgeArea, 8, Border, 1);
        UI::TextStyle style;
        style.align = UI::Align::Center;
        UI::DrawText(canvas, "Y  Boxes", BoxesBadgeArea, style);
    }
}
