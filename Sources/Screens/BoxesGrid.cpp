#include "Screens/BoxesGrid.hpp"

#include <cstdio>
#include <vector>

#include "UI/GameAssets.hpp"
#include "UI/Layout.hpp"
#include "UI/PokeIcons.hpp"
#include "UI/Text.hpp"
#include "UI/Theme.hpp"

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

    namespace Theme = UI::Theme;

    // Measured on the game's Box screen (bottom screen, docs/BOXES.md).
    static constexpr UI::Color FrameLine  = Rgb(0x10, 0x22, 0x0E);   // dark line around the wallpaper
    static constexpr UI::Color GrassTop   = Rgb(0x52, 0xAD, 0x4A);   // grass wallpaper, top to bottom
    static constexpr UI::Color GrassBot   = Rgb(0x37, 0x8A, 0x2F);
    static constexpr UI::Color GrassTuft  = Rgb(0x61, 0xB8, 0x58);
    static constexpr UI::Color TabRim     = Rgb(0xCC, 0xDD, 0xFF);   // box name tab: pale rim, slate line,
    static constexpr UI::Color TabLine    = Rgb(0x33, 0x55, 0x77);   // the wallpaper's picture inside
    static constexpr UI::Color TabTop     = Rgb(0x77, 0xBB, 0x99);
    static constexpr UI::Color TabBot     = Rgb(0x4E, 0x82, 0x60);
    static constexpr UI::Color ArrowBlue  = Rgb(0x11, 0x99, 0xFF);
    static constexpr UI::Color ArrowDark  = Rgb(0x00, 0x66, 0xCC);
    static constexpr UI::Color PartyPanel = Rgb(0x20, 0x83, 0x18);   // the party's dark green panel
    // Ours (the game shows Pokémon icons on the bare wallpaper).
    static constexpr UI::Color MonPlate   = Rgb(0x10, 0x40, 0x10, 0x8C);
    static constexpr UI::Color EmptyLine  = Rgb(0xFF, 0xFF, 0xFF, 0x46);
    static constexpr UI::Color DimText    = Rgb(0xB0, 0xC8, 0xAC);
    static constexpr UI::Color White      = Rgb(0xFF, 0xFF, 0xFF);

    // Inside BoxesArea: header (< box name >, the cursor's Pokémon), 5 rows
    // of 6 slots, the party as one row of 6 list rows (the game's cursor
    // order: 30, 31, ... 35), the button prompts.
    static const int CellW = 60, CellH = 25, Gap = 2;
    static const int GridX = BoxesArea.x + 6;
    static const int GridW = (int)BoxColumns * (CellW + Gap) - Gap;
    static const int HeaderY = BoxesArea.y + 5, HeaderH = 24;
    static const int GridY = HeaderY + HeaderH + 5;
    static const int PartyGap = 8, PartyH = Theme::RowHeight;
    static const int ArrowW = 16;
    static const int TabW = 170;
    // Where a box icon sits in its cell: its left edge (the pictures have
    // about 9 clear columns on the left), how far its bottom reaches below
    // the cell (their feet are on row 28 of 30), the name line above it.
    static const int IconX = -6, IconDrop = 2, NameH = 13;

    UI::Rect BoxesCellRect(u32 index)
    {
        if (index < BoxSlots)
            return UI::Rect{ GridX + (int)(index % BoxColumns) * (CellW + Gap),
                             GridY + (int)(index / BoxColumns) * (CellH + Gap), CellW, CellH };
        if (index < BoxSlots + BoxPartySlots)
            return UI::Rect{ GridX + (int)(index - BoxSlots) * (CellW + Gap),
                             GridY + (int)BoxRows * (CellH + Gap) - Gap + PartyGap, CellW, PartyH };
        // The box name tab, between the arrows.
        return UI::Rect{ GridX + ArrowW + 4, HeaderY, TabW, HeaderH };
    }

    static UI::Rect FooterRect(void)
    {
        const UI::Rect party = BoxesCellRect(BoxSlots);
        return UI::Rect{ GridX, party.Bottom() + 3, GridW, Theme::PromptHeight };
    }

    // Right of the tab's right arrow: the Pokémon at the cursor (or held).
    static UI::Rect InfoRect(void)
    {
        const UI::Rect name = BoxesCellRect(BoxCursorName);
        const int x = name.Right() + 4 + ArrowW + 6;
        return UI::Rect{ x, HeaderY, GridX + GridW - x, HeaderH };
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

    static UI::Color Mix(UI::Color a, UI::Color b, int num, int den)
    {
        return Rgb((u8)(a.r + (b.r - a.r) * num / den), (u8)(a.g + (b.g - a.g) * num / den),
                   (u8)(a.b + (b.b - a.b) * num / den), (u8)(a.a + (b.a - a.a) * num / den));
    }

    // A vertical gradient in a rounded rect: the rounded ends solid, the
    // straight middle line by line.
    static void FillGradient(UI::Canvas &canvas, const UI::Rect &r, int radius, UI::Color top, UI::Color bottom)
    {
        canvas.FillRoundRect(UI::Rect{ r.x, r.y, r.w, 2 * radius }, radius, top);
        canvas.FillRoundRect(UI::Rect{ r.x, r.Bottom() - 2 * radius, r.w, 2 * radius }, radius, bottom);
        for (int y = r.y + radius; y < r.Bottom() - radius; y++)
            canvas.HLine(r.x, y, r.w, Mix(top, bottom, y - r.y, r.h - 1));
    }

    // The game's blue arrow beside the box name, pointing left or right.
    static void DrawArrow(UI::Canvas &canvas, int x, int cy, bool right)
    {
        const float tip = right ? x + ArrowW : x, back = right ? x : x + ArrowW;
        const float in = right ? -1.0f : 1.0f;     // from the tip towards the back
        canvas.FillTriangle(tip, cy, back, cy - 10, back, cy + 10, White);
        canvas.FillTriangle(tip + 3 * in, cy, back - in, cy - 7, back - in, cy + 7, ArrowDark);
        canvas.FillTriangle(tip + 4 * in, cy - 1, back - in, cy - 6, back - in, cy, ArrowBlue);
    }

    // The 24x24 middle of the game's ball icon (the ball fills it; the rest
    // of the 32x32 icon is clear). Nothing while the icons aren't loaded.
    static void DrawBallIcon(UI::Canvas &canvas, int x, int y, u8 ball)
    {
        const UI::GameAssets *assets = UI::GetGameAssets();
        const UI::Image *icon = assets != nullptr && ball >= 1 && ball <= 16 ? assets->Ball(ball) : nullptr;
        if (icon == nullptr)
            return;
        const UI::Rect src{ 4, 4, 24, 24 };
        UI::DrawImage(canvas, *icon, x, y, &src);
    }

    // The icon on the cell's left, 1:1 as the game shows it, its feet on
    // the plate's bottom edge; it may reach into the gaps above and below
    // (2 px), never into a neighbour.
    static void DrawCellIcon(UI::Canvas &canvas, const UI::Rect &r, const UI::PokeIcon *icon, bool dim)
    {
        if (icon == nullptr)
            return;
        const UI::Rect clip = UI::Intersect(canvas.GetClip(), UI::Rect{ r.x, r.y - Gap, r.w, r.h + 2 * Gap });
        const UI::Rect saved = canvas.GetClip();
        canvas.SetClip(clip);
        UI::DrawPokeIcon(canvas, *icon, r.x + IconX, r.Bottom() + IconDrop - UI::PokeIconH, dim ? 110 : 255);
        canvas.SetClip(saved);
    }

    static void DrawCell(UI::Canvas &canvas, const UI::Rect &r, const std::string &label, bool party,
                         bool cursor, bool holding, bool dim, const std::string &held,
                         const UI::PokeIcon *icon, const UI::PokeIcon *heldIcon)
    {
        Theme::TextStyle style;
        style.size = Theme::TextSize::Small;
        style.align = UI::Align::Center;
        if (label.empty())
            canvas.StrokeRoundRect(r, 6, EmptyLine, 1);
        else if (party)
        {
            Theme::DrawRowBackground(canvas, r, Theme::RowKind::List);
            if (dim)
                canvas.FillRoundRect(r, Theme::RowRadius, Rgb(0x20, 0x40, 0x20, 0x80));
            style.color = Theme::ListText;
        }
        else
        {
            canvas.FillRoundRect(r, 6, dim ? Rgb(MonPlate.r, MonPlate.g, MonPlate.b, MonPlate.a / 2) : MonPlate);
            style.color = dim ? DimText : White;
            style.outline = dim ? Rgb(0, 0, 0, 0) : FrameLine;
        }

        // Without an icon the name is centred in the cell. With one, the
        // icon stands on the left and the name runs along the top right:
        // the icons' pictures sit low (feet on the bottom row), so a long
        // name covers at most the top of the picture. Outlined in the box,
        // so it reads over the picture.
        const UI::Rect centred = UI::Inset(r, 2, 0, 2, party ? 2 : 0);
        const UI::Rect top{ r.x + 2, r.y + (party ? 1 : 0), r.w - 4 - (party ? 4 : 0), NameH };
        if (!label.empty())
        {
            DrawCellIcon(canvas, r, icon, dim);
            if (icon != nullptr)
            {
                style.align = UI::Align::Right;
                // On the party's light rows: a light rim keeps the dark
                // letters apart from the picture.
                if (party && !dim)
                    style.outline = Theme::ListTop;
            }
            Theme::DrawText(canvas, label, icon != nullptr ? top : centred, style);
        }
        else if (cursor && holding && !held.empty())
        {
            // Where the held Pokémon would go.
            DrawCellIcon(canvas, r, heldIcon, false);
            if (heldIcon != nullptr)
                style.align = UI::Align::Right;
            style.color = White;
            style.outline = Theme::CursorRed;
            Theme::DrawText(canvas, held, heldIcon != nullptr ? top : centred, style);
        }

        if (cursor)
        {
            // The orange frame of the game's selected plate; red while
            // holding. Last, so an icon reaching into the gap stays under it.
            const UI::Rect ring = UI::Inset(r, -2);
            if (holding)
                canvas.StrokeRoundRect(ring, 8, Theme::CursorRed, 2);
            else
                Theme::DrawHighlightFrame(canvas, ring, 8);
        }
    }

    static void DrawPrompts(UI::Canvas &canvas, const UI::Rect &r,
                            const std::vector<std::pair<Theme::Button, std::string>> &prompts)
    {
        int x = r.x;
        for (const auto &p : prompts)
            x += Theme::DrawButtonPrompt(canvas, x, r.y, p.first, p.second) + (p.second.empty() ? 2 : 6);
    }

    void DrawBoxes(UI::Canvas &canvas, const BoxesView &view, const UI::PokeIconCache *icons)
    {
        const bool carrying = view.mode == BoxMode::Carrying;

        // Wallpaper: dark frame, grass gradient, a few lighter tufts.
        canvas.FillRoundRect(BoxesArea, BoxesRadius, FrameLine);
        FillGradient(canvas, UI::Inset(BoxesArea, 2), BoxesRadius - 2, GrassTop, GrassBot);
        for (int y = GridY + 10, row = 0; y < BoxesArea.Bottom() - 8; y += 18, row++)
            for (int x = GridX + 4 + (row % 2) * 31; x < GridX + GridW - 4; x += 62)
            {
                canvas.FillTriangle(x, y + 4, x + 2, y, x + 3, y + 4, GrassTuft);
                canvas.FillTriangle(x + 3, y + 4, x + 5, y - 1, x + 6, y + 4, GrassTuft);
            }

        // Header: < Box name > with which box of how many; the Pokémon at
        // the cursor in a window beside it, with its ball.
        const UI::Rect name = BoxesCellRect(BoxCursorName);
        const int radius = name.h / 2;
        canvas.FillRoundRect(name, radius, TabRim);
        canvas.FillRoundRect(UI::Inset(name, 1), radius - 1, TabLine);
        FillGradient(canvas, UI::Inset(name, 3), radius - 3, TabTop, TabBot);
        if (view.cursor == BoxCursorName)
            Theme::DrawHighlightFrame(canvas, UI::Inset(name, -2), radius + 2);
        DrawArrow(canvas, name.x - 4 - ArrowW, name.y + name.h / 2, false);
        DrawArrow(canvas, name.Right() + 4, name.y + name.h / 2, true);
        Theme::TextStyle title;
        title.color = White;
        title.outline = TabLine;
        title.align = UI::Align::Center;
        Theme::DrawText(canvas, view.boxName, UI::Inset(name, 34, 0, 34, 0), title);
        char count[24];
        std::snprintf(count, sizeof(count), "%u/%u", (unsigned)(view.tray + 1), (unsigned)view.unlocked);
        Theme::TextStyle small;
        small.size = Theme::TextSize::Small;
        small.color = White;
        small.outline = TabLine;
        small.align = UI::Align::Right;
        Theme::DrawText(canvas, count, UI::Inset(name, 0, 0, 10, 0), small);

        const UI::Rect info = InfoRect();
        Theme::DrawWindow(canvas, info);
        std::string what;
        u8 ball = 0;
        if (carrying)
        {
            what = view.held.empty() ? std::string("Moving") : view.held;
            ball = view.heldBall;
        }
        else if (view.cursor < BoxSlots)
        {
            what = view.slots[view.cursor];
            ball = view.slotBalls[view.cursor];
        }
        else if (view.cursor < BoxSlots + BoxPartySlots)
        {
            what = view.party[view.cursor - BoxSlots];
            ball = view.partyBalls[view.cursor - BoxSlots];
        }
        else if (view.cursor == BoxCursorName)
            what = view.boxName;
        else if (view.cursor == BoxCursorBoxList)
            what = "Box List";
        DrawBallIcon(canvas, info.x + 3, info.y, ball);
        Theme::TextStyle infoText;
        infoText.color = carrying ? Theme::Highlight : Theme::WindowText;
        Theme::DrawText(canvas, what, UI::Inset(info, 29, 0, Theme::WindowPad, 0), infoText);

        // The box, then the party on its dark green panel.
        const UI::Rect first = BoxesCellRect(BoxSlots);
        canvas.FillRoundRect(UI::Rect{ GridX - 3, first.y - 3, GridW + 6, first.h + 6 }, Theme::RowRadius + 3,
                             PartyPanel);
        for (u32 i = 0; i < BoxSlots + BoxPartySlots; i++)
        {
            const bool party = i >= BoxSlots;
            const std::string &label = party ? view.party[i - BoxSlots] : view.slots[i];
            // The held Pokémon's own slot: the game shows it empty until the
            // drop; dim it here.
            const bool source = carrying && view.heldIndex == (int)i
                                && (party || view.heldTray == (int)view.tray);
            const u16 iconIndex = party ? view.partyIcons[i - BoxSlots] : view.slotIcons[i];
            const UI::PokeIcon *icon = icons != nullptr && iconIndex != 0 ? icons->Find(iconIndex) : nullptr;
            const UI::PokeIcon *heldIcon = icons != nullptr && view.heldIcon != 0 ? icons->Find(view.heldIcon) : nullptr;
            DrawCell(canvas, BoxesCellRect(i), label, party, i == view.cursor, carrying,
                     source || !view.busy.empty(), view.held, icon, heldIcon);
            const bool shiny = party ? view.partyShiny[i - BoxSlots] : view.slotShiny[i];
            if (shiny && !label.empty())
            {
                const UI::Rect cell = BoxesCellRect(i);
                Theme::DrawShiny(canvas, cell.Right() - 11, cell.y + 1);
            }
        }

        // Footer: the button prompts while the screen takes keys, else what
        // is happening in a window.
        const UI::Rect footer = FooterRect();
        const bool waiting = view.busy.empty() && view.cursor != BoxCursorBoxList
                             && (view.mode == BoxMode::Idle || carrying);
        if (waiting && carrying)
            DrawPrompts(canvas, footer, { { Theme::Button::A, "Drop" }, { Theme::Button::B, "Cancel" },
                                          { Theme::Button::L, "" }, { Theme::Button::R, "Box" } });
        else if (waiting)
            DrawPrompts(canvas, footer, { { Theme::Button::A, "Move" }, { Theme::Button::Y, "Details" },
                                          { Theme::Button::L, "" }, { Theme::Button::R, "Box" },
                                          { Theme::Button::B, "Exit" } });
        else
        {
            Theme::DrawWindow(canvas, footer);
            Theme::TextStyle text;
            text.size = Theme::TextSize::Small;
            text.color = Theme::WindowText;
            Theme::DrawText(canvas, BoxesFooter(view), UI::Inset(footer, Theme::WindowPad, 0, Theme::WindowPad, 0),
                            text);
        }
    }

    void DrawBoxesBadge(UI::Canvas &canvas)
    {
        // A prompt chip on the layer's black (it is copied whole, rounded).
        const int w = Theme::PromptWidth(Theme::Button::Y, "Boxes");
        Theme::DrawButtonPrompt(canvas, BoxesBadgeArea.x + (BoxesBadgeArea.w - w) / 2,
                                BoxesBadgeArea.y + (BoxesBadgeArea.h - Theme::PromptHeight) / 2, Theme::Button::Y, "Boxes");
    }
}
