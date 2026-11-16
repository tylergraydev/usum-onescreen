// Host tests for the native Bag's pure part (Sources/Screens/BagList.cpp):
// tabs and actions, the list cursor, which menu entries are native, items
// with a follow-up and the result lines, the pocket icons and descriptions
// read from a made-up RomFS, and the driver against a small fake of the
// game's Bag. Build and run: make -C tests. With a path argument, also
// writes a preview of the Bag there as a PPM image.

#include "hires_screen_smoke.hpp"
#include "Screens/BagList.hpp"

#include <cstdio>
#include <cstring>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

// --- Byte builders: message files, layout archives, LZ11 ---

static void Put16(std::vector<u8> &b, size_t at, u32 v)
{
    if (b.size() < at + 2)
        b.resize(at + 2);
    b[at] = (u8)v;
    b[at + 1] = (u8)(v >> 8);
}

static void Put32(std::vector<u8> &b, size_t at, u32 v)
{
    Put16(b, at, v & 0xFFFF);
    Put16(b, at + 2, v >> 16);
}

// A message file (docs/RE2-text.md) with ASCII lines, encrypted as the game's.
static std::vector<u8> MakeMessageFile(const std::vector<std::string> &lines)
{
    std::vector<u8> b;
    Put16(b, 0, 1);
    Put16(b, 2, (u32)lines.size());
    const u32 section = 0x10;
    Put32(b, 0x0C, section);
    u32 text = 4 + (u32)lines.size() * 8;
    for (size_t l = 0; l < lines.size(); l++)
    {
        const u32 length = (u32)lines[l].size() + 1;   // with the terminating 0
        Put32(b, section + 4 + l * 8, text);
        Put16(b, section + 4 + l * 8 + 4, length);
        u16 key = (u16)(0x2983 * (l + 3));
        for (u32 i = 0; i < length; i++)
        {
            const u16 c = i < lines[l].size() ? (u16)(u8)lines[l][i] : 0;
            Put16(b, section + text + i * 2, c ^ key);
            key = (u16)((key << 3) | (key >> 13));
        }
        text += length * 2;
    }
    Put32(b, section, text);
    return b;
}

// An ALYT stand-in (16 bytes) and a SARC with these files, each `gap`
// bytes after the last (filled with a pattern that repeats every 4096).
static std::vector<u8> MakeArchive(const std::vector<std::string> &names, const std::vector<std::vector<u8>> &data, u32 gap)
{
    std::vector<u8> b(16, 0);
    std::memcpy(&b[0], "ALYT", 4);
    const size_t s = 16, sfat = 0x14, sfnt = sfat + 0x0C + names.size() * 16;
    b.resize(s + sfnt + 8);
    std::memcpy(&b[s], "SARC", 4);
    Put16(b, s + 4, 0x14);
    Put16(b, s + 6, 0xFEFF);
    std::memcpy(&b[s + sfat], "SFAT", 4);
    Put16(b, s + sfat + 4, 0x0C);
    Put16(b, s + sfat + 6, (u32)names.size());
    std::memcpy(&b[s + sfnt], "SFNT", 4);
    Put16(b, s + sfnt + 4, 8);
    size_t nameAt = 0;
    for (size_t i = 0; i < names.size(); i++)
    {
        b.resize(s + sfnt + 8 + nameAt + (names[i].size() + 4) / 4 * 4, 0);
        std::memcpy(&b[s + sfnt + 8 + nameAt], names[i].c_str(), names[i].size());
        Put32(b, s + sfat + 0x0C + i * 16 + 4, 0x01000000 | (u32)(nameAt / 4));
        nameAt += (names[i].size() + 4) / 4 * 4;
    }
    const u32 dataAt = (u32)((b.size() - s + 0xFF) / 0x100 * 0x100);
    Put32(b, s + 0x0C, dataAt);
    b.resize(s + dataAt, 0);
    u32 off = 0;
    for (size_t i = 0; i < names.size(); i++)
    {
        for (u32 k = 0; k < gap; k++)
            b.push_back((u8)((off + k) % 4096 % 253));
        off += gap;
        Put32(b, s + sfat + 0x0C + i * 16 + 8, off);
        Put32(b, s + sfat + 0x0C + i * 16 + 12, off + (u32)data[i].size());
        b.insert(b.end(), data[i].begin(), data[i].end());
        off += (u32)data[i].size();
    }
    Put32(b, s + 8, (u32)(b.size() - s));
    return b;
}

// LZ11 with copies 1 back (runs) and 4096 back (the gap pattern), else
// literals.
static std::vector<u8> Lz11Compress(const std::vector<u8> &in)
{
    std::vector<u8> out = { 0x11, (u8)in.size(), (u8)(in.size() >> 8), (u8)(in.size() >> 16) };
    size_t i = 0;
    while (i < in.size())
    {
        const size_t flagsAt = out.size();
        out.push_back(0);
        for (int bit = 0; bit < 8 && i < in.size(); bit++)
        {
            size_t best = 0, disp = 0;
            for (size_t d : { (size_t)1, (size_t)4096 })
            {
                size_t n = 0;
                while (i >= d && i + n < in.size() && n < 16 && in[i + n] == in[i + n - d])
                    n++;
                if (n >= 3 && n > best)
                {
                    best = n;
                    disp = d;
                }
            }
            if (best == 0)
            {
                out.push_back(in[i++]);
                continue;
            }
            out[flagsAt] |= (u8)(0x80 >> bit);
            out.push_back((u8)(((best - 1) << 4) | ((disp - 1) >> 8)));
            out.push_back((u8)((disp - 1) & 0xFF));
            i += best;
        }
    }
    return out;
}

static void TestTables(void)
{
    CHECK(BagTabFind("medicine") == 1);
    CHECK(BagTabFind("rotom_powers") == 0);
    CHECK(BagTabFind("free_space") == BagTabCount - 1);
    CHECK(BagTabFind("nope") == -1);
    for (int i = 0; i < BagTabCount; i++)
    {
        const LayoutButton *b = FindLayoutButton(BagTabAt(i).button);
        CHECK(b != nullptr && b->verified == LayoutVerified::Tap);
        // Tabs sit left to right in the game's tab order.
        if (i > 0)
            CHECK(b != nullptr && b->tapX > FindLayoutButton(BagTabAt(i - 1).button)->tapX);
    }

    // Every command has an action; all of them may run except the menu
    // position that was never tapped (5: no 5-entry menu seen).
    BagCmd cmd;
    std::vector<BagCmd> all;
    for (int i = 0; i < BagTabCount; i++) all.push_back(BagCmd{ BagStep::TapTab, i });
    for (int r = 1; r <= 7; r++) all.push_back(BagCmd{ BagStep::TapRow, r });
    for (int p = 1; p <= 5; p++) all.push_back(BagCmd{ BagStep::TapMenu, p });
    for (int s = 0; s < 6; s++) all.push_back(BagCmd{ BagStep::TapParty, s });
    for (BagStep s : { BagStep::PressUp, BagStep::PressDown, BagStep::TapYes, BagStep::TapNo, BagStep::PressA,
                       BagStep::PressB, BagStep::TapBack })
        all.push_back(BagCmd{ s, 0 });
    for (const BagCmd &c : all)
    {
        const ActionDef *def = BagAction(c);
        CHECK(def != nullptr);
        if (def == nullptr)
            continue;
        CHECK(std::strncmp(def->name, "bag-", 4) == 0);
        CHECK(def->summary != nullptr);
        CHECK(def->needs.scene != nullptr && std::string(def->needs.scene) == SceneBag);
        const std::string bad = CheckAction(*def);
        const bool untapped = c.step == BagStep::TapMenu && c.arg == 5;
        if (bad.empty() == untapped)
            std::printf("    %s: '%s'\n", def->name, bad.c_str());
        CHECK(bad.empty() != untapped);
    }
    CHECK(BagAction(BagCmd{ BagStep::TapRow, 0 }) == nullptr);
    CHECK(BagAction(BagCmd{ BagStep::TapRow, 8 }) == nullptr);
    CHECK(BagAction(BagCmd{ BagStep::TapParty, 6 }) == nullptr);
    CHECK(BagAction(BagCmd{ BagStep::TapTab, BagTabCount }) == nullptr);
    CHECK(BagAction(cmd) == nullptr);

    CHECK(std::string(BagCommandLabel(BagCommand::Give)) == "Give");
    CHECK(std::string(BagCommandLabel(0x07)) == "Option 0x07");
    CHECK(std::string(BagCommandLabel(BagCommand::ReturnToPocket)) == "Return to Pocket");
    CHECK(std::string(BagCommandLabel(BagCommand::Off)) == "Off");

    // Every entry of every menu seen runs natively (positions 1-4); a fifth
    // entry from the bottom was never tapped.
    const std::vector<u8> potion = { BagCommand::Use, BagCommand::Give, BagCommand::FreeSpace, BagCommand::Cancel };
    for (int i = 0; i < 4; i++)
        CHECK(BagEntryEnabled(potion, i));
    CHECK(!BagEntryEnabled(potion, 4) && !BagEntryEnabled(potion, -1));
    const std::vector<u8> expShare = { BagCommand::Off, BagCommand::FreeSpace, BagCommand::Cancel };
    const std::vector<u8> freeSpace = { BagCommand::Give, BagCommand::ReturnToPocket, BagCommand::Cancel };
    for (int i = 0; i < 3; i++)
        CHECK(BagEntryEnabled(expShare, i) && BagEntryEnabled(freeSpace, i));
    const std::vector<u8> five = { 0x07, BagCommand::Use, BagCommand::Give, BagCommand::FreeSpace, BagCommand::Cancel };
    CHECK(!BagEntryEnabled(five, 0) && BagEntryEnabled(five, 1));
    const std::vector<u8> yesNo = { BagCommand::Yes, BagCommand::No };
    CHECK(BagEntryEnabled(yesNo, 0) && BagEntryEnabled(yesNo, 1));

    // Pocket icons: the selected icon and its "2" variant, all different.
    for (int i = 0; i < BagTabCount; i++)
    {
        CHECK(std::strncmp(BagTabAt(i).icon, "Bag_Icon_", 9) == 0);
        for (int j = 0; j < i; j++)
            CHECK(std::strcmp(BagTabAt(i).icon, BagTabAt(j).icon) != 0);
    }
    CHECK(std::string(BagTabAt(BagTabFind("rotom_powers")).icon) == "Bag_Icon_06f");
    CHECK(std::string(BagTabAt(BagTabFind("medicine")).icon) == "Bag_Icon_01b");
}

static void TestItems(void)
{
    // Kinds of the items with a follow-up.
    CHECK(BagItemKindOf(17) == BagItemKind::Other);           // Potion
    CHECK(BagItemKindOf(78) == BagItemKind::Other);           // Escape Rope
    CHECK(BagItemKindOf(38) == BagItemKind::PpOne);           // Ether
    CHECK(BagItemKindOf(53) == BagItemKind::PpOne);           // PP Max
    CHECK(BagItemKindOf(40) == BagItemKind::PpAll);           // Elixir
    CHECK(BagItemKindOf(79) == BagItemKind::Repel && BagItemKindOf(76) == BagItemKind::Repel);
    CHECK(BagItemKindOf(50) == BagItemKind::RareCandy);
    CHECK(BagItemKindOf(82) == BagItemKind::Stone && BagItemKindOf(849) == BagItemKind::Stone);
    CHECK(BagItemKindOf(328) == BagItemKind::Tm && BagItemKindOf(419) == BagItemKind::Tm);    // TM01, TM92
    CHECK(BagItemKindOf(420) == BagItemKind::Other);          // HM01
    CHECK(BagItemKindOf(618) == BagItemKind::Tm && BagItemKindOf(694) == BagItemKind::Tm);    // TM93, TM100
    CHECK(BagItemKindOf(695) == BagItemKind::Other);
    CHECK(std::string(BagItemKindName(BagItemKind::Tm)) == "tm");
    CHECK(std::string(BagPickQuestion(328)) == "Which Pokémon do you want to teach it to?");
    CHECK(std::string(BagPickQuestion(17)) == "Which Pokémon do you want to use this item on?");

    // Stepping aside: only during a Use, only after the game was busy long
    // enough (an HP bar filling is shorter), and back once it waits in its
    // list or party pick.
    CHECK(BagFollowGame(BagMode::Busy, BagAsideFrames, true, false) == BagFollow::Aside);
    CHECK(BagFollowGame(BagMode::Busy, BagAsideFrames - 1, true, false) == BagFollow::Native);
    CHECK(BagFollowGame(BagMode::Busy, 10000, false, false) == BagFollow::Native);
    CHECK(BagFollowGame(BagMode::Message, 10000, true, false) == BagFollow::Native);
    CHECK(BagFollowGame(BagMode::Busy, 0, true, true) == BagFollow::Aside);
    CHECK(BagFollowGame(BagMode::Message, 0, true, true) == BagFollow::Aside);
    CHECK(BagFollowGame(BagMode::List, 0, true, true) == BagFollow::Native);
    CHECK(BagFollowGame(BagMode::Pick, 0, true, true) == BagFollow::Native);

    // Result lines.
    BagResult r;
    r.command = BagCommand::Use;
    r.item = "Potion";
    r.countBefore = 9;
    r.countAfter = 8;
    r.picked = true;
    r.mon = "Pichu";
    r.hpBefore = 7;
    r.hpAfter = 27;
    r.maxHp = 27;
    r.levelBefore = r.levelAfter = 12;
    CHECK(BagResultText(r) == "Pichu: HP 7 -> 27/27");
    r.hpAfter = 7;
    r.countAfter = 9;
    CHECK(BagResultText(r) == "Potion had no effect on Pichu");
    // Ether: PP back.
    r.item = "Ether";
    r.kind = BagItemKind::PpOne;
    r.countAfter = 8;
    r.ppBefore = 40;
    r.ppAfter = 50;
    CHECK(BagResultText(r) == "Pichu's PP restored");
    // Rare Candy: a level and a move.
    r = BagResult();
    r.command = BagCommand::Use;
    r.kind = BagItemKind::RareCandy;
    r.item = "Rare Candy";
    r.countBefore = 1;
    r.picked = true;
    r.mon = "Pichu";
    r.levelBefore = 12;
    r.levelAfter = 13;
    r.hpBefore = 27;
    r.hpAfter = 29;
    r.maxHp = 29;
    r.learned = { "Sweet Kiss" };
    CHECK(BagResultText(r) == "Pichu grew to Lv. 13, Pichu learned Sweet Kiss, Pichu: HP 27 -> 29/29");
    // Stone: an evolution.
    r.kind = BagItemKind::Stone;
    r.item = "Thunder Stone";
    r.levelAfter = 12;
    r.speciesBefore = 25;
    r.speciesAfter = 26;
    r.speciesName = "Raichu";
    CHECK(BagResultText(r) == "Pichu evolved into Raichu");
    // TM: learned, or not.
    r = BagResult();
    r.command = BagCommand::Use;
    r.kind = BagItemKind::Tm;
    r.item = "TM01";
    r.countBefore = r.countAfter = 1;
    r.picked = true;
    r.mon = "Torracat";
    r.hpBefore = r.hpAfter = r.maxHp = 51;
    r.levelBefore = r.levelAfter = 17;
    CHECK(BagResultText(r) == "Torracat did not learn the move");
    r.learned = { "Work Up" };
    CHECK(BagResultText(r) == "Torracat learned Work Up");
    // Repel / Escape Rope: no Pokémon.
    r = BagResult();
    r.command = BagCommand::Use;
    r.kind = BagItemKind::Repel;
    r.item = "Repel";
    r.countBefore = 2;
    r.countAfter = 1;
    CHECK(BagResultText(r) == "Used Repel");
    r.countAfter = 2;
    CHECK(BagResultText(r) == "Repel wasn't used");
    // Key Items and Free Space.
    r.item = "Exp. Share";
    r.command = BagCommand::Off;
    CHECK(BagResultText(r) == "Exp. Share turned off");
    r.command = BagCommand::On;
    CHECK(BagResultText(r) == "Exp. Share turned on");
    r.item = "Quick Claw";
    r.command = BagCommand::FreeSpace;
    CHECK(BagResultText(r) == "Quick Claw moved to the Free Space");
    r.command = BagCommand::ReturnToPocket;
    CHECK(BagResultText(r) == "Quick Claw returned to its pocket");
    // Give.
    r = BagResult();
    r.command = BagCommand::Give;
    r.item = "Escape Rope";
    r.picked = true;
    r.mon = "Pichu";
    r.heldBefore = "Quick Claw";
    r.heldAfter = "Escape Rope";
    r.holdsItem = true;
    CHECK(BagResultText(r) == "Pichu now holds Escape Rope (Quick Claw back in the Bag)");
    r.holdsItem = false;
    r.heldAfter = "Quick Claw";
    CHECK(BagResultText(r) == "Pichu still holds Quick Claw");
    r.command = BagCommand::Cancel;
    CHECK(BagResultText(r).empty());

    // Descriptions: one line of the message file, read on its own; line
    // breaks become spaces.
    const std::vector<std::string> texts = { "", "Catches\nanything.", "  Two\n\nlines  " };
    const std::vector<u8> msg = MakeMessageFile(texts);
    const std::vector<u8> image = std::vector<u8>(100, 0xEE);   // the file sits at 100 in the "RomFS"
    std::vector<u8> romfs = image;
    romfs.insert(romfs.end(), msg.begin(), msg.end());
    int reads = 0;
    const RomfsRead read = [&romfs, &reads](uint64_t offset, void *dst, u32 size) {
        reads++;
        if (offset + size > romfs.size())
            return false;
        std::memcpy(dst, &romfs[offset], size);
        return true;
    };
    std::string line, error;
    CHECK(ReadMessageLine(read, 100, (u32)msg.size(), 1, line, error) && line == "Catches\nanything.");
    CHECK(!ReadMessageLine(read, 100, (u32)msg.size(), 3, line, error) && !error.empty());
    CHECK(!ReadMessageLine(read, 100, 8, 1, line, error));
    BagAssets assets;
    CHECK(assets.Pocket(0, true) == nullptr && assets.PocketCount() == 0);
    CHECK(BagReadDescription(read, assets, 1).empty());     // no file found
    assets.descriptionsAt = 100;
    assets.descriptionsSize = (u32)msg.size();
    reads = 0;
    CHECK(BagReadDescription(read, assets, 1) == "Catches anything.");
    CHECK(reads <= 3);                                      // header, line entry, text: not the whole file
    CHECK(BagReadDescription(read, assets, 2) == "Two lines");
    CHECK(BagReadDescription(read, assets, 0).empty() && BagReadDescription(read, assets, 60000).empty());
    assets.pockets[1][0].w = assets.pockets[1][0].h = 2;
    assets.pockets[1][0].rgba.assign(16, 255);
    CHECK(assets.Pocket(1, true) == &assets.pockets[1][0] && assets.Pocket(1, false) == nullptr);
    CHECK(assets.Pocket(BagTabCount, true) == nullptr && assets.PocketCount() == 1);

    // Loading from a RomFS that isn't there: both parts fail and say so.
    std::vector<std::string> log;
    BagAssets none;
    LoadBagAssets([](uint64_t, void *, u32) { return false; }, 2, none, log);
    CHECK(log.size() == 2 && none.PocketCount() == 0 && none.descriptionsSize == 0);
    for (const std::string &l : log)
        CHECK(l.find("not loaded") != std::string::npos);
}

static void TestArchive(void)
{
    // A layout archive bigger than the decoder's window, with the wanted
    // files far apart, compressed with literals and copies (1 back, 4096
    // back): the streamed read finds exactly those files.
    const std::vector<std::string> names = { "timg/Bag_Icon_01b.bflim", "timg/Bag_Icon_06f2.bflim", "timg/other.bflim" };
    std::vector<std::vector<u8>> contents(3);
    for (int f = 0; f < 3; f++)
        for (int i = 0; i < 1000 + f * 300; i++)
            contents[f].push_back((u8)(i * 7 + f * 31));
    const std::vector<u8> alyt = MakeArchive(names, contents, 9000);
    const std::vector<u8> lz = Lz11Compress(alyt);
    CHECK(lz.size() < alyt.size());
    std::vector<u8> romfs(64, 0);
    romfs.insert(romfs.end(), lz.begin(), lz.end());
    const RomfsRead read = [&romfs](uint64_t offset, void *dst, u32 size) {
        if (offset + size > romfs.size())
            return false;
        std::memcpy(dst, &romfs[offset], size);
        return true;
    };
    std::vector<std::vector<u8>> files;
    std::string error;
    const std::vector<std::string> wanted = { names[1], "timg/missing.bflim", names[0] };
    CHECK(Lz11ArchiveFiles(read, 64, (u32)lz.size(), wanted, files, error));
    CHECK(files.size() == 3 && files[0] == contents[1] && files[1].empty() && files[2] == contents[0]);
    // A cut stream, a stream that isn't LZ11, data that isn't an archive.
    CHECK(!Lz11ArchiveFiles(read, 64, (u32)lz.size() / 2, wanted, files, error) && !error.empty());
    CHECK(!Lz11ArchiveFiles(read, 0, 64, wanted, files, error));
    std::vector<u8> plain(20000, 'z');
    const std::vector<u8> lzPlain = Lz11Compress(plain);
    const RomfsRead readPlain = [&lzPlain](uint64_t offset, void *dst, u32 size) {
        if (offset + size > lzPlain.size())
            return false;
        std::memcpy(dst, &lzPlain[offset], size);
        return true;
    };
    CHECK(!Lz11ArchiveFiles(readPlain, 0, (u32)lzPlain.size(), wanted, files, error));
    CHECK(error == "not a layout archive");
}

static void TestCursor(void)
{
    CHECK(BagMoveCursor(0, -1, 9) == 8);
    CHECK(BagMoveCursor(8, 1, 9) == 0);
    CHECK(BagMoveCursor(3, 1, 9) == 4);
    CHECK(BagMoveCursor(5, 1, 0) == 0);
    CHECK(BagListTop(0, 3, 5) == 0);        // short list: never scrolls
    CHECK(BagListTop(0, 4, 9) == 0);        // 5 rows: 0..4 fit
    CHECK(BagListTop(0, 5, 9) == 1);
    CHECK(BagListTop(1, 0, 9) == 0);        // wrapped to the top
    CHECK(BagListTop(0, 8, 20) == 4);
    CHECK(BagListTop(10, 12, 20) == 10);    // still visible: stays
    CHECK(BagListTop(10, 19, 20) == 15);    // never past the end
    CHECK(BagListTop(0, 3, 8, 8) == 0 && BagListTop(0, 8, 9, 8) == 1);  // other row counts
}

// --- A fake of the game's field Bag ---

struct FakeBag
{
    BagGame g;
    std::vector<std::vector<BagItem>> pockets;
    std::vector<int> cursorOf, scrollOf;    // per pocket, as the game remembers
    int busy = 0;                           // frames of Busy before the next state shows
    BagMode after = BagMode::List;
    std::vector<int> queue;                 // states after each A in a message: modes
    u16 held[6] = {};
    int lastPicked = -1;
    u8 pending = 0;                         // Use or Give chosen in the menu
    bool ignoreTaps = false;
    bool closed = false;
    int away = 0;                           // frames the game spends on another screen (a TM's move list)
    BagGame saved;                          // the Bag while it is away
    int commands = 0;
    std::vector<BagCmd> log;

    FakeBag()
    {
        pockets.resize(BagTabCount);
        cursorOf.assign(BagTabCount, 0);
        scrollOf.assign(BagTabCount, 0);
        for (int i = 0; i < 12; i++)
            pockets[1].push_back(BagItem{ (u16)(17 + i), (u16)(i + 1), false });
        pockets[2].push_back(BagItem{ 217, 1, false });     // Quick Claw
        pockets[2].push_back(BagItem{ 78, 3, false });      // Escape Rope
        pockets[3].push_back(BagItem{ 328, 1, false });     // TM01
        held[3] = 217;
        Show(1);
        g.mode = BagMode::List;
    }

    void Show(int tab)
    {
        g.tab = tab;
        g.items = pockets[tab];
        g.visible = (int)g.items.size() < 7 ? (int)g.items.size() : 7;
        g.cursorRow = cursorOf[tab];
        g.scroll = scrollOf[tab];
    }

    void Save(void)
    {
        cursorOf[g.tab] = g.cursorRow;
        scrollOf[g.tab] = g.scroll;
    }

    void Move(int delta)
    {
        const int n = (int)g.items.size();
        int index = (g.Index() + delta + n) % n;
        // The game keeps the cursor on row 3 while it can scroll.
        int scroll = index - 3;
        if (scroll < 0) scroll = 0;
        if (scroll > n - g.visible) scroll = n - g.visible;
        g.scroll = scroll;
        g.cursorRow = index - scroll;
        Save();
    }

    void Then(BagMode mode, int frames = 3)
    {
        busy = frames;
        after = mode;
        g.mode = BagMode::Busy;
    }

    void OpenMenu(void)
    {
        const BagItem &it = g.items[g.Index()];
        g.item = it.id;
        g.commands.clear();
        if (g.tab == 1 || g.tab == 3 || it.id == 78)
            g.commands.push_back(BagCommand::Use);
        if (g.tab != 3)
            g.commands.push_back(BagCommand::Give);
        g.commands.push_back(BagCommand::FreeSpace);
        g.commands.push_back(BagCommand::Cancel);
        Then(BagMode::Menu);
    }

    void Apply(const BagCmd &c)
    {
        log.push_back(c);
        commands++;
        if (ignoreTaps)
            return;
        switch (c.step)
        {
        case BagStep::TapTab:
            if (g.mode == BagMode::List)
            {
                Show(c.arg);
                g.scroll = -1;  // slides in
                Then(BagMode::List);
            }
            break;
        case BagStep::TapRow:
            if (g.mode == BagMode::List && c.arg <= g.visible)
            {
                g.cursorRow = c.arg - 1;
                Save();
                OpenMenu();
            }
            break;
        case BagStep::PressUp:
        case BagStep::PressDown:
            if (g.mode == BagMode::List)
                Move(c.step == BagStep::PressUp ? -1 : 1);
            break;
        case BagStep::TapMenu:
        {
            if (g.mode != BagMode::Menu)
                break;
            const int entry = (int)g.commands.size() - c.arg;
            const u8 cmd = g.commands[entry];
            g.commands.clear();
            if (cmd == BagCommand::Cancel)
                Then(BagMode::List);
            else if (cmd == BagCommand::FreeSpace)
            {
                // Out of its pocket into the Free Space; a message, then the list.
                const BagItem it = g.items[g.Index()];
                pockets[g.tab].erase(pockets[g.tab].begin() + g.Index());
                pockets[6].push_back(it);
                Show(g.tab);
                if (g.Index() >= (int)g.items.size())
                    g.cursorRow = g.scroll = 0;
                queue = { (int)BagMode::List };
                Then(BagMode::Message);
            }
            else if (cmd == BagCommand::Use || cmd == BagCommand::Give)
            {
                g.partyCursor = 0;
                pending = cmd;
                Then(BagMode::Pick);
            }
            break;
        }
        case BagStep::TapParty:
            if (g.mode != BagMode::Pick)
                break;
            if (g.partyCursor != c.arg)
            {
                g.partyCursor = c.arg;
                break;
            }
            {
                const bool give = pending == BagCommand::Give;
                lastPicked = c.arg;
                // Use: message, then back to the pick. Give: message, then
                // the list; over a held item a question first.
                queue.clear();
                if (BagItemKindOf(g.item) == BagItemKind::Tm)
                    // "You booted up the TM." ... then the move-forget
                    // screen, and back with "did not learn".
                    queue = { (int)BagMode::Message, -1, (int)BagMode::List };
                else if (give && held[c.arg] != 0)
                    queue = { (int)BagMode::Menu };     // Yes / No after the message
                else
                    queue = { (int)(give ? BagMode::List : BagMode::Pick) };
                if (give && held[c.arg] == 0)
                    held[c.arg] = g.item;
                Then(BagMode::Message);
            }
            break;
        case BagStep::PressA:
            if (g.mode == BagMode::Message && !queue.empty())
            {
                const int nextValue = queue.front();
                const BagMode next = (BagMode)nextValue;
                queue.erase(queue.begin());
                if (nextValue < 0)
                {
                    saved = g;
                    saved.mode = BagMode::Message;
                    g = BagGame();
                    away = 60;
                }
                else if (next == BagMode::Menu)
                {
                    g.commands = { BagCommand::Yes, BagCommand::No };
                    Then(BagMode::Menu);
                }
                else
                    Then(next);
            }
            break;
        case BagStep::TapYes:
        case BagStep::TapNo:
            if (g.YesNo())
            {
                if (c.step == BagStep::TapYes)
                    held[lastPicked] = g.item;
                g.commands.clear();
                queue = { (int)BagMode::List };
                Then(BagMode::Message);
            }
            break;
        case BagStep::PressB:
            if (g.mode == BagMode::Menu || g.mode == BagMode::Pick)
            {
                g.commands.clear();
                Then(BagMode::List);
            }
            break;
        case BagStep::TapBack:
            if (g.mode == BagMode::List)
                closed = true;
            break;
        case BagStep::None:
            break;
        }
    }

    // One frame of the game.
    void Frame(void)
    {
        if (closed)
        {
            g = BagGame();
            return;
        }
        if (away > 0)
        {
            if (--away == 0)
                g = saved;
            return;
        }
        if (busy > 0 && --busy == 0)
        {
            g.mode = after;
            if (g.scroll < 0)
                g.scroll = scrollOf[g.tab];
        }
    }
};

static void PrintLog(const std::vector<std::string> &log)
{
    for (const std::string &l : log)
        std::printf("    %s\n", l.c_str());
}

// Runs the driver until it is idle; each command takes `actionFrames`
// frames in the action layer. Returns true if it ended without failure; the
// driver's log is printed if that isn't what the test `expects`.
static bool Run(BagDriver &d, FakeBag &f, bool expects = true, u32 maxFrames = 2000, int actionFrames = 8)
{
    std::vector<std::string> log;
    int running = 0;
    for (u32 i = 0; i < maxFrames && d.Busy(); i++)
    {
        f.Frame();
        const BagCmd cmd = d.Update(f.g, running > 0, log);
        if (running > 0)
            running--;
        if (cmd.step != BagStep::None)
        {
            CHECK(BagAction(cmd) != nullptr);
            f.Apply(cmd);
            running = actionFrames;
        }
    }
    const bool ok = !d.Busy() && d.Failure().empty();
    if (ok != expects)
        PrintLog(log);
    return ok;
}

static int Count(const FakeBag &f, BagStep step)
{
    int n = 0;
    for (const BagCmd &c : f.log)
        n += c.step == step;
    return n;
}

static void TestDriver(void)
{
    // Pocket switch: one tab tap, done once the list has settled.
    {
        FakeBag f;
        BagDriver d;
        d.Tab(2);
        CHECK(Run(d, f));
        CHECK(f.g.tab == 2 && f.commands == 1 && f.log[0] == (BagCmd{ BagStep::TapTab, 2 }));
        const u32 finished = d.Finished();
        d.Tab(2);   // already there: nothing to do
        CHECK(Run(d, f));
        CHECK(f.commands == 1 && d.Finished() == finished + 1);
    }
    // Open an item in the window: one row tap.
    {
        FakeBag f;
        BagDriver d;
        d.Open(4, f.g.items[4].id);
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::Menu && f.g.item == 21);
        CHECK(f.commands == 1 && f.log[0] == (BagCmd{ BagStep::TapRow, 5 }));
    }
    // Below the window: down until it is in, then the row.
    {
        FakeBag f;
        BagDriver d;
        d.Open(10, f.g.items[10].id);
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::Menu && f.g.item == 27);
        CHECK(Count(f, BagStep::PressDown) > 0 && Count(f, BagStep::PressUp) == 0);
        CHECK(f.log.back().step == BagStep::TapRow);
        // Then back up to the first item: presses up.
        BagDriver d2;
        f.Apply(BagCmd{ BagStep::PressB, 0 });
        for (int i = 0; i < 5; i++) f.Frame();
        f.log.clear();
        d2.Open(0, 17);
        CHECK(Run(d2, f));
        CHECK(f.g.item == 17 && Count(f, BagStep::PressUp) > 0 && Count(f, BagStep::PressDown) == 0);
    }
    // The list changed under the native cursor: refuse, don't open another item.
    {
        FakeBag f;
        BagDriver d;
        d.Open(2, 999);
        CHECK(!Run(d, f, false));
        CHECK(f.commands == 0 && d.Failure().find("isn't where it was") != std::string::npos);
    }
    // Use: menu entry 0 of 4 is position 4; the pick taps twice (cursor, then
    // choose) for slot 4, once for the slot under the cursor; messages: A.
    {
        FakeBag f;
        BagDriver d;
        d.Open(0, 17);
        CHECK(Run(d, f));
        d.Choose(0);
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 4 }));
        CHECK(f.g.mode == BagMode::Pick);
        d.Pick(3);
        CHECK(Run(d, f));
        CHECK(Count(f, BagStep::TapParty) == 2 && f.lastPicked == 3);
        CHECK(f.g.mode == BagMode::Message);
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::Pick && Count(f, BagStep::PressA) == 1);
        // Slot under the cursor (still 3): one tap.
        d.Pick(3);
        CHECK(Run(d, f));
        CHECK(Count(f, BagStep::TapParty) == 3);
        d.Advance();
        CHECK(Run(d, f));
        d.Cancel();     // B back to the list
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::List && f.log.back().step == BagStep::PressB);
    }
    // Give over a held item: message, Yes / No, message, list.
    {
        FakeBag f;
        BagDriver d;
        d.Tab(2);
        CHECK(Run(d, f));
        d.Open(1, 78);
        CHECK(Run(d, f));
        CHECK(f.g.commands.size() == 4);
        d.Choose(1);    // Give: 3rd from the bottom
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 3 }));
        d.Pick(3);      // holds Quick Claw
        CHECK(Run(d, f));
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.YesNo());
        d.Answer(true);
        CHECK(Run(d, f));
        CHECK(f.log.back().step == BagStep::TapYes && f.held[3] == 78);
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::List);
    }
    // Cancel from the item menu taps its last entry (Cancel); Close taps back.
    {
        FakeBag f;
        BagDriver d;
        d.Open(1, 18);
        CHECK(Run(d, f));
        d.Cancel();
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 1 }) && f.g.mode == BagMode::List);
        d.Close();
        CHECK(Run(d, f));
        CHECK(f.closed && f.log.back().step == BagStep::TapBack);
    }
    // A game that ignores the taps: a few tries, then a failure, no endless tapping.
    {
        FakeBag f;
        f.ignoreTaps = true;
        BagDriver d;
        d.Tab(3);
        CHECK(!Run(d, f, false));
        CHECK(f.commands == 3 && !d.Failure().empty());
        d.Open(0, 17);
        f.commands = 0;
        CHECK(!Run(d, f, false, 5000));
        CHECK(f.commands > 0 && f.commands <= (int)BagDriver::MaxCommands);
    }
    // Not where the goal starts (pick asked for in the list): fails at once.
    {
        FakeBag f;
        BagDriver d;
        d.Pick(2);
        CHECK(!Run(d, f, false));
        CHECK(f.commands == 0);
    }
    // The Bag closes under a goal: fails.
    {
        FakeBag f;
        BagDriver d;
        d.Open(0, 17);
        f.closed = true;
        CHECK(!Run(d, f, false));
        CHECK(d.Failure().find("gone") != std::string::npos);
    }
    // A TM: Use, pick, messages, then the game leaves the Bag for its
    // move-forget screen. The goal that was running then is done, not
    // failed; back in the Bag, the message and the list follow.
    {
        FakeBag f;
        BagDriver d;
        d.Tab(3);
        CHECK(Run(d, f));
        d.Open(0, 328);
        CHECK(Run(d, f));
        CHECK(f.g.commands.size() == 3 && f.g.commands[0] == BagCommand::Use);
        d.Choose(0);
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 3 }) && f.g.mode == BagMode::Pick);
        d.Pick(0);
        CHECK(Run(d, f));
        d.Advance();
        CHECK(Run(d, f));
        CHECK(!f.g.Valid() && f.away > 0 && d.Failure().empty());
        while (f.away > 0)
            f.Frame();
        CHECK(f.g.mode == BagMode::Message);
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::List && f.g.items.size() == 1);   // a TM isn't used up
    }
    // The Bag gone before any command of a goal is still a failure.
    {
        FakeBag f;
        BagDriver d;
        d.Advance();
        f.closed = true;
        CHECK(!Run(d, f, false));
        CHECK(d.Failure().find("gone") != std::string::npos);
    }
    // Move to Free Space: 2nd from the bottom, a message, the list.
    {
        FakeBag f;
        BagDriver d;
        d.Tab(2);
        CHECK(Run(d, f));
        d.Open(0, 217);
        CHECK(Run(d, f));
        CHECK(f.g.commands.size() == 3);
        d.Choose(1);
        CHECK(Run(d, f));
        CHECK(f.log.back() == (BagCmd{ BagStep::TapMenu, 2 }));
        d.Advance();
        CHECK(Run(d, f));
        CHECK(f.g.mode == BagMode::List && f.g.items.size() == 1 && f.pockets[6].size() == 1);
    }
    // Settling: the driver doesn't act on a screen that keeps changing.
    {
        BagGame g;
        g.mode = BagMode::List;
        g.tab = 1;
        g.scroll = 0;
        g.items.push_back(BagItem{ 17, 1, false });
        g.visible = 1;
        BagDriver d;
        d.Tab(2);
        std::vector<std::string> log;
        for (u32 i = 0; i < 50; i++)
        {
            g.cursorRow = (int)(i & 1);
            CHECK(d.Update(g, false, log).step == BagStep::None);
        }
        CHECK(d.Busy());
        // While an action runs, nothing either.
        g.cursorRow = 0;
        for (u32 i = 0; i < 50; i++)
            CHECK(d.Update(g, true, log).step == BagStep::None);
        BagCmd cmd;
        for (u32 i = 0; i < 20 && cmd.step == BagStep::None; i++)
            cmd = d.Update(g, false, log);
        CHECK(cmd == (BagCmd{ BagStep::TapTab, 2 }));
    }
}

static void TestSignature(void)
{
    BagGame a;
    a.mode = BagMode::Menu;
    a.commands = { BagCommand::Use, BagCommand::Cancel };
    BagGame b = a;
    CHECK(a.Signature() == b.Signature());
    b.commands = { BagCommand::Yes, BagCommand::No };
    CHECK(a.Signature() != b.Signature());
    CHECK(b.YesNo() && !a.YesNo());
    b = a;
    b.partyCursor = 2;
    CHECK(a.Signature() != b.Signature());
    BagGame c;
    c.scroll = 3;
    c.cursorRow = 2;
    CHECK(c.Index() == 5);
    c.scroll = -1;
    CHECK(c.Index() == -1);
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void TestDraw(const char *preview)
{
    const int bpp = 4;
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
    const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);

    BagView view;
    view.mode = BagMode::Menu;
    view.tab = 1;
    const char *names[] = { "Potion", "Paralyze Heal", "Awakening", "Antidote", "Burn Heal", "Ice Heal",
                            "Revive", "Super Potion", "Fresh Water" };
    for (int i = 0; i < 9; i++)
    {
        view.names.push_back(names[i]);
        view.items.push_back(BagItem{ (u16)(17 + i), (u16)(9 - i), i == 4 });
    }
    view.cursor = 8;
    view.top = 1;
    view.commands = { BagCommand::Use, BagCommand::Give, BagCommand::FreeSpace, BagCommand::Cancel };
    view.menuTitle = "Fresh Water";
    const char *mons[] = { "Torracat", "Zorua", "Pikipek", "Pichu", "Ledyba", "Buneary" };
    for (int i = 0; i < 6; i++)
    {
        BagMon m;
        m.name = mons[i];
        m.level = 12 + i;
        m.maxHp = 30 + i;
        m.hp = i == 3 ? 7 : m.maxHp;
        m.held = i == 3 ? "Quick Claw" : "";
        view.party.push_back(m);
    }

    view.description = "A spray-type medicine for treating wounds. It can be used to restore 20 HP to an injured Pokémon.";
    // Synthetic pocket icons: a coloured square per pocket, lighter when not selected.
    BagAssets assets;
    for (int t = 0; t < BagTabCount; t++)
        for (int s = 0; s < 2; s++)
        {
            UI::Image &image = assets.pockets[t][s];
            image.w = image.h = 20;
            image.rgba.resize(20 * 20 * 4);
            for (int p = 0; p < 20 * 20; p++)
            {
                image.rgba[p * 4 + 0] = (u8)(40 * t);
                image.rgba[p * 4 + 1] = s ? 220 : 120;
                image.rgba[p * 4 + 2] = 200;
                image.rgba[p * 4 + 3] = 255;
            }
        }

    // Every mode draws the whole area (the Bag covers the top screen), with
    // and without the game's icons.
    for (const BagAssets *a : { (const BagAssets *)nullptr, (const BagAssets *)&assets })
        for (BagMode mode : { BagMode::List, BagMode::Pick, BagMode::Message, BagMode::Busy, BagMode::Menu })
        {
            canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));
            view.mode = mode;
            view.assets = a;
            view.partyCursor = mode == BagMode::Pick ? 3 : -1;
            view.pickQuestion = mode == BagMode::Pick ? BagPickQuestion(17) : "";
            DrawBag(canvas, view);
            CHECK(HiResScreenSmoke::Run(BagArea, BagRadius, [&](UI::Canvas &hi) { DrawBag(hi, view); }));
            for (int y = 0; y < UI::ScreenH; y += 7)
                for (int x = 0; x < UI::ScreenW; x += 7)
                {
                    const UI::Color c = At(canvas, x, y);
                    if (c.r == 10 && c.g == 200 && c.b == 10)
                    {
                        std::printf("    mode %d: %d,%d not drawn\n", (int)mode, x, y);
                        s_failures++;
                        y = UI::ScreenH;
                        break;
                    }
                }
        }
    // The open pocket's game icon is drawn (the selected variant).
    view.mode = BagMode::List;
    DrawBag(canvas, view);
    bool icon = false;
    for (int y = 200; y < UI::ScreenH && !icon; y++)
        for (int x = 0; x < UI::ScreenW && !icon; x++)
        {
            const UI::Color c = At(canvas, x, y);
            icon = c.r == 40 && c.g == 120 && c.b == 200;
        }
    CHECK(icon);
    // Empty pocket, and nothing at all.
    BagView empty;
    empty.mode = BagMode::List;
    empty.tab = 6;
    DrawBag(canvas, empty);
    DrawBag(canvas, BagView());
    // The preview: the menu over the list, the game's icons.
    view.mode = BagMode::Menu;
    view.menuTitle = "What do you want to do with the Fresh Water?";
    view.cursor = 8;
    DrawBag(canvas, view);

    if (preview != nullptr)
    {
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
        std::printf("bag_test: preview written to %s\n", preview);
    }
}

int main(int argc, char **argv)
{
    TestTables();
    TestItems();
    TestArchive();
    TestCursor();
    TestDriver();
    TestSignature();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("bag_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("bag_test: all passed\n");
    return 0;
}
