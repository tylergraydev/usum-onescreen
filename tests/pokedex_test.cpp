// Host tests for the native Pokédex's data, list and cursor logic
// (Sources/Screens/PokedexList.cpp). Build and run: make -C tests. With a
// path prefix argument, also writes a preview of each screen there as PPM
// images (<prefix>-top.ppm, -list.ppm, -grid.ppm, -entry.ppm).

#include "Screens/PokedexList.hpp"
#include "UI/Theme.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

// A made-up form table in the game's layout (not the game's data).
static const DexFormEntry Forms[] = { { 3, 2 }, { 6, 3 }, { 19, 2 }, { 52, 2 } };
static const int FormCount = 4;

static void TestFormBits(void)
{
    // Form 0 is species - 1.
    CHECK(DexFormBit(1, 0, Forms, FormCount) == 0);
    CHECK(DexFormBit(807, 0, Forms, FormCount) == 806);
    // Extra forms follow bit 806 in table order: 3 has one extra, 6 two.
    CHECK(DexFormBit(3, 1, Forms, FormCount) == 807);
    CHECK(DexFormBit(6, 1, Forms, FormCount) == 808);
    CHECK(DexFormBit(6, 2, Forms, FormCount) == 809);
    CHECK(DexFormBit(19, 1, Forms, FormCount) == 810);
    CHECK(DexFormBit(52, 1, Forms, FormCount) == 811);
    // No such form, no such species.
    CHECK(DexFormBit(3, 2, Forms, FormCount) == -1);
    CHECK(DexFormBit(25, 1, Forms, FormCount) == -1);
    CHECK(DexFormBit(0, 0, Forms, FormCount) == -1);
    CHECK(DexFormBit(808, 0, Forms, FormCount) == -1);

    CHECK(DexFormCount(6, Forms, FormCount) == 3);
    CHECK(DexFormCount(25, Forms, FormCount) == 1);
}

static void SetBit(std::vector<u8> &z, u32 region, int bit)
{
    z[region + bit / 8] |= (u8)(1 << (bit % 8));
}

static void TestStates(void)
{
    std::vector<u8> z(DexZukanBytes, 0);
    SetBit(z, DexZukanCaught, 725 - 1);             // Litten caught
    SetBit(z, DexZukanSeen[0], 722 - 1);            // Rowlet seen (male)
    SetBit(z, DexZukanSeen[1], 10 - 1);             // Caterpie seen (female)
    SetBit(z, DexZukanSeen[3], 25 - 1);             // Pikachu seen (shiny female)
    SetBit(z, DexZukanSeen[0], DexFormBit(19, 1, Forms, FormCount));   // Rattata seen in its other form only

    CHECK(DexStateOf(z.data(), 725, Forms, FormCount) == DexState::Caught);
    CHECK(DexStateOf(z.data(), 722, Forms, FormCount) == DexState::Seen);
    CHECK(DexStateOf(z.data(), 10, Forms, FormCount) == DexState::Seen);
    CHECK(DexStateOf(z.data(), 25, Forms, FormCount) == DexState::Seen);
    CHECK(DexStateOf(z.data(), 19, Forms, FormCount) == DexState::Seen);
    CHECK(DexStateOf(z.data(), 723, Forms, FormCount) == DexState::Unseen);
    CHECK(DexStateOf(z.data(), 20, Forms, FormCount) == DexState::Unseen);
    CHECK(DexStateOf(z.data(), 0, Forms, FormCount) == DexState::Unseen);
    CHECK(DexStateOf(z.data(), 900, Forms, FormCount) == DexState::Unseen);
    // Without the form table, Rattata's form bit isn't found.
    CHECK(DexStateOf(z.data(), 19, nullptr, 0) == DexState::Unseen);
}

static DexRecord Rec(u16 species, u16 alola, u16 melemele, u16 akala = 0)
{
    DexRecord r;
    r.species = species;
    r.number[0] = alola;
    r.number[1] = melemele;
    r.number[2] = akala;
    return r;
}

static std::vector<DexRecord> Records(void)
{
    // In Alola order, as the game keeps them; island numbers differ.
    return { Rec(722, 1, 1), Rec(723, 2, 2), Rec(724, 3, 3), Rec(725, 4, 4), Rec(731, 5, 10, 1),
             Rec(19, 6, 15, 6), Rec(10, 7, 0, 2), Rec(744, 8, 0, 0) };
}

static void TestRows(void)
{
    const std::vector<DexRecord> recs = Records();
    const std::vector<DexRow> alola = DexRows(recs, DexKind::Alola, 0);
    CHECK(alola.size() == 8);
    CHECK(alola[0].number == 1 && alola[0].species == 722);
    CHECK(alola[7].number == 8 && alola[7].species == 744);

    // The Alola list stops where the game says (the highest number seen).
    const std::vector<DexRow> cut = DexRows(recs, DexKind::Alola, 5);
    CHECK(cut.size() == 5 && cut.back().species == 731);
    CHECK(DexRows(recs, DexKind::Alola, 99).size() == 8);

    // Islands: only members, in island order.
    const std::vector<DexRow> mele = DexRows(recs, DexKind::Melemele, 0);
    CHECK(mele.size() == 6);
    CHECK(mele[4].number == 10 && mele[4].species == 731);
    CHECK(mele[5].number == 15 && mele[5].species == 19);
    const std::vector<DexRow> akala = DexRows(recs, DexKind::Akala, 0);
    CHECK(akala.size() == 3);
    CHECK(akala[0].species == 731 && akala[1].species == 10 && akala[2].species == 19);
    CHECK(DexRows(recs, DexKind::Poni, 0).empty());
    CHECK(DexRows(recs, DexKind::Count, 0).empty());

    std::vector<DexRow> rows = alola;
    rows[0].state = DexState::Seen;
    rows[3].state = DexState::Caught;
    rows[4].state = DexState::Caught;
    const DexCounts c = DexCount(rows);
    CHECK(c.total == 8 && c.seen == 3 && c.caught == 2);

    rows[0].name = "Rowlet";
    CHECK(DescribeDexRow(rows[0]) == "#001 Rowlet seen");
    CHECK(DescribeDexRow(rows[1]) == "#002 ????? unseen");
    // A seen row whose name couldn't be read still doesn't print garbage.
    CHECK(DescribeDexRow(rows[3]) == "#004 ????? caught");
}

static void TestWindow(void)
{
    // Everything fits: no scrolling.
    CHECK(DexWindowTop(0, 5, 8) == 0);
    CHECK(DexWindowTop(4, 5, 8) == 0);
    // The cursor stays in the middle (row 3 of 8) where it can.
    CHECK(DexWindowTop(0, 126, 8) == 0);
    CHECK(DexWindowTop(3, 126, 8) == 0);
    CHECK(DexWindowTop(4, 126, 8) == 1);
    CHECK(DexWindowTop(60, 126, 8) == 57);
    CHECK(DexWindowTop(125, 126, 8) == 118);
    CHECK(DexWindowTop(122, 126, 8) == 118);
    // The grid's three rows: the cursor in the middle one.
    CHECK(DexWindowTop(0, 150, 3) == 0);
    CHECK(DexWindowTop(16, 150, 3) == 15);
    CHECK(DexWindowTop(149, 150, 3) == 147);
    CHECK(DexWindowTop(5, 150, 0) == 0);

    std::vector<DexRow> rows = DexRows(Records(), DexKind::Alola, 0);
    DexWindow w = MakeDexWindow(rows, 5, 3);
    CHECK(w.count == 8 && w.cursor == 5 && w.first == 4 && w.rows.size() == 3);
    CHECK(w.rows[1].number == 6);
    w = MakeDexWindow(rows, 7, 3);
    CHECK(w.first == 5 && w.rows.size() == 3 && w.rows[2].number == 8);
    // A cursor past the end is clamped (the game's list grew or shrank).
    w = MakeDexWindow(rows, 40, 8);
    CHECK(w.cursor == 7 && w.first == 0 && w.rows.size() == 8);
    w = MakeDexWindow(std::vector<DexRow>(), 0, 8);
    CHECK(w.count == 0 && w.rows.empty());
}

static void TestScreens(void)
{
    CHECK(DexScreenFromVtable(0x34CD4) == DexScreen::Top);
    CHECK(DexScreenFromVtable(0x34DD8) == DexScreen::List);
    CHECK(DexScreenFromVtable(0x34F68) == DexScreen::Grid);
    CHECK(DexScreenFromVtable(0x34C24) == DexScreen::Entry);
    CHECK(DexScreenFromVtable(0x34C14) == DexScreen::Other);   // the app's data, not a frame
    CHECK(std::string(DexScreenName(DexScreen::Grid)) == "grid");
    CHECK(std::string(DexName(DexKind::Ulaula)) == "Ula'ula");
    CHECK(std::string(DexId(DexKind::Ulaula)) == "ulaula");

    // Every screen that draws has an area on the top screen, none overlaps
    // the game's name plate (bottom left, below y 186) except the top level
    // (whose game screen is a title card).
    for (DexScreen s : { DexScreen::Top, DexScreen::List, DexScreen::Grid, DexScreen::Entry })
    {
        const UI::Rect r = PokedexArea(s);
        CHECK(!r.Empty());
        CHECK(r.x >= 0 && r.y >= 0 && r.Right() <= UI::ScreenW && r.Bottom() <= UI::ScreenH);
        CHECK(r.w <= PokedexMaxW && r.h <= PokedexMaxH);
        if (s != DexScreen::Top)
            CHECK(r.x >= 150 || r.Bottom() <= 186);
    }
    CHECK(PokedexArea(DexScreen::Other).Empty());
    CHECK(PokedexArea(DexScreen::None).Empty());
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void WritePpm(const UI::Canvas &canvas, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
    {
        std::printf("can't write %s\n", path.c_str());
        s_failures++;
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", UI::ScreenW, UI::ScreenH);
    for (int y = 0; y < UI::ScreenH; y++)
        for (int x = 0; x < UI::ScreenW; x++)
        {
            const UI::Color c = At(canvas, x, y);
            const u8 rgb[3] = { c.r, c.g, c.b };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
}

static void TestTopCounts(const char *prefix)
{
    DexView view;
    view.screen = DexScreen::Top;
    view.evaluation = true;
    for (int row = 0; row < DexKinds; row++)
    {
        view.open[row] = true;
        view.counts[row].caught = view.counts[row].seen = view.counts[row].total = 403;
    }
    CHECK(UI::Theme::MeasureText("403", UI::Theme::TextSize::Body) <= DexTopCountTextWidth());
    CHECK(UI::Theme::MeasureText("999", UI::Theme::TextSize::Body) <= DexTopCountTextWidth());
    CHECK(DexTopCountWidth() == DexTopCountTextWidth() + 18 + 6);
    // The compact fallback keeps even the longest row labels whole.
    UI::TextStyle compact;
    compact.font = UI::Font::Small;
    const int labelWidth = PokedexArea(DexScreen::Top).w - 22 - 12 - 2 * (26 / 5) - 18 - 6
                           - 2 * DexTopCountWidth() - 8;
    CHECK(UI::MeasureText("Melemele Pok\xC3\xA9" "dex", compact) <= labelWidth);
    CHECK(UI::MeasureText("Ula'ula Pok\xC3\xA9" "dex", compact) <= labelWidth);

    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * 4, 0);
    const UI::Surface surface = { pixels.data(), 4, UI::ScreenW * 4, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);
    for (int row = 0; row < DexKinds; row++)
    {
        view.topCursor = row;
        DrawPokedex(canvas, view);
    }
    if (prefix != nullptr)
        WritePpm(canvas, std::string(prefix) + "-top-403.ppm");
}

static void TestDraw(const char *prefix)
{
    std::vector<DexRow> rows = DexRows(Records(), DexKind::Alola, 0);
    const char *names[] = { "Rowlet", "Dartrix", "Decidueye", "Litten", "Pikipek", "Rattata", "Caterpie", "Rockruff" };
    for (size_t i = 0; i < rows.size(); i++)
    {
        rows[i].state = i % 3 == 1 ? DexState::Unseen : i % 3 == 0 ? DexState::Seen : DexState::Caught;
        if (rows[i].state != DexState::Unseen)
            rows[i].name = names[i];
    }

    DexView view;
    view.open[0] = view.open[1] = true;
    view.evaluation = true;
    view.counts[0] = view.counts[1] = DexCount(rows);
    view.topCursor = 1;

    for (DexScreen s : { DexScreen::Top, DexScreen::List, DexScreen::Grid, DexScreen::Entry })
    {
        const int bpp = 4;
        std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
        const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                      UI::ScreenW, UI::ScreenH };
        UI::Canvas canvas(surface);
        canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));

        view.screen = s;
        view.dex = s == DexScreen::Grid ? DexKind::Melemele : DexKind::Alola;
        view.window = MakeDexWindow(rows, 3, s == DexScreen::List ? DexListRows : s == DexScreen::Grid ? DexGridRows : 1);
        DrawPokedex(canvas, view);

        // Only the screen's area is touched (the Layer copies only it).
        const UI::Rect area = PokedexArea(s);
        CHECK(At(canvas, area.x - 1, area.y + area.h / 2).g == 200);
        CHECK(At(canvas, area.Right(), area.y + area.h / 2).g == 200 || area.Right() >= UI::ScreenW);
        CHECK(At(canvas, area.x + area.w / 2, area.Bottom()).g == 200 || area.Bottom() >= UI::ScreenH);
        CHECK(At(canvas, area.x + area.w / 2, area.y + area.h / 2).g != 200);

        if (prefix != nullptr)
            WritePpm(canvas, std::string(prefix) + "-" + DexScreenName(s) + ".ppm");
    }
    if (prefix != nullptr)
        std::printf("pokedex_test: previews written to %s-*.ppm\n", prefix);

    // Nothing to show: nothing drawn.
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * 4, 0);
    const UI::Surface surface = { pixels.data(), 4, UI::ScreenW * 4, UI::PixelFormat::RGBA8, UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);
    view.screen = DexScreen::Other;
    DrawPokedex(canvas, view);
    bool untouched = true;
    for (u8 p : pixels)
        untouched = untouched && p == 0;
    CHECK(untouched);
}

int main(int argc, char **argv)
{
    TestFormBits();
    TestStates();
    TestRows();
    TestWindow();
    TestScreens();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    TestTopCounts(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("pokedex_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("pokedex_test: all passed\n");
    return 0;
}
