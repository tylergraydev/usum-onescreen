#include "Screens/PokedexList.hpp"

#include <algorithm>
#include <cstdio>

#include "UI/Icons.hpp"
#include "UI/Layout.hpp"
#include "UI/Text.hpp"
#include "UI/Theme.hpp"

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

    // The game's own Pokédex look (docs/VISUAL-STYLE.md, measured on its
    // bottom screens): a light blue background with lighter squares; rows are
    // bars leaning right with a 2 px dark line, a darker band at the bottom
    // and a shadow under them, a dark notch at the left end; the selected
    // top-level row is red and a little bigger, the others orange, empty ones
    // brown; counts in dark pills after a ball (caught) and a grey dot
    // (seen). The red arrow cursor marks the selection. Text and button
    // icons come from UI::Theme, in the game's font once it is loaded.
    namespace Theme = UI::Theme;

    static constexpr UI::Color Sky       = Rgb(0x8C, 0xDE, 0xF2);   // #8DE1F2 at the top, #87D7F2 lower down
    static constexpr UI::Color SkySquare = Rgb(0x94, 0xE8, 0xF2);   // the lighter squares in it
    static constexpr UI::Color SkyEdge   = Rgb(0x4E, 0xA6, 0xBE);   // the panel's edge (ours; the game's screen has none)
    static constexpr UI::Color BarShadow = Rgb(0x59, 0x8C, 0x93);   // the bars' shadow on the background
    static constexpr UI::Color EmptyLine = Rgb(0x14, 0x05, 0x06);   // empty rows' line (DexLine on the others)
    static constexpr UI::Color CountSel  = Rgb(0x5B, 0x00, 0x0B);   // count pills on the selected row
    static constexpr UI::Color CountRow  = Rgb(0x72, 0x1B, 0x0D);   // ... on the others
    static constexpr UI::Color SeenDot   = Rgb(0x4A, 0x4A, 0x4A);
    static constexpr UI::Color SeenRim   = Rgb(0x2D, 0x29, 0x29);
    static constexpr UI::Color BallRed   = Rgb(0xDD, 0x00, 0x00);
    static constexpr UI::Color BallLine  = Rgb(0x1C, 0x00, 0x00);
    static constexpr UI::Color EvalFill  = Rgb(0xFF, 0xDD, 0x00);   // Pokédex Evaluation
    static constexpr UI::Color EvalLine  = Rgb(0x44, 0x22, 0x11);
    static constexpr UI::Color EvalNotch = Rgb(0x8A, 0x53, 0x0A);
    // The Alola list's rows (its bottom screen): seen and caught ones light
    // red, unseen ones dark red with a pink "?????".
    static constexpr UI::Color ListSeen   = Rgb(0xFF, 0x55, 0x44);
    static constexpr UI::Color ListUnseen = Rgb(0x96, 0x32, 0x28);
    static constexpr UI::Color ListLine   = Rgb(0x33, 0x11, 0x11);
    static constexpr UI::Color ListQuery  = Rgb(0xFF, 0x64, 0x64);
    static constexpr UI::Color White = Rgb(0xFF, 0xFF, 0xFF);
    static constexpr UI::Color Ink   = Rgb(0x25, 0x25, 0x25);       // dark text on the background

    static UI::Color Mix(UI::Color a, UI::Color b, int t)
    {
        return Rgb((u8)(a.r + (b.r - a.r) * t / 255), (u8)(a.g + (b.g - a.g) * t / 255),
                   (u8)(a.b + (b.b - a.b) * t / 255), a.a);
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

    static Theme::TextStyle Style(UI::Color color, Theme::TextSize size = Theme::TextSize::Body,
                                  UI::Align align = UI::Align::Left)
    {
        Theme::TextStyle s;
        s.color = color;
        s.size = size;
        s.align = align;
        return s;
    }

    // The background: light blue with a grid of lighter squares.
    static void DrawBackground(UI::Canvas &canvas, const UI::Rect &area)
    {
        canvas.FillRoundRect(area, PokedexRadius, SkyEdge);
        const UI::Rect inner = UI::Inset(area, 2);
        canvas.FillRoundRect(inner, PokedexRadius - 2, Sky);
        const UI::Rect clip = canvas.GetClip();
        canvas.SetClip(UI::Intersect(clip, UI::Inset(inner, 2)));
        static const int Cell = 16;
        for (int y = inner.y; y < inner.Bottom(); y += Cell)
            for (int x = inner.x + ((y - inner.y) / Cell % 2) * Cell; x < inner.Right(); x += 2 * Cell)
                canvas.FillRect(UI::Rect{ x + 2, y + 2, Cell - 4, Cell - 4 }, SkySquare);
        canvas.SetClip(clip);
    }

    // A shape leaning right: its top edge `slant` px right of its bottom
    // edge, inside r. A rect in the middle and a triangle at each end.
    static void FillSlanted(UI::Canvas &canvas, const UI::Rect &r, int slant, UI::Color c)
    {
        if (r.Empty())
            return;
        const float x0 = (float)r.x, x1 = (float)r.Right(), y0 = (float)r.y, y1 = (float)r.Bottom();
        const float s = (float)slant;
        canvas.FillRect(UI::Rect{ r.x + slant, r.y, r.w - 2 * slant, r.h }, c);
        canvas.FillTriangle(x0 + s, y0, x0 + s, y1, x0, y1, c);
        canvas.FillTriangle(x1 - s, y0, x1, y0, x1 - s, y1, c);
    }

    static int Slant(const UI::Rect &r)
    {
        return r.h / 5;
    }

    // A Pokédex bar in r: shadow, 2 px line, fill, darker band at the bottom,
    // and the dark notch at the left end unless `notch` is clear.
    static void DrawBar(UI::Canvas &canvas, const UI::Rect &r, UI::Color fill, UI::Color line, UI::Color notch)
    {
        const int s = Slant(r);
        FillSlanted(canvas, UI::Rect{ r.x + 2, r.y + 2, r.w, r.h }, s, BarShadow);
        FillSlanted(canvas, r, s, line);
        const UI::Rect in = UI::Inset(r, 2);
        FillSlanted(canvas, in, s, fill);
        canvas.FillRect(UI::Rect{ in.x + s, in.Bottom() - 2, in.w - 2 * s, 2 }, Mix(fill, Rgb(0, 0, 0), 50));
        if (notch.a != 0)
        {
            const float lx = (float)(r.x + s / 2 + 3), cy = (float)r.y + r.h / 2.f;
            canvas.FillTriangle(lx, cy - 4.f, lx + 5.f, cy, lx, cy + 4.f, notch);
        }
    }

    // Caught: the game's flat red and white ball; seen: a grey dot. Centred
    // on (cx, cy), `size` px across.
    static void DrawMark(UI::Canvas &canvas, DexState state, int cx, int cy, int size)
    {
        const UI::Rect r{ cx - size / 2, cy - size / 2, size, size };
        if (state == DexState::Seen)
        {
            canvas.FillRoundRect(r, size / 2, SeenRim);
            canvas.FillRoundRect(UI::Inset(r, 1), size / 2 - 1, SeenDot);
        }
        else if (state == DexState::Caught)
        {
            canvas.FillRoundRect(r, size / 2, BallLine);
            const UI::Rect in = UI::Inset(r, 1);
            canvas.FillRoundRect(in, in.w / 2, White);
            const UI::Rect clip = canvas.GetClip();
            canvas.SetClip(UI::Intersect(clip, UI::Rect{ in.x, in.y, in.w, in.h / 2 }));
            canvas.FillRoundRect(in, in.w / 2, BallRed);
            canvas.SetClip(clip);
            canvas.HLine(in.x, in.y + in.h / 2, in.w, BallLine);
            const int b = std::max(4, size / 3);
            canvas.FillRoundRect(UI::Rect{ cx - b / 2 - 1, cy - b / 2 - 1, b + 2, b + 2 }, b / 2 + 1, BallLine);
            canvas.FillRoundRect(UI::Rect{ cx - b / 2, cy - b / 2, b, b }, b / 2, White);
        }
    }

    static std::string CountText(u16 count)
    {
        char text[8];
        std::snprintf(text, sizeof(text), "%u", count);
        return text;
    }

    // Width of a count pill `h` tall: the mark, the number, the round end.
    static int CountWidth(u16 count, int h, Theme::TextSize size)
    {
        return h + Theme::MeasureText(CountText(count), size) + 6;
    }

    int DexTopCountWidth(void)
    {
        return CountWidth(999, 18, Theme::TextSize::Body);
    }

    int DexTopCountTextWidth(void)
    {
        return DexTopCountWidth() - 18 - 6;
    }

    // A dark count pill: the mark at its left, the number right-aligned.
    static void DrawCount(UI::Canvas &canvas, const UI::Rect &r, DexState mark, u16 count, UI::Color fill,
                          Theme::TextSize size)
    {
        canvas.FillRoundRect(r, r.h / 2, fill);
        const int m = r.h - 4;
        DrawMark(canvas, mark, r.x + 2 + m / 2, r.y + r.h / 2, m);
        Theme::DrawText(canvas, CountText(count), UI::Inset(r, m + 4, 0, 6, 0), Style(White, size, UI::Align::Right));
    }

    // A dark chip with up/down arrows and a label, as the theme's button
    // prompts (the theme has no d-pad icon). Returns its width.
    static int DrawArrowsPrompt(UI::Canvas &canvas, int x, int y, const std::string &label)
    {
        const int w = 4 + 22 + 4 + Theme::MeasureText(label, Theme::TextSize::Body) + 6;
        canvas.FillRoundRect(UI::Rect{ x, y, w, Theme::PromptHeight }, Theme::PromptHeight / 2, Theme::PromptFill);
        const int ay = y + (Theme::PromptHeight - 10) / 2;
        UI::DrawIcon(canvas, UI::Icon::ArrowUp, UI::Rect{ x + 4, ay, 10, 10 }, Theme::PromptText);
        UI::DrawIcon(canvas, UI::Icon::ArrowDown, UI::Rect{ x + 15, ay, 10, 10 }, Theme::PromptText);
        Theme::DrawText(canvas, label, UI::Rect{ x + 30, y, w - 34, Theme::PromptHeight }, Style(Theme::PromptText));
        return w;
    }

    // --- Top level ---

    // Row geometry inside the top-level area: five dex rows, the Evaluation
    // row a little apart and indented, then the prompts.
    static const int TopRowsY = 12, TopPitch = 30, TopRowH = 26, TopRowX = 22, TopRowRight = 12;
    static const int EvalGap = 6, EvalIndent = 56;

    static void DrawTop(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        const Theme::TextSize body = Theme::TextSize::Body;
        for (int row = 0; row < DexTopRows; row++)
        {
            const bool eval = row == DexTopEvaluation;
            if (eval && !view.evaluation)
                continue;
            const bool sel = row == view.topCursor;
            const int y = area.y + TopRowsY + row * TopPitch + (eval ? EvalGap : 0);
            UI::Rect r{ area.x + TopRowX + (eval ? EvalIndent : 0), y, area.w - TopRowX - TopRowRight - (eval ? EvalIndent : 0),
                        TopRowH };
            // The game's selected dex row is bigger and reaches further left.
            if (sel && !eval)
                r = UI::Rect{ r.x - 8, r.y - 3, r.w + 8, r.h + 6 };
            const bool open = eval || view.open[row];
            if (eval)
                DrawBar(canvas, r, EvalFill, EvalLine, sel ? Rgb(0, 0, 0, 0) : EvalNotch);
            else if (!open)
                DrawBar(canvas, r, Theme::DexEmpty, EmptyLine, sel ? Rgb(0, 0, 0, 0) : EmptyLine);
            else
                DrawBar(canvas, r, sel ? Theme::DexSelected : Theme::DexRow, Theme::DexLine,
                        sel ? Rgb(0, 0, 0, 0) : Theme::DexLine);

            const int s = Slant(r);
            const UI::Rect label = UI::Inset(r, s + 18, 2, s + 6, 4);
            if (eval)
                Theme::DrawText(canvas, "Pok\xC3\xA9" "dex Evaluation", label, Style(Ink, body));
            else if (open)
            {
                // Caught, then seen, each in its pill, at the right end.
                const UI::Color pill = sel ? CountSel : CountRow;
                const int ph = 18, pw = DexTopCountWidth(), py = r.y + (r.h - 2 - ph) / 2;
                const int seenX = r.Right() - s - 6 - pw;
                DrawCount(canvas, UI::Rect{ seenX, py, pw, ph }, DexState::Seen, view.counts[row].seen, pill, body);
                DrawCount(canvas, UI::Rect{ seenX - 4 - pw, py, pw, ph }, DexState::Caught, view.counts[row].caught,
                          pill, body);
                const std::string name = std::string(DexName((DexKind)row)) + " Pok\xC3\xA9" "dex";
                const UI::Rect text{ label.x, label.y, seenX - 8 - pw - label.x, label.h };
                const Theme::TextSize size = Theme::MeasureText(name, body) <= text.w ? body : Theme::TextSize::Small;
                if (Theme::MeasureText(name, size) <= text.w)
                    Theme::DrawText(canvas, name, text, Style(White, size));
                else
                {
                    UI::TextStyle compact;
                    compact.font = UI::Font::Small;
                    compact.color = White;
                    UI::DrawText(canvas, name, text, compact);
                }
            }
            if (sel)
                Theme::DrawCursor(canvas, r.x + s + 14, r.y + (r.h - 2) / 2);
        }
        // The prompts, bottom left (the game's back button is there).
        const int py = area.Bottom() - 6 - Theme::PromptHeight;
        int x = area.x + 10;
        x += Theme::DrawButtonPrompt(canvas, x, py, Theme::Button::B, "Back") + 4;
        x += Theme::DrawButtonPrompt(canvas, x, py, Theme::Button::A, "Open") + 4;
        DrawArrowsPrompt(canvas, x, py, "Choose");
    }

    // --- List and grid ---

    // Header: the dex's name, its caught and seen counts in pills.
    static void DrawHeader(UI::Canvas &canvas, const DexView &view, const UI::Rect &r)
    {
        const Theme::TextSize small = Theme::TextSize::Small;
        const DexCounts &c = view.counts[(int)view.dex];
        const int ph = 16, py = r.y + (r.h - ph) / 2;
        const int seenW = CountWidth(c.seen, ph, small), caughtW = CountWidth(c.caught, ph, small);
        const int seenX = r.Right() - seenW, caughtX = seenX - 3 - caughtW;
        DrawCount(canvas, UI::Rect{ seenX, py, seenW, ph }, DexState::Seen, c.seen, CountRow, small);
        DrawCount(canvas, UI::Rect{ caughtX, py, caughtW, ph }, DexState::Caught, c.caught, CountRow, small);
        Theme::DrawText(canvas, DexName(view.dex), UI::Rect{ r.x, r.y, caughtX - 4 - r.x, r.h }, Style(Ink));
    }

    // Rows `pitch` apart from y: number, name and mark on a light red bar
    // (dark red while unseen); the cursor marks the selected one, which is
    // also a little bigger, as on the game's top level.
    static void DrawRows(UI::Canvas &canvas, const DexWindow &w, int x, int y, int width, int pitch)
    {
        const int h = pitch - 3;
        for (int i = 0; i < (int)w.rows.size(); i++)
        {
            const DexRow &row = w.rows[i];
            const bool sel = w.first + i == w.cursor;
            const bool unseen = row.state == DexState::Unseen;
            UI::Rect r{ x + 8, y + i * pitch, width - 8, h };
            if (sel)
                r = UI::Rect{ r.x - 6, r.y - 1, r.w + 6, r.h + 2 };
            DrawBar(canvas, r, unseen ? ListUnseen : ListSeen, ListLine, sel ? Rgb(0, 0, 0, 0) : ListLine);
            const int s = Slant(r);
            const UI::Rect in = UI::Inset(r, s + 14, 1, s + 4, 3);
            const int nw = std::max(30, (int)Theme::MeasureText(Number(row.number), Theme::TextSize::Small));
            Theme::DrawText(canvas, Number(row.number), UI::Rect{ in.x, in.y, nw, in.h },
                            Style(unseen ? ListQuery : White, Theme::TextSize::Small));
            Theme::DrawText(canvas, RowName(row), UI::Rect{ in.x + nw + 2, in.y, in.w - nw - 2 - 16, in.h },
                            Style(unseen ? ListQuery : White));
            DrawMark(canvas, row.state, in.Right() - 7, r.y + (r.h - 2) / 2, 12);
            if (sel)
                Theme::DrawCursor(canvas, r.x + s + 10, r.y + (r.h - 2) / 2);
        }
    }

    static void DrawScrollBar(UI::Canvas &canvas, const DexWindow &w, const UI::Rect &track)
    {
        if (w.count <= (int)w.rows.size() || w.count <= 0)
            return;
        canvas.FillRoundRect(track, 2, BarShadow);
        const int h = std::max(8, (int)track.h * (int)w.rows.size() / w.count);
        const int y = (int)track.y + ((int)track.h - h) * w.first / std::max(1, w.count - (int)w.rows.size());
        canvas.FillRoundRect(UI::Rect{ track.x, y, track.w, h }, 2, Theme::DexSelected);
    }

    static void DrawList(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        DrawHeader(canvas, view, UI::Rect{ area.x + 10, area.y + 5, area.w - 20, 20 });
        DrawRows(canvas, view.window, area.x + 6, area.y + 29, area.w - 22, 21);
        DrawScrollBar(canvas, view.window, UI::Rect{ area.Right() - 10, area.y + 30, 4, area.h - 60 });
        const int py = area.Bottom() - 4 - Theme::PromptHeight;
        const int x = area.x + 8;
        Theme::DrawButtonPrompt(canvas, x + Theme::DrawButtonPrompt(canvas, x, py, Theme::Button::B, "Back") + 4, py,
                                Theme::Button::A, "Open");
    }

    static void DrawGrid(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        DrawHeader(canvas, view, UI::Rect{ area.x + 8, area.y + 4, area.w - 16, 18 });
        DrawRows(canvas, view.window, area.x + 4, area.y + 25, area.w - 12, 21);
    }

    // --- Entry page ---

    // As the game's entry page header: a white bar with the mark, number
    // and name; the state under it; then the prompts.
    static void DrawEntry(UI::Canvas &canvas, const DexView &view, const UI::Rect &area)
    {
        const UI::Rect in = UI::Inset(area, 8, 5, 8, 5);
        const DexWindow &w = view.window;
        const int at = w.cursor - w.first;
        Theme::DrawText(canvas, std::string(DexName(view.dex)) + " Pok\xC3\xA9" "dex", UI::Rect{ in.x, in.y, in.w, 16 },
                        Style(Ink, Theme::TextSize::Small));
        if (at >= 0 && at < (int)w.rows.size())
        {
            const DexRow &row = w.rows[at];
            const UI::Rect bar{ in.x, in.y + 18, in.w, 26 };
            canvas.FillRoundRect(UI::Rect{ bar.x + 2, bar.y + 2, bar.w, bar.h }, 6, BarShadow);
            canvas.FillRoundRect(bar, 6, Theme::DexLine);
            canvas.FillRoundRect(UI::Inset(bar, 2), 4, White);
            DrawMark(canvas, row.state == DexState::Unseen ? DexState::Seen : row.state, bar.x + 13,
                     bar.y + bar.h / 2, 14);
            const UI::Rect text = UI::Inset(bar, 24, 2, 6, 2);
            const int nw = Theme::DrawText(canvas, Number(row.number), text, Style(Ink));
            Theme::DrawText(canvas, RowName(row), UI::Inset(text, nw + 6, 0, 0, 0), Style(Ink));
            static const char *const states[] = { "Not seen", "Seen", "Caught" };
            Theme::DrawText(canvas, states[(int)row.state], UI::Rect{ in.x + 4, bar.Bottom() + 3, in.w - 4, 19 },
                            Style(row.state == DexState::Unseen ? Mix(Ink, Sky, 120) : Ink));
        }
        const int py = in.Bottom() - Theme::PromptHeight;
        Theme::DrawButtonPrompt(canvas, in.x, py, Theme::Button::A, "Habitat");
        const int ny = py - 3 - Theme::PromptHeight;
        const int aw = DrawArrowsPrompt(canvas, in.x, ny, "Next seen");
        Theme::DrawButtonPrompt(canvas, in.x + aw + 4, ny, Theme::Button::B, "");
    }

    void DrawPokedex(UI::Canvas &canvas, const DexView &view)
    {
        const UI::Rect area = PokedexArea(view.screen);
        if (area.Empty())
            return;
        DrawBackground(canvas, area);
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
