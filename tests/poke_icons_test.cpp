// Host tests for the game's Pokémon box icons (Sources/UI/PokeIcons.cpp):
// the lookup table read from (fake) game memory, an icon GARC in a fake
// RomFS, decoding (LZ11 BFLIM, 64x32 RGBA5551, transposed), drawing and the
// cache. All data is synthetic. Build and run: make -C tests.

#include "UI/PokeIcons.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace OneScreen;
using namespace OneScreen::UI;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static void Put16(std::vector<u8> &b, size_t at, u32 v)
{
    if (b.size() < at + 2)
        b.resize(at + 2, 0);
    b[at] = (u8)v;
    b[at + 1] = (u8)(v >> 8);
}

static void Put32(std::vector<u8> &b, size_t at, u32 v)
{
    Put16(b, at, v & 0xFFFF);
    Put16(b, at + 2, v >> 16);
}

static void Add16(std::vector<u8> &b, u32 v) { Put16(b, b.size(), v); }
static void Add32(std::vector<u8> &b, u32 v) { Put32(b, b.size(), v); }

// --- Fake game memory: the lookup function and its table ---

struct FakeMemory
{
    std::map<u32, std::vector<u8>> blocks;    // start address -> bytes

    void Write(u32 address, const std::vector<u8> &bytes) { blocks[address] = bytes; }

    MemRead Reader(void) const
    {
        return [this](u32 address, void *dst, u32 size) {
            for (const auto &b : blocks)
                if (address >= b.first && address + size <= b.first + b.second.size())
                {
                    std::memcpy(dst, b.second.data() + (address - b.first), size);
                    return true;
                }
            return false;
        };
    }
};

static const u32 TableAt = 0x5C0000, FormsAt = 0x5D0000, EggIcon = 9;

static FakeMemory MakeGame(void)
{
    FakeMemory mem;
    std::vector<u8> func;
    for (u32 w : { 0xE92D41F0u, 0xE3500FCAu, 0xE1A04001u, 0xE59D6018u })
        Add32(func, w);
    func.resize(0xCC, 0);
    Add32(func, EggIcon);
    Add32(func, TableAt);
    mem.Write(PokeIconLookupFunc, func);

    // Species 0-807: icon = species, no forms; then a few with forms or a
    // female icon. Forms: 3 -> {3, 4}; 19 -> {6, 5} (the second form's
    // icon first, as the game has it for Alolan forms); 25 -> {7, 8, 8}.
    std::vector<u8> table(808 * 16, 0);
    for (u32 sp = 0; sp < 808; sp++)
    {
        Put16(table, sp * 16, sp < 3 ? sp : 100 + sp);
        Put16(table, sp * 16 + 2, sp < 3 ? sp : 100 + sp);
    }
    std::vector<u8> forms;
    auto withForms = [&](u32 sp, std::vector<u16> icons) {
        Put16(table, sp * 16, icons[0]);
        Put16(table, sp * 16 + 2, icons[0]);
        Put32(table, sp * 16 + 4, FormsAt + (u32)forms.size());
        Put16(table, sp * 16 + 12, (u32)icons.size());
        for (u16 v : icons)
            Add16(forms, v);
    };
    withForms(3, { 3, 4 });
    withForms(19, { 6, 5 });
    withForms(25, { 7, 8, 8 });
    Put16(table, 521 * 16, 11);         // male icon
    Put16(table, 521 * 16 + 2, 10);     // female icon
    mem.Write(TableAt, table);
    mem.Write(FormsAt, forms);
    return mem;
}

static void TestLookup(void)
{
    const FakeMemory mem = MakeGame();
    PokeIconTable table;
    std::string error;
    CHECK(FindPokeIconTable(mem.Reader(), table, error));
    CHECK(table.table == TableAt && table.egg == EggIcon);

    const MemRead read = mem.Reader();
    CHECK(PokeIconIndex(read, table, 1, 0, 0, false) == 1);
    CHECK(PokeIconIndex(read, table, 2, 0, 1, false) == 2);
    CHECK(PokeIconIndex(read, table, 3, 0, 0, false) == 3);
    CHECK(PokeIconIndex(read, table, 3, 1, 0, false) == 4);
    CHECK(PokeIconIndex(read, table, 3, 5, 0, false) == 3);    // past the form count: the base icon
    CHECK(PokeIconIndex(read, table, 19, 0, 0, false) == 6);
    CHECK(PokeIconIndex(read, table, 19, 1, 1, false) == 5);
    CHECK(PokeIconIndex(read, table, 25, 2, 0, false) == 8);
    CHECK(PokeIconIndex(read, table, 521, 0, 0, false) == 11);
    CHECK(PokeIconIndex(read, table, 521, 0, 1, false) == 10);  // female
    CHECK(PokeIconIndex(read, table, 521, 0, 2, false) == 11);
    CHECK(PokeIconIndex(read, table, 25, 0, 0, true) == EggIcon);
    CHECK(PokeIconIndex(read, table, 808, 0, 0, false) == EggIcon);
    CHECK(PokeIconIndex(read, table, 809, 0, 0, false) == EggIcon);
    CHECK(PokeIconIndex(read, table, 900, 0, 0, false) == 0);   // as species 0
    CHECK(PokeIconIndex(read, PokeIconTable(), 1, 0, 0, false) == PokeIconNone);

    // Another build: the function isn't there.
    FakeMemory other = MakeGame();
    std::vector<u8> func = other.blocks[PokeIconLookupFunc];
    func[4] ^= 1;
    other.Write(PokeIconLookupFunc, func);
    CHECK(!FindPokeIconTable(other.Reader(), table, error));
    CHECK(!table.Valid());
    // A table that doesn't start as the game's does.
    FakeMemory odd = MakeGame();
    std::vector<u8> t = odd.blocks[TableAt];
    Put16(t, 16, 7);
    odd.Write(TableAt, t);
    CHECK(!FindPokeIconTable(odd.Reader(), table, error));
}

// --- An icon file: BFLIM 64x32 RGBA5551, transposed, LZ11 ---

// Pixel (x, y) of the 64x32 image for icon `seed`: opaque inside the
// picture area where (x + y + seed) % 3 != 0, clear elsewhere.
static u16 ImagePixel(u32 seed, s32 x, s32 y)
{
    if (x < 12 || x >= 52 || y < 1 || y >= 31 || (x + y + (s32)seed) % 3 == 0)
        return 0;
    const u32 r = (u32)(x * 31 / 63), g = (u32)(y * 31 / 31), b = seed & 31;
    return (u16)(r << 11 | g << 6 | b << 1 | 1);
}

static std::vector<u8> MakeIconFlim(u32 seed)
{
    // Transposed: the texture is 32 wide, 64 high; image (x, y) is texel
    // (31 - y, x). 8x8 tiles, Morton order inside.
    const u32 texW = 32, texH = 64;
    std::vector<u16> texels(texW * texH, 0);
    for (s32 y = 0; y < 32; y++)
        for (s32 x = 0; x < 64; x++)
            texels[x * texW + (texW - 1 - y)] = ImagePixel(seed, x, y);
    std::vector<u8> b;
    for (u32 ty = 0; ty < texH; ty += 8)
        for (u32 tx = 0; tx < texW; tx += 8)
            for (u32 k = 0; k < 64; k++)
            {
                const u32 x = (k & 1) | ((k >> 1) & 2) | ((k >> 2) & 4);
                const u32 y = ((k >> 1) & 1) | ((k >> 2) & 2) | ((k >> 3) & 4);
                Add16(b, texels[(ty + y) * texW + tx + x]);
            }
    const size_t f = b.size();
    b.resize(f + 0x28, 0);
    std::memcpy(&b[f], "FLIM", 4);
    Put16(b, f + 4, 0xFEFF);
    Put16(b, f + 6, 0x14);
    Put32(b, f + 8, 0x07020100);
    Put32(b, f + 0x0C, (u32)(f + 0x28));
    Put16(b, f + 0x10, 1);
    std::memcpy(&b[f + 0x14], "imag", 4);
    Put32(b, f + 0x18, 0x10);
    Put16(b, f + 0x1C, 64);
    Put16(b, f + 0x1E, 32);
    Put16(b, f + 0x20, 0x80);
    b[f + 0x22] = 7;        // RGBA5551
    b[f + 0x23] = 4;        // transposed
    Put32(b, f + 0x24, (u32)f);
    return b;
}

// LZ11 with literals only.
static std::vector<u8> Lz11Literals(const std::vector<u8> &in)
{
    std::vector<u8> out = { 0x11, (u8)in.size(), (u8)(in.size() >> 8), (u8)(in.size() >> 16) };
    for (size_t i = 0; i < in.size(); i += 8)
    {
        out.push_back(0);
        for (size_t k = i; k < i + 8 && k < in.size(); k++)
            out.push_back(in[k]);
    }
    return out;
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

// RomFS level 3 holding only a/0/6/2 = `file`.
static std::vector<u8> Romfs(const std::vector<u8> &file)
{
    const u32 None = 0xFFFFFFFF;
    std::vector<u8> dirs, files, data;
    auto dir = [&](u32 parent, u32 child, u32 firstFile, const std::string &name) {
        Add32(dirs, parent); Add32(dirs, None); Add32(dirs, child); Add32(dirs, firstFile); Add32(dirs, None);
        AddName(dirs, name);
    };
    // Directory entries: root, a, 0, 6 (each the only child of the one before).
    const u32 root = 0, a = 0x18, d0 = a + 0x18 + 4, d6 = d0 + 0x18 + 4;
    dir(root, a, None, "");
    dir(root, d0, None, "a");
    dir(a, d6, None, "0");
    dir(d0, None, 0, "6");
    Add32(files, d6); Add32(files, None);
    Add32(files, 0); Add32(files, 0);
    Add32(files, (u32)file.size()); Add32(files, 0);
    Add32(files, None);
    AddName(files, "2");
    data = file;

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
    return l3;
}

static RomfsRead ReaderOf(const std::vector<u8> &image, int *reads)
{
    return [&image, reads](uint64_t offset, void *dst, u32 size) {
        (*reads)++;
        if (offset + size > image.size())
            return false;
        std::memcpy(dst, image.data() + offset, size);
        return true;
    };
}

static bool SameAsImage(const PokeIcon &icon, u32 seed)
{
    for (s32 y = 0; y < PokeIconH; y++)
        for (s32 x = 0; x < PokeIconW; x++)
            if (icon.px[y * PokeIconW + x] != ImagePixel(seed, x + 12, y + 1))
                return false;
    return true;
}

static void TestDecodeAndArchive(void)
{
    // One file decoded: the 40x30 at (12, 1), pixel for pixel.
    PokeIcon icon;
    std::string error;
    const std::vector<u8> plain = MakeIconFlim(5);
    CHECK(DecodePokeIcon(plain.data(), plain.size(), icon, error));
    CHECK(SameAsImage(icon, 5));
    const std::vector<u8> packed = Lz11Literals(MakeIconFlim(6));
    CHECK(DecodePokeIcon(packed.data(), packed.size(), icon, error));
    CHECK(SameAsImage(icon, 6));
    std::vector<u8> broken = packed;
    broken.resize(broken.size() / 2);
    CHECK(!DecodePokeIcon(broken.data(), broken.size(), icon, error));

    // The archive: 12 icons (seed = index), entry 3 broken.
    std::vector<std::vector<u8>> files;
    for (u32 i = 0; i < 12; i++)
        files.push_back(i == 3 ? std::vector<u8>(40, 0x11) : Lz11Literals(MakeIconFlim(i)));
    const std::vector<u8> image = Romfs(Garc(files));
    int reads = 0;
    PokeIconArchive archive;
    CHECK(archive.Open(ReaderOf(image, &reads), error));
    CHECK(archive.IsOpen() && archive.Count() == 12);
    CHECK(reads < 20);      // the entry table in one read
    reads = 0;
    CHECK(archive.Load(7, icon, error) && icon.index == 7 && SameAsImage(icon, 7));
    CHECK(reads == 1);
    CHECK(!archive.Load(3, icon, error));
    CHECK(!archive.Load(12, icon, error));
    archive.Close();
    CHECK(!archive.IsOpen());
    CHECK(!archive.Load(7, icon, error));

    // No a/0/6/2 (an empty RomFS of a/0/6/1 would be the same).
    const std::vector<u8> notGarc = Romfs(std::vector<u8>(64, 0));
    CHECK(!archive.Open(ReaderOf(notGarc, &reads), error));

    // The cache: a few per call, wanted ones kept, the oldest dropped.
    CHECK(archive.Open(ReaderOf(image, &reads), error));
    PokeIconCache cache(4);
    u32 wanted[5] = { 1, PokeIconNone, 2, 3, 4 };
    error.clear();
    CHECK(cache.Fill(archive, wanted, 5, 2, error) == 2);
    CHECK(cache.Find(1) != nullptr && cache.Find(2) != nullptr && cache.Find(4) == nullptr);
    CHECK(cache.Fill(archive, wanted, 5, 10, error) == 2);     // 3 fails, 4 loads
    CHECK(!error.empty());
    CHECK(cache.Find(3) == nullptr && cache.Find(4) != nullptr && SameAsImage(*cache.Find(4), 4));
    CHECK(cache.Fill(archive, wanted, 5, 10, error) == 0);     // nothing left (3 not retried)
    CHECK(cache.Next(wanted, 5) == PokeIconNone);
    CHECK(cache.Size() == 3);
    // New box: 5, 6 wanted; capacity 4, so 1 of the old ones goes.
    u32 next[2] = { 5, 6 };
    CHECK(cache.Fill(archive, next, 2, 10, error) == 2);
    CHECK(cache.Find(5) != nullptr && cache.Find(6) != nullptr && cache.Size() == 4);
    CHECK((cache.Find(1) != nullptr) + (cache.Find(2) != nullptr) + (cache.Find(4) != nullptr) == 2);
    // More wanted than fit: the extra one isn't stored.
    u32 many[5] = { 7, 8, 9, 10, 11 };
    error.clear();
    cache.Fill(archive, many, 5, 10, error);
    CHECK(cache.Size() == 4 && !error.empty());
    CHECK(cache.Find(7) != nullptr && cache.Find(10) != nullptr && cache.Find(11) == nullptr);
    CHECK(cache.Bytes() == 4 * sizeof(PokeIcon));
    cache.Clear();
    CHECK(cache.Size() == 0 && cache.Find(7) == nullptr && cache.Bytes() == 0);
}

static void TestDraw(void)
{
    PokeIcon icon;
    for (s32 i = 0; i < PokeIconW * PokeIconH; i++)
        icon.px[i] = 0;
    icon.px[0] = (u16)(31 << 11 | 1);                               // red, top-left
    icon.px[PokeIconW * PokeIconH - 1] = (u16)(31 << 1 | 1);        // blue, bottom-right
    icon.px[1] = (u16)(31 << 6);                                    // green but clear

    const int w = 64, h = 48, bpp = 4;
    std::vector<u8> pixels(w * h * bpp, 0);
    const Surface surface = { pixels.data(), bpp, w * bpp, PixelFormat::RGBA8, w, h };
    Canvas canvas(surface);
    canvas.FillRect(Rect{ 0, 0, w, h }, Rgb(10, 20, 30));
    DrawPokeIcon(canvas, icon, 5, 6);
    Color c;
    CHECK(canvas.GetPixel(5, 6, c) && c.r == 255 && c.g == 0 && c.b == 0);
    CHECK(canvas.GetPixel(5 + 39, 6 + 29, c) && c.b == 255 && c.r == 0);
    CHECK(canvas.GetPixel(6, 6, c) && c.r == 10 && c.g == 20);        // clear pixel left alone
    // Clipped: nothing outside the clip.
    canvas.FillRect(Rect{ 0, 0, w, h }, Rgb(10, 20, 30));
    canvas.SetClip(Rect{ 20, 20, 10, 10 });
    DrawPokeIcon(canvas, icon, 5, 6);
    canvas.ResetClip();
    CHECK(canvas.GetPixel(5, 6, c) && c.r == 10);
}

int main()
{
    TestLookup();
    TestDecodeAndArchive();
    TestDraw();
    if (s_failures != 0)
    {
        std::printf("poke_icons_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("poke_icons_test: all passed\n");
    return 0;
}
