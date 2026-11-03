#include "Screens/PokedexList.hpp"

#include <algorithm>
#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"

namespace OneScreen
{
    using UI::Rgb;

    const char *DexName(DexKind dex)
    {
        static const char *const names[] = { "Alola", "Melemele", "Akala", "Ula'ula", "Poni" };
        return (int)dex < DexKinds ? names[(int)dex] : "?";
    }

    const char *DexId(DexKind dex)
    {
        static const char *const ids[] = { "alola", "melemele", "akala", "ulaula", "poni" };
        return (int)dex < DexKinds ? ids[(int)dex] : "?";
    }

    DexScreen DexScreenFromVtable(u32 offsetInModule)
    {
        // Zukan module offsets of the frame classes (docs/RE7-pokedex.md).
        switch (offsetInModule)
        {
        case 0x34CD4: return DexScreen::Top;
        case 0x34DD8: return DexScreen::List;
        case 0x34F68: return DexScreen::Grid;
        case 0x34C24: return DexScreen::Entry;
        default:      return DexScreen::Other;
        }
    }

    const char *DexScreenName(DexScreen screen)
    {
        static const char *const names[] = { "none", "top", "list", "grid", "entry", "other" };
        return (int)screen < 6 ? names[(int)screen] : "?";
    }

    // --- Flags ---

    // Last flag bit of the base forms (species 807 -> bit 806), where the
    // extra forms start (0x4C8060).
    static const int FormBitBase = 0x326;
    static const u16 MaxSpecies = 807;

    int DexFormBit(u16 species, u16 form, const DexFormEntry *table, int count)
    {
        if (species == 0 || species > MaxSpecies)
            return -1;
        if (form == 0)
            return species - 1;
        int next = FormBitBase;
        for (int i = 0; i < count; i++)
        {
            if (table[i].species == species)
                return form < table[i].forms ? next + form : -1;
            next += table[i].forms - 1;
        }
        return -1;
    }

    u16 DexFormCount(u16 species, const DexFormEntry *table, int count)
    {
        for (int i = 0; i < count; i++)
            if (table[i].species == species)
                return table[i].forms;
        return 1;
    }

    static bool Bit(const u8 *base, int bit)
    {
        return (base[bit >> 3] >> (bit & 7)) & 1;
    }

    DexState DexStateOf(const u8 *zukan, u16 species, const DexFormEntry *table, int count)
    {
        if (species == 0 || species > MaxSpecies)
            return DexState::Unseen;
        if (Bit(zukan + DexZukanCaught, species - 1))
            return DexState::Caught;
        const u16 forms = DexFormCount(species, table, count);
        for (u16 form = 0; form < forms; form++)
        {
            const int bit = DexFormBit(species, form, table, count);
            if (bit < 0 || bit >= 0x8C * 8)
                continue;
            for (u32 region : DexZukanSeen)
                if (Bit(zukan + region, bit))
                    return DexState::Seen;
        }
        return DexState::Unseen;
    }

    // --- Rows ---

    std::vector<DexRow> DexRows(const std::vector<DexRecord> &records, DexKind dex, u32 limit)
    {
        std::vector<DexRow> rows;
        if ((int)dex >= DexKinds)
            return rows;
        for (const DexRecord &r : records)
        {
            const u16 number = r.number[(int)dex];
            if (number == 0)
                continue;
            DexRow row;
            row.number = number;
            row.species = r.species;
            rows.push_back(row);
        }
        std::sort(rows.begin(), rows.end(), [](const DexRow &a, const DexRow &b) { return a.number < b.number; });
        if (limit > 0 && rows.size() > limit)
            rows.resize(limit);
        return rows;
    }

    DexCounts DexCount(const std::vector<DexRow> &rows)
    {
        DexCounts c;
        c.total = (u16)rows.size();
        for (const DexRow &r : rows)
        {
            if (r.state != DexState::Unseen)
                c.seen++;
            if (r.state == DexState::Caught)
                c.caught++;
        }
        return c;
    }

    int DexWindowTop(int cursor, int count, int visible)
    {
        if (count <= visible || visible <= 0)
            return 0;
        const int top = cursor - (visible - 1) / 2;
        return std::max(0, std::min(top, count - visible));
    }

    DexWindow MakeDexWindow(const std::vector<DexRow> &rows, int cursor, int visible)
    {
        DexWindow w;
        w.count = (int)rows.size();
        w.cursor = std::max(0, std::min(cursor, w.count - 1));
        w.first = DexWindowTop(w.cursor, w.count, visible);
        for (int i = w.first; i < w.count && i < w.first + visible; i++)
            w.rows.push_back(rows[i]);
        return w;
    }

    std::string DescribeDexRow(const DexRow &row)
    {
        static const char *const states[] = { "unseen", "seen", "caught" };
        char text[80];
        std::snprintf(text, sizeof(text), "#%03u %s %s", row.number,
                      row.state == DexState::Unseen || row.name.empty() ? "?????" : row.name.c_str(),
                      states[(int)row.state]);
        return text;
    }

    // --- Drawing ---

    UI::Rect PokedexArea(DexScreen screen)
    {
        switch (screen)
        {
        // The game's top screen here is a title card; its completion
        // percentage (bottom right) stays visible.
        case DexScreen::Top:   return UI::Rect{ 12, 8, PokedexMaxW, PokedexMaxH };
        // Right of the 3D model and its name plate (bottom left).
        case DexScreen::List:  return UI::Rect{ 196, 8, 196, 224 };
        // Top right, above the model; the island map and habitat stay.
        case DexScreen::Grid:  return UI::Rect{ 236, 4, 158, 88 };
        // Top left; the model stands in the middle.
        case DexScreen::Entry: return UI::Rect{ 8, 8, 150, 124 };
        default:               return UI::Rect{ 0, 0, 0, 0 };
        }
    }

    static constexpr UI::Color PanelBg  = Rgb(30, 22, 30, 235);
    static constexpr UI::Color Border   = Rgb(232, 72, 72);
    static constexpr UI::Color TextMain = Rgb(250, 250, 252);
    static constexpr UI::Color TextDim  = Rgb(160, 150, 160);
    static constexpr UI::Color Selected = Rgb(255, 236, 120);
    static constexpr UI::Color RowSel   = Rgb(200, 52, 60);
    static constexpr UI::Color BallRed  = Rgb(232, 56, 56);
    static constexpr UI::Color BallDark = Rgb(90, 90, 98);
    // Row colours of the game's top level, by row (Alola, then the islands).
    static constexpr UI::Color DexColors[DexKinds] = {
        Rgb(224, 48, 48), Rgb(232, 112, 32), Rgb(70, 150, 60), Rgb(214, 170, 30), Rgb(140, 80, 190),
    };
    static constexpr UI::Color EvalColor = Rgb(236, 200, 40);
    static constexpr UI::Color LockedBg = Rgb(70, 44, 32);

    // Caught: a small red and white ball; seen: a dark dot (as the game).
    static void DrawMark(UI::Canvas &canvas, DexState state, int cx, int cy)
    {
        if (state == DexState::Caught)
        {
            canvas.FillRoundRect(UI::Rect{ cx - 6, cy - 6, 12, 12 }, 6, Rgb(250, 250, 250));
            canvas.FillRoundRect(UI::Rect{ cx - 6, cy - 6, 12, 6 }, 5, BallRed);
            canvas.HLine(cx - 6, cy, 12, Rgb(20, 20, 20));
            canvas.FillRoundRect(UI::Rect{ cx - 2, cy - 2, 4, 4 }, 2, Rgb(20, 20, 20));
        }
        else if (state == DexState::Seen)
            canvas.FillRoundRect(UI::Rect{ cx - 5, cy - 5, 10, 10 }, 5, BallDark);
    }

    static std::string RowName(const DexRow &row)
    {
        return row.state == DexState::Unseen || row.name.empty() ? "?????" : row.name;
    }

    static std::string Number(u16 number)
    {
        char text[8];
        std::snprintf(text, sizeof(text), "%03u", number);
        return text;
    }

    static void Frame(UI::Canvas &canvas, const UI::Rect &area)
    {
        canvas.FillRoundRect(area, PokedexRadius, PanelBg);
        canvas.StrokeRoundRect(area, PokedexRadius, Border, 2);
    }

    static void DrawTop(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        UI::Stack col(UI::Inset(area, 10, 8, 10, 6), UI::Stack::Column, 3);
        UI::TextStyle title;
        title.font = UI::Font::Large;
        UI::DrawText(canvas, "Pok\xC3\xA9" "dex", col.Next(24), title);

        UI::TextStyle hint;
        hint.color = TextDim;
        const UI::Rect footer = col.NextFromEnd(18);

        UI::TextStyle text;
        UI::TextStyle count;
        count.align = UI::Align::Right;
        for (int row = 0; row < DexTopRows; row++)
        {
            const bool eval = row == DexTopEvaluation;
            if (eval && !view.evaluation)
                continue;
            UI::Rect r = col.Next(eval ? 24 : 22);
            if (eval)
                r = UI::Inset(r, 0, 2, 0, 0);
            const bool open = eval || view.open[row];
            const bool sel = row == view.topCursor;
            const UI::Color fill = !open ? LockedBg : eval ? EvalColor : DexColors[row];
            canvas.FillRoundRect(r, 8, fill);
            if (sel)
            {
                canvas.StrokeRoundRect(UI::Inset(r, -2), 10, Selected, 2);
                UI::DrawIcon(canvas, UI::Icon::ArrowRight, UI::Rect{ r.x + 3, r.y + r.h / 2 - 5, 10, 10 }, Selected);
            }
            const UI::Rect inner = UI::Inset(r, 16, 0, 8, 0);
            text.color = eval ? Rgb(40, 30, 10) : TextMain;
            if (eval)
                UI::DrawText(canvas, "Pok\xC3\xA9" "dex Evaluation", inner, text);
            else if (!open)
            {
                text.color = TextDim;
                UI::DrawText(canvas, "Locked", inner, text);
            }
            else
            {
                UI::DrawText(canvas, std::string(DexName((DexKind)row)) + " Pok\xC3\xA9" "dex", inner, text);
                // Caught and seen, each after its mark, as the game's rows.
                char c[8];
                std::snprintf(c, sizeof(c), "%u", view.counts[row].seen);
                UI::DrawText(canvas, c, inner, count);
                DrawMark(canvas, DexState::Seen, inner.Right() - 40, inner.y + inner.h / 2);
                const UI::Rect left = UI::Inset(inner, 0, 0, 52, 0);
                std::snprintf(c, sizeof(c), "%u", view.counts[row].caught);
                UI::DrawText(canvas, c, left, count);
                DrawMark(canvas, DexState::Caught, left.Right() - 40, left.y + left.h / 2);
            }
        }
        UI::DrawText(canvas, "Up/Down Choose   A Open   B Back", footer, hint);
    }

    // Header line: dex name and its counts.
    static void DrawHeader(UI::Canvas &canvas, const DexView &view, const UI::Rect &r, bool counts)
    {
        UI::TextStyle t;
        UI::DrawText(canvas, std::string(DexName(view.dex)) + (counts ? "" : " Pok\xC3\xA9" "dex"), r, t);
        if (counts)
        {
            UI::TextStyle c;
            c.align = UI::Align::Right;
            c.color = TextDim;
            char text[24];
            std::snprintf(text, sizeof(text), "%u/%u", view.counts[(int)view.dex].caught,
                          view.counts[(int)view.dex].seen);
            UI::DrawText(canvas, text, r, c);
        }
    }

    static void DrawRows(UI::Canvas &canvas, const DexWindow &w, UI::Stack &col, int rowH)
    {
        UI::TextStyle num, name;
        num.color = TextDim;
        for (int i = 0; i < (int)w.rows.size(); i++)
        {
            const DexRow &row = w.rows[i];
            const UI::Rect r = col.Next(rowH);
            const bool sel = w.first + i == w.cursor;
            if (sel)
            {
                canvas.FillRoundRect(r, 6, RowSel);
                canvas.StrokeRoundRect(r, 6, Selected, 1);
            }
            num.color = sel ? TextMain : TextDim;
            UI::DrawText(canvas, Number(row.number), UI::Rect{ r.x + 4, r.y, 34, r.h }, num);
            name.color = row.state == DexState::Unseen ? TextDim : TextMain;
            UI::DrawText(canvas, RowName(row), UI::Rect{ r.x + 40, r.y, r.w - 60, r.h }, name);
            DrawMark(canvas, row.state, r.Right() - 10, r.y + r.h / 2);
        }
    }

    static void DrawScrollBar(UI::Canvas &canvas, const DexWindow &w, const UI::Rect &track)
    {
        if (w.count <= (int)w.rows.size() || w.count <= 0)
            return;
        canvas.FillRoundRect(track, 2, Rgb(80, 70, 80));
        const int h = std::max(8, (int)track.h * (int)w.rows.size() / w.count);
        const int y = (int)track.y + ((int)track.h - h) * w.first / std::max(1, w.count - (int)w.rows.size());
        canvas.FillRoundRect(UI::Rect{ track.x, y, track.w, h }, 2, Selected);
    }

    static void DrawList(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        UI::Stack col(UI::Inset(area, 8, 6, 12, 4), UI::Stack::Column, 2);
        DrawHeader(canvas, view, col.Next(20), true);
        UI::TextStyle hint;
        hint.color = TextDim;
        const UI::Rect footer = col.NextFromEnd(18);
        DrawRows(canvas, view.window, col, 19);
        DrawScrollBar(canvas, view.window, UI::Rect{ area.Right() - 8, area.y + 30, 4, area.h - 56 });
        UI::DrawText(canvas, "A Open  B Back", footer, hint);
    }

    static void DrawGrid(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        UI::Stack col(UI::Inset(area, 6, 4, 6, 4), UI::Stack::Column, 1);
        DrawHeader(canvas, view, col.Next(19), true);
        DrawRows(canvas, view.window, col, 19);
    }

    static void DrawEntry(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        UI::Stack col(UI::Inset(area, 8, 5, 8, 5), UI::Stack::Column, 1);
        UI::TextStyle dim;
        dim.color = TextDim;
        const DexWindow &w = view.window;
        const int at = w.cursor - w.first;
        const UI::Rect head = col.Next(18);
        UI::DrawText(canvas, DexName(view.dex), head, dim);
        if (at >= 0 && at < (int)w.rows.size())
        {
            const DexRow &row = w.rows[at];
            UI::TextStyle num = dim;
            num.align = UI::Align::Right;
            UI::DrawText(canvas, "No. " + Number(row.number), head, num);
            UI::TextStyle big;
            big.font = UI::Font::Large;
            UI::DrawText(canvas, RowName(row), col.Next(24), big);
            const UI::Rect st = col.Next(18);
            DrawMark(canvas, row.state, st.x + 7, st.y + st.h / 2);
            static const char *const states[] = { "Not seen", "Seen", "Caught" };
            UI::DrawText(canvas, states[(int)row.state], UI::Rect{ st.x + 18, st.y, st.w - 18, st.h },
                         row.state == DexState::Unseen ? dim : UI::TextStyle());
        }
        UI::TextStyle hint;
        hint.color = TextDim;
        col.Next(2);
        const UI::Rect keys = col.Next(18);
        UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ keys.x, keys.y + 4, 10, 10 }, TextDim);
        UI::DrawIcon(canvas, UI::Icon::ArrowDown, UI::Rect{ keys.x + 11, keys.y + 4, 10, 10 }, TextDim);
        UI::DrawText(canvas, "Next seen", UI::Inset(keys, 26, 0, 0, 0), hint);
        UI::DrawText(canvas, "A Habitat   B Back", col.Next(18), hint);
    }

    void DrawPokedex(UI::Canvas &canvas, const DexView &view)
    {
        const UI::Rect area = PokedexArea(view.screen);
        if (area.Empty())
            return;
        Frame(canvas, area);
        switch (view.screen)
        {
        case DexScreen::Top:   DrawTop(canvas, view, area); break;
        case DexScreen::List:  DrawList(canvas, view, area); break;
        case DexScreen::Grid:  DrawGrid(canvas, view, area); break;
        case DexScreen::Entry: DrawEntry(canvas, view, area); break;
        default: break;
        }
    }
}
