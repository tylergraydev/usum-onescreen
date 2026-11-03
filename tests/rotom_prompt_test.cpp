// Host tests for the Rotom Dex's field prompts (Sources/Screens/RotomPromptView.cpp).
// Build and run: make -C tests. Arguments: an optional preview PPM path, then
// an optional path to the game's own a/0/3/2 (English text GARC, from the
// user's dump in analysis/private/; skipped when missing, as on CI).

#include "RomfsText.hpp"
#include "Screens/RotomPromptView.hpp"
#include "UI/Text.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

// A line as the message file stores it: `plain` encrypted with line `line`'s key.
static std::vector<u16> Encrypt(const std::vector<u16> &plain, u32 line)
{
    std::vector<u16> out;
    u16 key = (u16)(0x2983 * (line + 3));
    for (u16 c : plain)
    {
        out.push_back(c ^ key);
        key = (u16)((key << 3) | (key >> 13));
    }
    return out;
}

static void Add(std::vector<u16> &units, const char *ascii)
{
    for (const char *p = ascii; *p; p++)
        units.push_back((u8)*p);
}

static void TestTable(void)
{
    const u32 table = 0x8074DC;
    CHECK(RotomEntryIndex(table, 0) == -1);
    CHECK(RotomEntryIndex(table, table) == 0);
    CHECK(RotomEntryIndex(table, table + 20 * 16) == 20);       // 0x80761C: "Heya, ..." in the runs
    CHECK(RotomEntryIndex(table, 0x80761C) == 20);
    CHECK(RotomEntryIndex(table, table + 0x99 * 16) == 0x99);
    CHECK(RotomEntryIndex(table, table + 0x9A * 16) == -1);
    CHECK(RotomEntryIndex(table, table + 8) == -1);
    CHECK(RotomEntryIndex(table, table - 16) == -1);

    // Entry 20 as the FieldMenu CRO holds it.
    const u16 heya[8] = { 1031, 1043, 1044, 1035, 1039, 7710, 0xFFFF, 0xFFFF };
    RotomEntry e;
    CHECK(RotomEntryFromWords(heya, e));
    CHECK(e.question == 1031 && e.answer[0] == 1043 && e.answer[1] == 1044);
    CHECK(e.reply[0] == 1035 && e.reply[1] == 1039);
    // The save reminder has no replies.
    const u16 save[8] = { 1419, 1421, 1422, 0xFFFF, 0xFFFF, 0, 0xFFFF, 0xFFFF };
    CHECK(RotomEntryFromWords(save, e));
    CHECK(e.reply[0] == RotomNoLine && e.reply[1] == RotomNoLine);
    const u16 broken[8] = { 1419, 0xFFFF, 1422, 0, 0, 0, 0, 0 };
    CHECK(!RotomEntryFromWords(broken, e));

    CHECK(RotoLotoStepFrom(0) == RotoLotoStep::None);
    CHECK(RotoLotoStepFrom(3) == RotoLotoStep::Intro);
    CHECK(RotoLotoStepFrom(5) == RotoLotoStep::Spinning);
    CHECK(RotoLotoStepFrom(6) == RotoLotoStep::Result);
    CHECK(RotoLotoStepFrom(7) == RotoLotoStep::Result);
    CHECK(RotoLotoStepFrom(0x00010008) == RotoLotoStep::Fading);     // as the u32 read showed it
    CHECK(RotoLotoStepFrom(9) == RotoLotoStep::Power);
    CHECK(RotoLotoStepFrom(1) == RotoLotoStep::None);
}

static void TestLineText(void)
{
    // "Heya, [VAR 0189 0001], how’re you doing? Zzrt!"
    std::vector<u16> plain;
    Add(plain, "Heya, ");
    plain.insert(plain.end(), { 0x0010, 2, 0x0189, 0x0001 });
    Add(plain, ", how");
    plain.push_back(0x2019);
    Add(plain, "re you doing? Zzrt!");
    plain.push_back(0);
    std::vector<u16> enc = Encrypt(plain, 1033);
    CHECK(RotomLineText(enc.data(), (u32)enc.size(), 1033, "Tyler") == "Heya, Tyler, how\xE2\x80\x99re you doing? Zzrt!");
    // The wrong line's key gives something else.
    CHECK(RotomLineText(enc.data(), (u32)enc.size(), 1032, "Tyler") != "Heya, Tyler, how\xE2\x80\x99re you doing? Zzrt!");

    // Page and line breaks, the wait tag dropped, private glyphs, [VAR 0100].
    plain.clear();
    Add(plain, "Hi, ");
    plain.insert(plain.end(), { 0x0010, 2, 0x0100, 0x0000 });
    Add(plain, "!");
    plain.insert(plain.end(), { 0x0010, 2, 0xBE02, 0x0028, 0x0010, 1, 0xBE01 });
    Add(plain, "Wait");
    plain.push_back(0xE08D);
    plain.push_back('\n');
    Add(plain, "Done");
    plain.insert(plain.end(), { 0x0010, 2, 0xBE02, 0x000A, 0x0010, 1, 0xBE00 });
    Add(plain, "end");
    plain.insert(plain.end(), { 0x0010, 2, 0xBDFF, 0x03AC });     // a word Rotom fills: dropped
    plain.push_back(0);
    enc = Encrypt(plain, 7);
    CHECK(RotomLineText(enc.data(), (u32)enc.size(), 7, "Ash") == "Hi, Ash!\nWait\xE2\x80\xA6\nDone\nend");

    // A tag cut off at the end of the data doesn't read past it.
    plain = { 'A', 0x0010, 5, 0x0100 };
    enc = Encrypt(plain, 3);
    CHECK(RotomLineText(enc.data(), (u32)enc.size(), 3, "X") == "AX");
    CHECK(RotomLineText(enc.data(), 0, 3, "X").empty());
}

static void TestWrap(void)
{
    UI::TextStyle style;
    const int width = 200;
    const std::vector<std::string> lines =
        RotomWrap("So, how are you doing on your adventure, kid? I want to know!\nSecond", width);
    CHECK(lines.size() >= 3);
    for (const std::string &l : lines)
        CHECK(UI::MeasureText(l, style) <= width);
    CHECK(lines.back() == "Second");
    CHECK(lines[0].front() == 'S' && lines[0].back() != ' ');
    CHECK(RotomWrap("", width).empty());
    CHECK(RotomWrap("one\n\n", width).size() == 1);
    // A word longer than the line is cut where it must be.
    const std::vector<std::string> cut = RotomWrap("Zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", 100);
    CHECK(cut.size() >= 2);
    for (const std::string &l : cut)
        CHECK(UI::MeasureText(l, style) <= 100);

    RotomView view;
    view.mode = RotomMode::Choice;
    RotomSetText(view, "a\nb\nc\nd\ne\nf");
    CHECK(view.lines == RotomMaxLines);
    CHECK(std::strstr(view.text[RotomMaxLines - 1], "...") != nullptr);
}

static void TestKeys(void)
{
    const u32 talk = RotomKeyUp;
    RotomView v;
    CHECK(RotomKeys(v, RotomKeyA | RotomKeyUp, talk) == RotomKey::None);

    v.mode = RotomMode::Ready;
    CHECK(RotomKeys(v, RotomKeyUp, talk) == RotomKey::Talk);
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::None);
    CHECK(RotomKeys(v, RotomKeyUp, 0) == RotomKey::None);       // no talk key

    v.mode = RotomMode::Choice;
    CHECK(RotomKeys(v, RotomKeyUp, talk) == RotomKey::Up);
    CHECK(RotomKeys(v, RotomKeyDown, talk) == RotomKey::Down);
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::Answer);
    CHECK(RotomKeys(v, RotomKeyA | RotomKeyDown, talk) == RotomKey::Answer);
    v.busy = true;
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::None);

    v = RotomView();
    v.mode = RotomMode::Loto;
    v.loto = RotoLotoStep::Spinning;
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::Next);
    CHECK(RotomKeys(v, RotomKeyUp, talk) == RotomKey::None);
    v.loto = RotoLotoStep::Fading;
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::None);
    v.loto = RotoLotoStep::Power;
    v.busy = true;
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::None);

    v = RotomView();
    v.mode = RotomMode::Reply;
    CHECK(RotomKeys(v, RotomKeyA, talk) == RotomKey::None);
}

static RotomView Choice(void)
{
    RotomView v;
    v.mode = RotomMode::Choice;
    std::snprintf(v.key, sizeof(v.key), "Up");
    RotomSetText(v, "Heya, Tyler, how\xE2\x80\x99re you doing? Zzrt!");
    std::snprintf(v.answer[0], sizeof(v.answer[0]), "I\xE2\x80\x99m doing fine!");
    std::snprintf(v.answer[1], sizeof(v.answer[1]), "A bit tired...");
    return v;
}

static void TestDescribe(void)
{
    RotomView v = Choice();
    v.cursor = 1;
    // Wrapped lines are joined with " / " (the PC's stand-in font wraps this one).
    std::string question;
    for (int i = 0; i < v.lines; i++)
        question += (i ? " / " : "") + std::string(v.text[i]);
    CHECK(v.lines >= 1 && question.find("Zzrt!") != std::string::npos);
    CHECK(DescribeRotomView(v) == "choice \"" + question + "\" | 1 \"I\xE2\x80\x99m doing fine!\" | 2 \"A bit tired...\" | cursor 2");
    v.busy = true;
    CHECK(DescribeRotomView(v).find("(answering)") != std::string::npos);
    RotomView r;
    r.mode = RotomMode::Ready;
    std::snprintf(r.key, sizeof(r.key), "Up");
    CHECK(DescribeRotomView(r) == "ready (talk key Up)");
    r.mode = RotomMode::Loto;
    r.loto = RotoLotoStep::Intro;
    RotomSetText(r, "Roto Loto time!");
    CHECK(DescribeRotomView(r) == "roto loto intro \"Roto Loto time!\"");
    CHECK(DescribeRotomView(RotomView()) == "none");

    RotomView a = Choice(), b = Choice();
    CHECK(a == b);
    b.cursor = 1;
    CHECK(a != b);
    b = Choice();
    b.answer[1][0] = 'X';
    CHECK(a != b);
}

static void TestLayout(void)
{
    CHECK(RotomArea(RotomView()).Empty());
    RotomView badge;
    badge.mode = RotomMode::Ready;
    const UI::Rect b = RotomArea(badge);
    CHECK(b.x == RotomBadge.x && b.Right() <= UI::ScreenW && b.y >= 0);

    for (int lines = 1; lines <= RotomMaxLines; lines++)
    {
        RotomView v = Choice();
        std::string text;
        for (int i = 0; i < lines; i++)
            text += std::string(i ? "\n" : "") + "line";
        RotomSetText(v, text);
        CHECK(v.lines == lines);
        const UI::Rect area = RotomArea(v);
        CHECK(area.x >= 0 && area.Right() <= UI::ScreenW && area.y >= 0 && area.Bottom() <= UI::ScreenH);
        CHECK(area.h <= RotomBoxMaxH);
        CHECK(area.w * area.h <= RotomBoxW * RotomBoxMaxH);     // fits the layer's buffer
        for (int i = 0; i < 2; i++)
        {
            const UI::Rect r = RotomAnswerRect(v, i);
            CHECK(r.x > area.x && r.Right() < area.Right() && r.y > area.y && r.Bottom() < area.Bottom());
        }
        CHECK(RotomAnswerRect(v, 1).y >= RotomAnswerRect(v, 0).Bottom());
    }
    // A reply box is shorter than the question box (no answers).
    RotomView reply = Choice();
    reply.mode = RotomMode::Reply;
    CHECK(RotomArea(reply).h < RotomArea(Choice()).h);
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void WritePpm(const UI::Canvas &canvas, const char *path)
{
    FILE *f = std::fopen(path, "wb");
    if (f == nullptr)
    {
        std::printf("can't write %s\n", path);
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
    std::printf("rotom_prompt_test: preview written to %s\n", path);
}

static void TestDraw(const char *preview)
{
    const int bpp = 4;
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
    const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));

    RotomView v = Choice();
    v.cursor = 1;
    const UI::Rect area = RotomArea(v);
    DrawRotomPrompt(canvas, v);
    // Nothing above the box is touched; the box is drawn.
    const UI::Color above = At(canvas, area.x + 50, area.y - 2);
    CHECK(above.r == 10 && above.g == 200 && above.b == 10);
    const UI::Color inside = At(canvas, area.x + 6, area.y + area.h / 2);
    CHECK(inside.g != 200);
    // The cursor's yellow frame is around answer 2, not answer 1.
    const UI::Rect a1 = RotomAnswerRect(v, 0), a2 = RotomAnswerRect(v, 1);
    const UI::Color frame2 = At(canvas, a2.x + 40, a2.y - 1);
    const UI::Color frame1 = At(canvas, a1.x + 40, a1.y - 1);
    CHECK(frame2.r > 200 && frame2.g > 200);
    CHECK(!(frame1.r > 200 && frame1.g > 200));

    RotomView badge;
    badge.mode = RotomMode::Ready;
    DrawRotomPrompt(canvas, badge);
    const UI::Color edge = At(canvas, RotomBadge.x + RotomBadge.w / 2, RotomBadge.y + 1);
    CHECK(edge.r > 180 && edge.g < 120);

    if (preview != nullptr)
        WritePpm(canvas, preview);
}

// The user's own text GARC (not in git): Rotom's lines from message file 89
// come out as Rotom showed them on the bottom screen (w62 tap2, chat2).
static void TestRealLines(const char *garcPath)
{
    std::ifstream in(garcPath, std::ios::binary);
    if (!in)
    {
        std::printf("rotom_prompt_test: %s not found, real Rotom line check skipped\n", garcPath);
        return;
    }
    const std::vector<u8> garc((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const RomfsRead read = [&garc](uint64_t offset, void *dst, u32 size) {
        if (offset + size > garc.size())
            return false;
        std::memcpy(dst, garc.data() + offset, size);
        return true;
    };
    uint64_t at = 0;
    u32 size = 0;
    std::string error;
    CHECK(GarcEntry(read, 0, 89, at, size, error));
    if (!error.empty() || at + size > garc.size())
    {
        std::printf("    %s\n", error.c_str());
        return;
    }
    const u8 *file = garc.data() + at;
    const auto u16At = [file](u32 o) { return (u32)(file[o] | file[o + 1] << 8); };
    const auto u32At = [&](u32 o) { return u16At(o) | u16At(o + 2) << 16; };
    CHECK(u16At(2) == 1488);
    const u32 section = u32At(0x0C);
    const auto line = [&](u32 n) {
        const u32 offset = u32At(section + 4 + n * 8);
        const u32 length = u16At(section + 4 + n * 8 + 4);
        std::vector<u16> enc(length);
        for (u32 i = 0; i < length; i++)
            enc[i] = (u16)u16At(section + offset + i * 2);
        return RotomLineText(enc.data(), length, n, "Tyler");
    };
    CHECK(line(1033) == "Heya, Tyler, how\xE2\x80\x99re you doing? Zzrt!");
    CHECK(line(1043) == "I\xE2\x80\x99m doing fine!");
    CHECK(line(1044) == "A bit tired...");
    CHECK(line(1001) == "I\xE2\x80\x99m having fun!");
    CHECK(line(1470) == "Sorry to keep you waiting! It\xE2\x80\x99s Roto Loto time!");
    CHECK(line(1469) == "Hey, look at that! That\xE2\x80\x99zzz an amazing power!");
    CHECK(line(1473) == "Roto Prize Money is a power that increases the\nmoney you win after each battle!");
    // A multi-page line: the pages become lines.
    CHECK(line(1023).find("[VAR") == std::string::npos);
    CHECK(line(1023).find('\n') != std::string::npos);
    std::printf("rotom_prompt_test: Rotom's lines read from %s\n", garcPath);
}

int main(int argc, char **argv)
{
    TestTable();
    TestLineText();
    TestRealLines(argc > 2 ? argv[2] : "../analysis/private/romfs-base/a/0/3/2");
    TestWrap();
    TestKeys();
    TestDescribe();
    TestLayout();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("rotom_prompt_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("rotom_prompt_test: all passed\n");
    return 0;
}
