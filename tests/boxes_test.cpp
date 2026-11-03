// Host tests for the PC boxes: decoding (Sources/BoxData.cpp) and the native
// screen's actions, keys and drawing (Sources/Screens/BoxesGrid.cpp). Build
// and run: make -C tests. With a path argument, also writes a preview of the
// screen there as a PPM image.

#include "BoxData.hpp"
#include "Screens/BoxesGrid.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static void Put16(u8 *p, u32 offset, u16 v)
{
    p[offset] = (u8)v;
    p[offset + 1] = (u8)(v >> 8);
}

static void Put32(u8 *p, u32 offset, u32 v)
{
    Put16(p, offset, (u16)v);
    Put16(p, offset + 2, (u16)(v >> 16));
}

// A box Pokémon as the save stores it (encrypted core, no party stats).
static void MakeBoxMon(u8 *core, u32 ec, u16 species, const char *nick, bool egg)
{
    std::memset(core, 0, Pk7CoreSize);
    Put32(core, 0x00, ec);
    for (u32 i = 0x08; i < Pk7CoreSize; i++)
        core[i] = (u8)(i * 5);
    Put16(core, 0x08, species);
    for (u32 i = 0; i < 13; i++)
        Put16(core, 0x40 + i * 2, i < std::strlen(nick) ? (u16)(u8)nick[i] : 0);
    Put32(core, 0x74, egg ? 0x40000000 : 0x3FFFFFFF);
    EncodePk7(core, nullptr);
}

static void TestData(void)
{
    // The save's layout: 32 trays of 30 cores, slot by slot.
    CHECK(BoxSlotOffset(0, 0) == 0);
    CHECK(BoxSlotOffset(0, 1) == 0xE8);
    CHECK(BoxSlotOffset(1, 0) == 30 * 0xE8);
    CHECK(BoxSlotOffset(31, 29) + Pk7CoreSize == 0x36600);  // the save's box block size
    CHECK(BoxColumns * BoxRows == BoxSlots);

    CHECK(BoxCursorKindOf(0) == BoxCursorKind::Slot);
    CHECK(BoxCursorKindOf(29) == BoxCursorKind::Slot);
    CHECK(BoxCursorKindOf(30) == BoxCursorKind::Party);
    CHECK(BoxCursorKindOf(35) == BoxCursorKind::Party);
    CHECK(BoxCursorKindOf(36) == BoxCursorKind::Name);
    CHECK(BoxCursorKindOf(37) == BoxCursorKind::Other);
    CHECK(BoxCursorKindOf(38) == BoxCursorKind::BoxList);
    CHECK(BoxCursorKindOf(0xFFFFFFFF) == BoxCursorKind::Other);

    CHECK(BoxModeFromListener(BoxCroIdle) == BoxMode::Idle);
    CHECK(BoxModeFromListener(BoxCroModal) == BoxMode::Modal);
    CHECK(BoxModeFromListener(BoxCroCarrying) == BoxMode::Carrying);
    CHECK(BoxModeFromListener(BoxCroLowBgDraw) == BoxMode::Unknown);
    CHECK(BoxModeFromListener(0) == BoxMode::Unknown);
    CHECK(std::string(BoxModeName(BoxMode::Carrying)) == "carrying");

    // Box names: as stored, or "Box N" when empty.
    u16 name[BoxNameUnits] = {};
    CHECK(BoxNameText(name, 0) == "Box 1");
    CHECK(BoxNameText(name, 31) == "Box 32");
    const char *text = "Team Rocket!";
    for (u32 i = 0; text[i]; i++)
        name[i] = (u16)text[i];
    CHECK(BoxNameText(name, 3) == "Team Rocket!");
    // A full name has no terminator inside the 17 units: cut at 17.
    for (u32 i = 0; i < BoxNameUnits; i++)
        name[i] = 'A' + i;
    CHECK(BoxNameText(name, 0).size() == BoxNameUnits);

    CHECK(BoxUnlockedCount(8) == 8);
    CHECK(BoxUnlockedCount(0) == 1);
    CHECK(BoxUnlockedCount(32) == 32);
    CHECK(BoxUnlockedCount(200) == 32);
}

static void TestLabels(void)
{
    u8 core[Pk7CoreSize];
    MakeBoxMon(core, 0xB3D8352C, 427, "Buneary", false);
    const Pk7 mon = DecodePk7(core, nullptr);
    CHECK(mon.form == Pk7Form::Encrypted);
    CHECK(mon.species == 427);
    CHECK(BoxMonLabel(mon) == "Buneary");

    MakeBoxMon(core, 0x12345678, 172, "Sparky", false);
    CHECK(BoxMonLabel(DecodePk7(core, nullptr)) == "Sparky");

    MakeBoxMon(core, 0x0BADF00D, 25, "Pichu", true);
    CHECK(BoxMonLabel(DecodePk7(core, nullptr)) == "Egg");

    // An empty slot is all zero: it decodes (checksum 0) with species 0.
    std::memset(core, 0, sizeof(core));
    const Pk7 empty = DecodePk7(core, nullptr);
    CHECK(empty.species == 0);
    CHECK(BoxMonLabel(empty).empty());

    // A slot read mid-update doesn't decode: shown as empty, not garbage.
    MakeBoxMon(core, 0xCAFEBABE, 10, "Caterpie", false);
    core[0x20] ^= 0xFF;
    CHECK(BoxMonLabel(DecodePk7(core, nullptr)).empty());
}

static void TestButtons(void)
{
    // Every slot and party button exists, was verified by a tap, and the
    // cell grid on the bottom screen is 6 x 5 with no overlaps.
    for (u32 i = 0; i < BoxSlots + BoxPartySlots; i++)
    {
        const char *name = BoxesSlotButton(i);
        CHECK(name != nullptr);
        const LayoutButton *b = name ? FindLayoutButton(name) : nullptr;
        CHECK(b != nullptr);
        if (b == nullptr)
            continue;
        CHECK(b->verified == LayoutVerified::Tap);
        CHECK(b->tapX >= b->x0 && b->tapX <= b->x1 && b->tapY >= b->y0 && b->tapY <= b->y1);
        for (u32 j = 0; j < i; j++)
        {
            const LayoutButton *o = FindLayoutButton(BoxesSlotButton(j));
            CHECK(!(b->x0 <= o->x1 && o->x0 <= b->x1 && b->y0 <= o->y1 && o->y0 <= b->y1));
        }
        if (i < BoxSlots && i % BoxColumns != 0)
        {
            const LayoutButton *left = FindLayoutButton(BoxesSlotButton(i - 1));
            CHECK(left->tapY == b->tapY && left->tapX < b->tapX);
        }
    }
    CHECK(std::string(BoxesSlotButton(0)) == "box.slot_0");
    CHECK(std::string(BoxesSlotButton(30)) == "box.party_0");
    CHECK(std::string(BoxesSlotButton(35)) == "box.party_5");
    CHECK(BoxesSlotButton(36) == nullptr);
    CHECK(BoxesSlotButton(38) == nullptr);

    // Release is never tapped by anything.
    const LayoutButton *release = FindLayoutButton("box_menu.release");
    CHECK(release != nullptr && release->verified == LayoutVerified::Measured);

    // Every action only taps verified buttons, and never Release.
    for (u32 i = 0; i < BoxSlots + BoxPartySlots; i++)
    {
        for (const ActionDef *def : { &BoxesPickAction(i), &BoxesDropAction(i) })
        {
            const std::string bad = CheckAction(*def);
            if (!bad.empty())
                std::printf("    %s: %s\n", def->name, bad.c_str());
            CHECK(bad.empty());
            for (const ActionStep &s : def->steps)
                CHECK(s.button == nullptr || std::string(s.button) != "box_menu.release");
        }
    }
    CHECK(CheckAction(BoxesTurnAction(false)).empty());
    CHECK(CheckAction(BoxesTurnAction(true)).empty());
    CHECK(std::string(BoxesPickAction(7).name) == "box-pick-7");
    CHECK(std::string(BoxesDropAction(31).name) == "box-drop-31");
}

static void TestKeys(void)
{
    // A: pick up from an occupied slot, drop anywhere while holding.
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Idle, 3, true) == BoxesCommand::Pick);
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Idle, 33, true) == BoxesCommand::Pick);
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Idle, 3, false) == BoxesCommand::None);
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Carrying, 3, false) == BoxesCommand::Drop);
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Carrying, 3, true) == BoxesCommand::Drop);
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Carrying, 35, false) == BoxesCommand::Drop);
    // Not on the box name or the box list button.
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Idle, BoxCursorName, true) == BoxesCommand::None);
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Carrying, BoxCursorBoxList, false) == BoxesCommand::None);
    // L / R turn the box, also while holding; Y is details.
    CHECK(BoxesCommandFor(BoxesKeyL, BoxMode::Idle, 0, false) == BoxesCommand::PrevBox);
    CHECK(BoxesCommandFor(BoxesKeyR, BoxMode::Carrying, 0, false) == BoxesCommand::NextBox);
    CHECK(BoxesCommandFor(BoxesKeyY, BoxMode::Idle, BoxCursorName, false) == BoxesCommand::Details);
    // Nothing while the game's menu or a message is up, or for other keys.
    CHECK(BoxesCommandFor(BoxesKeyA, BoxMode::Modal, 3, true) == BoxesCommand::None);
    CHECK(BoxesCommandFor(BoxesKeyL, BoxMode::Modal, 3, true) == BoxesCommand::None);
    CHECK(BoxesCommandFor(BoxesKeyY, BoxMode::Unknown, 3, true) == BoxesCommand::None);
    CHECK(BoxesCommandFor(BoxesKeyX, BoxMode::Idle, 3, true) == BoxesCommand::None);
}

// A small fake of the game's Box screen: taps on slots open the slot menu
// (occupied) or move the cursor; Move picks up; a tap while holding drops
// (swapping); the arrows turn the box.
struct FakeBox
{
    ActionWorld world;
    bool occupied[BoxSlots + BoxPartySlots] = {};
    int tray = 0;
    int menuFor = -1, held = -1;
    bool refuseDrop = false;    // the game says no (e.g. the last party Pokémon)
    bool deaf = false;          // ignores every tap
    bool wasTouching = false;
    u32 frame = 0;
    std::vector<std::string> tapped;
    struct Change { u32 at; u32 flags; };
    std::vector<Change> pending;

    FakeBox(void)
    {
        world.scene = SceneBox;
        world.flags = WorldBoxIdle;
    }

    void Later(u32 delay, u32 flags) { pending.push_back({ frame + delay, flags }); }

    const LayoutButton *Hit(u16 x, u16 y)
    {
        const std::string screen = world.flags & WorldBoxModal ? "box_menu" : "box";
        for (const LayoutButton &b : LayoutButtons)
        {
            const std::string name = b.name;
            if (name.substr(0, name.find('.')) == screen && x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1)
                return &b;
        }
        return nullptr;
    }

    static int SlotOf(const std::string &name)
    {
        if (name.compare(0, 9, "box.slot_") == 0)
            return std::atoi(name.c_str() + 9);
        if (name.compare(0, 10, "box.party_") == 0)
            return (int)BoxSlots + std::atoi(name.c_str() + 10);
        return -1;
    }

    void Tap(const std::string &name)
    {
        const int slot = SlotOf(name);
        if (world.flags & WorldBoxIdle)
        {
            if (slot >= 0 && occupied[slot])
            {
                menuFor = slot;
                Later(8, WorldBoxModal);
            }
            else if (name == "box.prev_box" || name == "box.next_box")
                tray += name == "box.next_box" ? 1 : -1;
        }
        else if (world.flags & WorldBoxModal)
        {
            if (name == "box_menu.move" && menuFor >= 0)
            {
                held = menuFor;
                Later(6, WorldBoxCarrying);
            }
            else if (name == "box_menu.cancel")
                Later(6, WorldBoxIdle);
        }
        else if (world.flags & WorldBoxCarrying)
        {
            if (slot >= 0 && refuseDrop)
                Later(6, WorldBoxModal);
            else if (slot >= 0)
            {
                const bool other = occupied[slot] && slot != held;
                occupied[held] = other;
                occupied[slot] = true;
                held = -1;
                Later(6, WorldBoxIdle);
            }
            else if (name == "box.prev_box" || name == "box.next_box")
                tray += name == "box.next_box" ? 1 : -1;
        }
    }

    void Feed(const ActionOutput &out)
    {
        if (out.touching && !wasTouching && !deaf)
            if (const LayoutButton *b = Hit(out.x, out.y))
            {
                tapped.push_back(b->name);
                Tap(b->name);
            }
        wasTouching = out.touching;
        frame++;
        for (size_t i = 0; i < pending.size();)
            if (pending[i].at <= frame)
            {
                world.flags = pending[i].flags;
                pending.erase(pending.begin() + i);
            }
            else
                i++;
    }
};

static bool Run(FakeBox &game, const ActionDef &def, std::vector<std::string> &log)
{
    ActionRunner runner;
    if (!runner.Start(def, game.world, log))
        return false;
    for (u32 i = 0; i < 2000 && runner.Running(); i++)
        game.Feed(runner.Update(game.world, log));
    CHECK(!runner.Running());
    return !log.empty() && log.back().find(" ok in ") != std::string::npos;
}

static void PrintLog(const std::vector<std::string> &log)
{
    for (const std::string &line : log)
        std::printf("    %s\n", line.c_str());
}

static void TestActions(void)
{
    // Box slot 4 -> party slot 5 (empty), then back to box slot 0 (swap
    // with what is there).
    {
        FakeBox game;
        game.occupied[0] = game.occupied[4] = true;
        std::vector<std::string> log;
        bool ok = Run(game, BoxesPickAction(4), log);
        CHECK(ok);
        CHECK(game.tapped.size() == 2 && game.tapped[0] == "box.slot_4" && game.tapped[1] == "box_menu.move");
        CHECK(game.world.flags == WorldBoxCarrying && game.held == 4);

        ok = ok && Run(game, BoxesDropAction(35), log);
        CHECK(ok);
        CHECK(game.tapped.back() == "box.party_5");
        CHECK(game.world.flags == WorldBoxIdle && game.occupied[35] && !game.occupied[4]);

        ok = ok && Run(game, BoxesPickAction(35), log) && Run(game, BoxesDropAction(0), log);
        CHECK(ok);
        CHECK(game.occupied[0] && game.occupied[35]);   // swapped
        if (!ok)
            PrintLog(log);
    }

    // Pick and drop are refused in the wrong mode, with nothing tapped.
    {
        FakeBox game;
        game.occupied[2] = true;
        std::vector<std::string> log;
        CHECK(!Run(game, BoxesDropAction(2), log));
        game.world.flags = WorldBoxModal;
        CHECK(!Run(game, BoxesPickAction(2), log));
        game.world.scene = SceneField;
        game.world.flags = 0;
        CHECK(!Run(game, BoxesPickAction(2), log));
        CHECK(game.tapped.empty());
    }

    // A game that ignores taps: the slot is tapped twice, Move never.
    {
        FakeBox game;
        game.occupied[1] = true;
        game.deaf = true;
        ActionRunner runner;
        std::vector<std::string> log;
        CHECK(runner.Start(BoxesPickAction(1), game.world, log));
        u32 taps = 0;
        bool was = false;
        const LayoutButton *slot = FindLayoutButton("box.slot_1");
        for (u32 i = 0; i < 2000 && runner.Running(); i++)
        {
            const ActionOutput out = runner.Update(game.world, log);
            if (out.touching && !was)
            {
                taps++;
                CHECK(out.x == slot->tapX && out.y == slot->tapY);
            }
            was = out.touching;
            game.Feed(out);
        }
        CHECK(taps == 2);
        CHECK(!log.empty() && log.back().find("FAILED") != std::string::npos);
    }

    // The game refuses a drop (shows a message): one tap, then failed,
    // without tapping into the message.
    {
        FakeBox game;
        game.occupied[30] = true;
        std::vector<std::string> log;
        CHECK(Run(game, BoxesPickAction(30), log));
        game.refuseDrop = true;
        const size_t before = game.tapped.size();
        CHECK(!Run(game, BoxesDropAction(3), log));
        CHECK(game.tapped.size() == before + 1);
        CHECK(game.world.flags == WorldBoxModal);
    }

    // The arrows, idle and holding.
    {
        FakeBox game;
        game.occupied[5] = true;
        std::vector<std::string> log;
        CHECK(Run(game, BoxesTurnAction(true), log));
        CHECK(game.tray == 1);
        CHECK(Run(game, BoxesPickAction(5), log));
        CHECK(Run(game, BoxesTurnAction(false), log));
        CHECK(game.tray == 0 && game.world.flags == WorldBoxCarrying);
        CHECK(game.tapped.front() == "box.next_box");
    }
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

// The theme's orange highlight frame (UI::Theme::Highlight).
static bool IsRing(UI::Color c)
{
    return c.r > 200 && c.g > 60 && c.g < 130 && c.b < 60;
}

static void TestDraw(const char *preview)
{
    // Cells: inside the area, not overlapping, the party below the box.
    for (u32 i = 0; i <= BoxCursorName; i++)
    {
        if (i > BoxSlots + BoxPartySlots - 1 && i != BoxCursorName)
            continue;
        const UI::Rect r = BoxesCellRect(i);
        CHECK(r.x >= BoxesArea.x + 4 && r.Right() <= BoxesArea.Right() - 4);
        CHECK(r.y >= BoxesArea.y + 4 && r.Bottom() <= BoxesArea.Bottom() - 4);
        for (u32 j = 0; j < i; j++)
        {
            if (j > BoxSlots + BoxPartySlots - 1)
                continue;
            const UI::Rect o = BoxesCellRect(j);
            CHECK(!(r.x < o.Right() && o.x < r.Right() && r.y < o.Bottom() && o.y < r.Bottom()));
        }
    }
    CHECK(BoxesCellRect(BoxSlots).y > BoxesCellRect(BoxSlots - 1).Bottom());

    // Footer text per mode.
    BoxesView view;
    CHECK(BoxesFooter(view).find("A Move") != std::string::npos);
    view.mode = BoxMode::Carrying;
    view.held = "Ledyba";
    CHECK(BoxesFooter(view).find("Moving Ledyba") != std::string::npos);
    view.busy = "Turning the box...";
    CHECK(BoxesFooter(view) == "Turning the box...");
    view.busy.clear();
    view.mode = BoxMode::Modal;
    CHECK(BoxesFooter(view).find("bottom screen") != std::string::npos);

    const int bpp = 4;
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
    const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));

    // Box 1 of the test save (docs/RE6-boxes.md), holding Ledyba from slot 0.
    static const char *const box[] = { "", "Torracat", "Pikipek", "Yungoos", "Yungoos", "Caterpie", "Meowth",
                                       "Grubbin", "Magnemite", "Wingull", "Mime Jr.", "Abra", "Furfrou", "Meowth",
                                       "Ekans", "Drowzee", "Furfrou", "Murkrow", "Zubat", "Gastly", "Makuhita",
                                       "Buneary" };
    view = BoxesView();
    view.boxName = "Box 1";
    view.unlocked = 8;
    for (u32 i = 0; i < sizeof(box) / sizeof(box[0]); i++)
        view.slots[i] = box[i];
    static const char *const party[] = { "Zorua", "Pikipek", "Pichu", "Ledyba", "Buneary" };
    for (u32 i = 0; i < 5; i++)
        view.party[i] = party[i];
    view.cursor = 8;
    DrawBoxes(canvas, view);

    // Nothing outside the area is touched (the Layer copies only it).
    const UI::Color outside = At(canvas, BoxesArea.x - 1, BoxesArea.y + 50);
    CHECK(outside.r == 10 && outside.g == 200 && outside.b == 10);
    // The cursor cell has the orange frame just outside it; the others not.
    const UI::Rect sel = BoxesCellRect(view.cursor);
    CHECK(IsRing(At(canvas, sel.x + sel.w / 2, sel.y - 1)));
    const UI::Rect other = BoxesCellRect(9);
    CHECK(!IsRing(At(canvas, other.x + other.w / 2, other.y - 1)));
    // Empty cells are outlines (the grass wallpaper inside), occupied ones
    // a darker plate; party rows are the light list rows.
    const UI::Rect empty = BoxesCellRect(25);
    const UI::Color inside = At(canvas, empty.x + 3, empty.y + empty.h / 2);
    CHECK(inside.g > 120 && inside.r < 100 && inside.b < 100);
    const UI::Rect full = BoxesCellRect(1);
    const UI::Color plate = At(canvas, full.x + 3, full.y + full.h / 2);
    CHECK(plate.g < inside.g - 30 && plate.g > plate.r);
    const UI::Rect row = BoxesCellRect(BoxSlots);
    const UI::Color light = At(canvas, row.x + row.w / 2, row.y + 6);
    CHECK(light.r > 200 && light.g > 200 && light.b > 200);

    // Holding: the cursor frame turns red.
    view.mode = BoxMode::Carrying;
    view.held = "Ledyba";
    view.heldTray = 0;
    view.heldIndex = 33;
    view.cursor = 26;
    DrawBoxes(canvas, view);
    const UI::Rect hold = BoxesCellRect(26);
    const UI::Color red = At(canvas, hold.x + hold.w / 2, hold.y - 1);
    CHECK(red.r > 200 && red.g < 40 && red.b < 40);

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
        std::printf("boxes_test: preview written to %s\n", preview);
    }
}

int main(int argc, char **argv)
{
    TestData();
    TestLabels();
    TestButtons();
    TestKeys();
    TestActions();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("boxes_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("boxes_test: all passed\n");
    return 0;
}
