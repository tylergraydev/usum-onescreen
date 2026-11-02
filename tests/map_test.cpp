// Host tests for the native map (Sources/Screens/MapView.cpp) and for reading
// place names from the game's RomFS (Sources/RomfsText.cpp). Build and run:
// make -C tests. Arguments: an optional preview PPM path, then an optional
// path to the game's own a/0/3/2 (English text GARC, from the user's dump in
// analysis/private/; skipped when missing, as on CI).

#include "RomfsText.hpp"
#include "Screens/MapView.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

// --- Builders for synthetic game files ---

static void Put16(std::vector<u8> &b, size_t at, u32 v) { b[at] = (u8)v; b[at + 1] = (u8)(v >> 8); }
static void Put32(std::vector<u8> &b, size_t at, u32 v) { Put16(b, at, v & 0xFFFF); Put16(b, at + 2, v >> 16); }
static void Add16(std::vector<u8> &b, u32 v) { b.resize(b.size() + 2); Put16(b, b.size() - 2, v); }
static void Add32(std::vector<u8> &b, u32 v) { b.resize(b.size() + 4); Put32(b, b.size() - 4, v); }

// A message file with one section, lines encrypted as the game stores them.
// A line may hold a tag: "\x10" stands for a 1-word tag 0xBDFF.
static std::vector<u8> MessageFile(const std::vector<std::string> &lines)
{
    std::vector<std::vector<u16>> plain;
    for (const std::string &l : lines)
    {
        std::vector<u16> units;
        for (char c : l)
        {
            if (c == '\x10')
            {
                units.push_back(0x10);
                units.push_back(1);
                units.push_back(0xBDFF);
            }
            else
                units.push_back((u8)c);
        }
        units.push_back(0);
        plain.push_back(units);
    }
    std::vector<u8> b;
    Add16(b, 1);
    Add16(b, (u32)lines.size());
    Add32(b, 0);            // total size, filled below
    Add32(b, 0);
    Add32(b, 0x10);         // section offset
    const size_t section = b.size();
    Add32(b, 0);            // section size
    size_t text = 4 + lines.size() * 8;
    for (const auto &u : plain)
    {
        Add32(b, (u32)text);
        Add16(b, (u32)u.size());
        Add16(b, 0);
        text += u.size() * 2;
    }
    for (size_t line = 0; line < plain.size(); line++)
    {
        u16 key = (u16)(0x2983 * (line + 3));
        for (u16 c : plain[line])
        {
            Add16(b, c ^ key);
            key = (u16)((key << 3) | (key >> 13));
        }
    }
    Put32(b, section, (u32)(b.size() - section));
    Put32(b, 4, (u32)b.size());
    return b;
}

// GARC v6 with one variant per entry.
static std::vector<u8> Garc(const std::vector<std::vector<u8>> &files)
{
    std::vector<u8> b(0x1C, 0);
    std::memcpy(&b[0], "CRAG", 4);
    Put32(b, 4, 0x1C);
    Put16(b, 8, 0xFEFF);
    Put16(b, 0x0A, 0x0600);
    Put32(b, 0x0C, 4);
    const size_t otaf = b.size();
    b.resize(b.size() + 12, 0);
    std::memcpy(&b[otaf], "OTAF", 4);
    Put32(b, otaf + 4, (u32)(12 + files.size() * 4));
    Put16(b, otaf + 8, (u32)files.size());
    for (size_t i = 0; i < files.size(); i++)
        Add32(b, (u32)(i * 12));
    const size_t btaf = b.size();
    b.resize(b.size() + 12, 0);
    std::memcpy(&b[btaf], "BTAF", 4);
    Put32(b, btaf + 4, (u32)(12 + files.size() * 16));
    Put32(b, btaf + 8, (u32)files.size());
    u32 at = 0;
    for (const auto &f : files)
    {
        Add32(b, 1);
        Add32(b, at);
        Add32(b, at + (u32)f.size());
        Add32(b, (u32)f.size());
        at += ((u32)f.size() + 3) & ~3u;
    }
    const size_t bmif = b.size();
    b.resize(b.size() + 12, 0);
    std::memcpy(&b[bmif], "BMIF", 4);
    Put32(b, bmif + 4, 12);
    Put32(b, bmif + 8, at);
    Put32(b, 0x10, (u32)b.size());
    for (const auto &f : files)
    {
        b.insert(b.end(), f.begin(), f.end());
        b.resize((b.size() + 3) & ~(size_t)3, 0);
    }
    return b;
}

static void AddName(std::vector<u8> &meta, const std::string &name)
{
    Add32(meta, (u32)name.size() * 2);
    for (char c : name)
        Add16(meta, (u8)c);
    meta.resize((meta.size() + 3) & ~(size_t)3, 0);
}

// RomFS level 3 holding a/0/3/1 (decoy) and a/0/3/2 (`file`), plus a/x.
// With `ivfc`, level 3 sits behind an IVFC header as in a full image.
static std::vector<u8> Romfs(const std::vector<u8> &file, bool ivfc)
{
    const u32 None = 0xFFFFFFFF;
    std::vector<u8> dirs, files;
    // Directory entries: root, a, 0, 3 (each the only child of the one before).
    const u32 root = 0, a = 0x18, d0 = 0x18 + 0x18 + 4, d3 = d0 + 0x18 + 4;
    auto dir = [&](u32 parent, u32 child, u32 firstFile, const std::string &name) {
        Add32(dirs, parent); Add32(dirs, None); Add32(dirs, child); Add32(dirs, firstFile); Add32(dirs, None);
        AddName(dirs, name);
    };
    // Files: a/x at 0, then a/0/3/1 and a/0/3/2 (siblings).
    std::vector<u8> data;
    auto addFile = [&](u32 parent, u32 sibling, const std::vector<u8> &bytes, const std::string &name) {
        Add32(files, parent); Add32(files, sibling);
        Add32(files, (u32)data.size()); Add32(files, 0);
        Add32(files, (u32)bytes.size()); Add32(files, 0);
        Add32(files, None);
        AddName(files, name);
        data.insert(data.end(), bytes.begin(), bytes.end());
        data.resize((data.size() + 15) & ~(size_t)15, 0);
    };
    dir(root, a, None, "");
    dir(root, d0, 0, "a");
    dir(a, d3, None, "0");
    dir(d0, None, 0x24, "3");
    CHECK(dirs.size() == d3 + 0x18 + 4);
    addFile(a, None, std::vector<u8>(5, 0xEE), "x");                       // entry at 0, 0x24 bytes
    addFile(d3, 0x48, std::vector<u8>(7, 0xDD), "1");                      // at 0x24
    addFile(d3, None, file, "2");                                          // at 0x48
    CHECK(files.size() == 0x6C);

    std::vector<u8> l3(0x28, 0);
    Put32(l3, 0, 0x28);
    const u32 dirAt = 0x28, fileAt = dirAt + (u32)dirs.size();
    const u32 dataAt = (fileAt + (u32)files.size() + 15) & ~15u;
    Put32(l3, 0x0C, dirAt);
    Put32(l3, 0x10, (u32)dirs.size());
    Put32(l3, 0x1C, fileAt);
    Put32(l3, 0x20, (u32)files.size());
    Put32(l3, 0x24, dataAt);
    l3.insert(l3.end(), dirs.begin(), dirs.end());
    l3.insert(l3.end(), files.begin(), files.end());
    l3.resize(dataAt, 0);
    l3.insert(l3.end(), data.begin(), data.end());
    if (!ivfc)
        return l3;
    // IVFC: master hash size 0x20, level 3 block size 2^12.
    std::vector<u8> img(0x1000, 0);
    std::memcpy(&img[0], "IVFC", 4);
    Put32(img, 0x08, 0x20);
    Put32(img, 0x4C, 12);
    img.insert(img.end(), l3.begin(), l3.end());
    return img;
}

static RomfsRead ReaderOf(const std::vector<u8> &image, int *reads = nullptr)
{
    return [&image, reads](uint64_t offset, void *dst, u32 size) {
        if (reads)
            (*reads)++;
        if (offset + size > image.size())
            return false;
        std::memcpy(dst, image.data() + offset, size);
        return true;
    };
}

static void TestRomfsText(void)
{
    const std::vector<std::string> lines = { "", "Mystery Zone", "Route 2", "", "\x10", "A\x10" "B" };
    const std::vector<u8> msg = MessageFile(lines);
    std::vector<std::string> got;
    std::string error;
    CHECK(MsgFileLines(msg, got, error));
    CHECK(got.size() == lines.size());
    if (got.size() == lines.size())
    {
        CHECK(got[1] == "Mystery Zone");
        CHECK(got[2] == "Route 2");
        CHECK(got[4].empty());          // a line that is only a tag
        CHECK(got[5] == "AB");
    }

    const std::vector<u8> garc = Garc({ MessageFile({ "x" }), std::vector<u8>(3, 1), msg });
    for (bool ivfc : { false, true })
    {
        const std::vector<u8> image = Romfs(garc, ivfc);
        uint64_t at = 0, size = 0;
        error.clear();
        CHECK(RomfsFind(ReaderOf(image), "a/0/3/2", at, size, error));
        CHECK(size == garc.size());
        CHECK(at + size <= image.size() && std::memcmp(&image[at], garc.data(), garc.size()) == 0);
        CHECK(!RomfsFind(ReaderOf(image), "a/0/3/9", at, size, error) && !error.empty());
        CHECK(!RomfsFind(ReaderOf(image), "a/1/3/2", at, size, error));
        CHECK(RomfsFind(ReaderOf(image), "a/x", at, size, error) && size == 5);

        int reads = 0;
        got.clear();
        CHECK(ReadMessageFile(ReaderOf(image, &reads), "a/0/3/2", 2, got, error));
        CHECK(got.size() == lines.size() && got[2] == "Route 2");
        CHECK(reads < 20);
        CHECK(!ReadMessageFile(ReaderOf(image), "a/0/3/2", 3, got, error));     // no entry 3
        CHECK(!ReadMessageFile(ReaderOf(image), "a/0/3/2", 1, got, error));     // not a message file
    }

    // Damaged input fails cleanly.
    CHECK(!MsgFileLines(std::vector<u8>(8, 0), got, error));
    std::vector<u8> cut(msg.begin(), msg.begin() + 0x20);
    CHECK(!MsgFileLines(cut, got, error));
    const std::vector<u8> junk(0x200, 0x5A);
    uint64_t at = 0, size = 0;
    CHECK(!RomfsFind(ReaderOf(junk), "a/0/3/2", at, size, error));
    u32 entrySize = 0;
    CHECK(!GarcEntry(ReaderOf(junk), 0, 0, at, entrySize, error));
}

// The user's own text GARC (not in git): place names come out as the
// in-game banners showed them (docs/RE4-map.md).
static void TestRealPlaceNames(const char *garcPath)
{
    std::ifstream in(garcPath, std::ios::binary);
    if (!in)
    {
        std::printf("map_test: %s not found, real place-name check skipped\n", garcPath);
        return;
    }
    const std::vector<u8> garc((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::vector<u8> image = Romfs(garc, false);
    std::vector<std::string> names;
    std::string error;
    CHECK(ReadMessageFile(ReaderOf(image), "a/0/3/2", 72, names, error));
    if (!error.empty())
        std::printf("    %s\n", error.c_str());
    CHECK(names.size() > 21);
    if (names.size() > 21)
    {
        CHECK(names[12] == "Route 2");
        CHECK(names[20] == "Hau\xE2\x80\x99oli City");     // U+2019
        CHECK(names[21] == "Shopping District");
    }
    std::printf("map_test: %zu place names read from %s\n", names.size(), garcPath);
}

static WhereState Where(u16 zone, u16 nameLine, bool indoor, float x, float z, int yaw, bool mapView)
{
    WhereState w;
    w.valid = true;
    w.zone = zone;
    w.zoneData = true;
    w.nameLine = nameLine;
    w.indoor = indoor;
    w.x = x;
    w.z = z;
    w.yaw = yaw;
    w.mapView = mapView ? 0x30000000 : 0;
    w.mapIndex = 0;
    return w;
}

static void TestView(void)
{
    CHECK(std::string(MapIslandName(0)) == "Melemele Island");
    CHECK(std::string(MapIslandName(3)) == "Poni Island");
    CHECK(std::string(MapIslandName(4)).empty());
    CHECK(std::string(MapCompass(0)) == "S");
    CHECK(std::string(MapCompass(90)) == "E");
    CHECK(std::string(MapCompass(180)) == "N");
    CHECK(std::string(MapCompass(-180)) == "N");
    CHECK(std::string(MapCompass(-90)) == "W");
    CHECK(std::string(MapCompass(-58)) == "SW");
    CHECK(std::string(MapCompass(22)) == "S");
    CHECK(std::string(MapCompass(23)) == "SE");

    const std::vector<std::string> names = { "", "", "", "", "", "", "Route 1", "", "", "", "", "", "Route 2", "",
                                             "", "", "", "", "", "", "Hau'oli City", "Shopping District" };
    MapView v = MakeMapView(Where(13, 20, false, 7524.6f, 16323.3f, 0, true), names, "ZR");
    CHECK(v.known && v.picture && !v.indoor && v.hasIsland);
    CHECK(std::string(v.place) == "Hau'oli City" && std::string(v.sub) == "Shopping District");
    CHECK(v.x == 7525 && v.z == 16323);
    CHECK(DescribeMapView(v) == "zone 13 \"Hau'oli City\" / \"Shopping District\", outdoors, Melemele Island, "
                                "x 7525 z 16323 facing S, picture");

    v = MakeMapView(Where(79, 12, true, 2175.9f, 2522.7f, -58, true), names, "ZR");
    CHECK(std::string(v.place) == "Route 2" && v.sub[0] == 0 && v.indoor);
    CHECK(DescribeMapView(v) == "zone 79 \"Route 2\", indoors, Melemele Island, x 2176 z 2523 facing SW, picture");

    // No names loaded, no Rotom map, a name line past the end.
    v = MakeMapView(Where(7, 12, false, 1, 2, 90, false), {}, "");
    CHECK(v.known && !v.picture && !v.hasIsland && v.place[0] == 0);
    CHECK(DescribeMapView(v) == "zone 7 \"\", outdoors, no island, x 1 z 2 facing E, no picture");
    v = MakeMapView(Where(7, 21, false, 1, 2, 90, true), names, "");
    CHECK(std::string(v.place) == "Shopping District" && v.sub[0] == 0);
    v = MakeMapView(Where(7, 400, false, 1, 2, 90, true), names, "");
    CHECK(v.place[0] == 0);

    WhereState none;
    v = MakeMapView(none, names, "ZR");
    CHECK(!v.known && std::string(v.key) == "ZR");
    CHECK(DescribeMapView(v) == "not in the field");

    // Long names are cut, not overrun.
    const std::vector<std::string> longName = { std::string(200, 'x') };
    v = MakeMapView(Where(0, 0, false, 0, 0, 0, true), longName, "");
    CHECK(std::strlen(v.place) == sizeof(v.place) - 1);

    MapView a = MakeMapView(Where(13, 20, false, 7524.6f, 16323.3f, 0, true), names, "ZR");
    MapView b = a;
    CHECK(a == b);
    b.yaw = 1;
    CHECK(a != b);
}

static void TestKeys(void)
{
    const u32 ZR = 1u << 15, B = 1u << 1, L = 1u << 9, R = 1u << 8, X = 1u << 10;
    CHECK(MapKeys(false, true, ZR, ZR) == MapKeyResult::Open);
    CHECK(MapKeys(false, false, ZR, ZR) == MapKeyResult::None);     // not in the field / a menu is up
    CHECK(MapKeys(true, true, ZR, ZR) == MapKeyResult::Close);
    CHECK(MapKeys(true, false, ZR, ZR) == MapKeyResult::Close);
    CHECK(MapKeys(true, true, B, ZR) == MapKeyResult::Close);
    CHECK(MapKeys(false, true, B, ZR) == MapKeyResult::None);       // B in the field stays the game's
    CHECK(MapKeys(false, true, X, ZR) == MapKeyResult::None);
    CHECK(MapKeys(false, true, L | R, ZR) == MapKeyResult::None);   // the panel's key
    CHECK(MapKeys(false, true, ZR, 0) == MapKeyResult::None);       // map_key = none
    CHECK(MapKeys(false, true, L, L | R) == MapKeyResult::None);    // a combo needs all its keys
    CHECK(MapKeys(false, true, L | R, L | R) == MapKeyResult::Open);
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void TestLayout(void)
{
    // The copy's constraints (PanelCopyBottomRect): whole 16-byte BGR8 runs
    // at 8-byte aligned addresses in 240-px lines, either line direction.
    CHECK(MapSource.w == MapWindow.w && MapSource.h == MapWindow.h);
    CHECK((MapSource.h * 3) % 16 == 0);
    CHECK(MapSource.y % 8 == 0 && (MapSource.y + MapSource.h) % 8 == 0);
    CHECK(MapWindow.y % 8 == 0 && (MapWindow.y + MapWindow.h) % 8 == 0);
    CHECK(MapSource.x >= 0 && MapSource.Right() <= 320 && MapSource.Bottom() <= 240);
    // Inside the Rotom map's window on the bottom screen (layouts: field.map
    // rect 29,84 - 290,231) below Rotom's eyes (down to y 97), with the
    // player marker (160,158) near the middle.
    CHECK(MapSource.x >= 29 && MapSource.Right() <= 291 && MapSource.y >= 98 && MapSource.Bottom() <= 232);
    CHECK(std::abs(MapSource.x + MapSource.w / 2 - 160) <= 4 && std::abs(MapSource.y + MapSource.h / 2 - 158) <= 12);
    // On the top screen, inside the map's area with room for the frame.
    CHECK(MapWindow.x - 2 > MapArea.x && MapWindow.Right() + 2 < MapArea.Right());
    CHECK(MapWindow.y - 2 > MapArea.y + 50 && MapWindow.Bottom() + 2 + 18 <= MapArea.Bottom());
}

static void TestDraw(const char *preview)
{
    const int bpp = 4;
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
    const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));

    const std::vector<std::string> names(21, "");
    std::vector<std::string> n = names;
    n[20] = "Hau'oli City";
    n.push_back("Shopping District");
    MapView view = MakeMapView(Where(13, 20, false, 7524.6f, 16323.3f, -58, false), n, "ZR");
    DrawMap(canvas, view);

    // Nothing outside the area is touched.
    const UI::Color outside = At(canvas, MapArea.x - 1, MapArea.y + 50);
    CHECK(outside.r == 10 && outside.g == 200 && outside.b == 10);
    // The window is filled (sea) and framed.
    const UI::Color sea = At(canvas, MapWindow.x + 3, MapWindow.y + 3);
    CHECK(sea.b > sea.r && sea.g != 200);
    const UI::Color frame = At(canvas, MapWindow.x - 1, MapWindow.y + 30);
    CHECK(frame.b > 200);
    // The compass arrow (red) points south-west: below-left of its centre.
    bool red = false;
    for (int y = MapWindow.y; y < MapWindow.Bottom(); y++)
        for (int x = MapArea.x; x < MapWindow.x; x++)
        {
            const UI::Color c = At(canvas, x, y);
            red |= c.r > 200 && c.g < 120;
        }
    CHECK(red);

    if (preview != nullptr)
    {
        // With a stand-in for the copied picture, as the 3DS shows it.
        canvas.FillRect(MapWindow, UI::Rgb(70, 140, 80));
        canvas.FillRect(UI::Rect{ MapWindow.x + 120, MapWindow.y + 44, 20, 20 }, UI::Rgb(240, 240, 240));
        FILE *f = std::fopen(preview, "wb");
        if (f == nullptr)
        {
            std::printf("can't write %s\n", preview);
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
        std::printf("map_test: preview written to %s\n", preview);
    }
}

int main(int argc, char **argv)
{
    TestRomfsText();
    TestRealPlaceNames(argc > 2 ? argv[2] : "../analysis/private/romfs-base/a/0/3/2");
    TestView();
    TestKeys();
    TestLayout();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("map_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("map_test: all passed\n");
    return 0;
}
