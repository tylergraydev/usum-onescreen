// Host tests for the simple native screens (W5-3): Options, Trainer Passport
// and Battle Video (Sources/Screens/Options.cpp, TrainerPassport.cpp,
// BattleVideo.cpp, NativeScreen.cpp). Build and run: make -C tests. With a
// directory argument, also writes a preview of each screen there (PPM).
//
// The bottom screen is faked from the colours measured in the game
// (docs/NATIVE-SCREENS.md): each test paints the sample points the screen
// asks for.

#include "Screens/BattleVideo.hpp"
#include "Screens/Options.hpp"
#include "Screens/TrainerPassport.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace OneScreen;

static int s_failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } } while (0)

static const Sample Strip = { 205, 253, 255 };     // Options value strip
static const Sample Grey = { 118, 118, 118 };      // an arrow off the cursor row
static const Sample Blue = { 0, 119, 238 };        // an arrow on the cursor row
static const Sample Red = { 238, 0, 0 };           // the game's cursor
static const Sample RowBlue = { 1, 169, 246 };     // the label pill, left of the strip
static const Sample DarkBox = { 37, 30, 23 };      // message box
static const Sample Pill = { 214, 255, 255 };      // Yes / No, Open the Vs. Recorder
static const Sample Paper = { 241, 241, 224 };     // passport page
static const Sample Ink = { 60, 70, 40 };          // a stamp's strokes
static const Sample Water = { 13, 155, 246 };      // Battle Video background

// A fake bottom screen: every sample point gets `base` unless painted.
struct FakeScreen
{
    std::vector<SamplePoint> points;
    std::vector<Sample> samples;

    FakeScreen(const std::vector<SamplePoint> &p, Sample base) : points(p), samples(p.size(), base) {}

    void Paint(int x0, int x1, int y0, int y1, Sample c)
    {
        for (size_t i = 0; i < points.size(); i++)
            if (points[i].x >= x0 && points[i].x <= x1 && points[i].y >= y0 && points[i].y <= y1)
                samples[i] = c;
    }
};

// The Options list with the cursor on `row` and these values.
static FakeScreen OptionsList(int row, const int values[OptionRows])
{
    FakeScreen s(OptionsSamplePoints(), Strip);
    for (int r = 0; r < OptionRows; r++)
    {
        const int y = 19 + 32 * r;
        s.Paint(0, 12, y, y, r == row ? Red : RowBlue);
        const int count = OptionRowAt(r).count;
        const Sample arrow = r == row ? Blue : Grey;
        if (values[r] > 0)
            s.Paint(160, 180, y, y, arrow);
        if (values[r] < count - 1)
            s.Paint(296, 316, y, y, arrow);
    }
    return s;
}

// The "make these changes?" question with the cursor on Yes (0) or No (1).
static FakeScreen OptionsQuestion(int choice)
{
    const int values[OptionRows] = { 0, 0, 0, 0, 0 };
    FakeScreen s = OptionsList(4, values);
    s.Paint(0, 319, 20, 20, DarkBox);
    s.Paint(240, 260, 107, 107, Pill);
    s.Paint(240, 260, 135, 135, Pill);
    s.Paint(200, 220, choice == 0 ? 108 : 136, choice == 0 ? 108 : 136, Red);
    return s;
}

static void Feed(NativeScreen &screen, const FakeScreen &s, int times, std::vector<std::string> &log)
{
    for (int i = 0; i < times; i++)
        screen.Sampled(s.samples.data(), log);
}

static bool Logged(const std::vector<std::string> &log, const std::string &text)
{
    for (const std::string &l : log)
        if (l.find(text) != std::string::npos)
            return true;
    return false;
}

// Runs `def` against a world that stays in `scene` (or switches to
// `after` once the keys were pressed) and returns the keys it pressed.
static u32 RunAction(const ActionDef &def, const char *scene, const char *after, bool &ok)
{
    ActionRunner runner;
    ActionWorld world;
    world.scene = scene;
    std::vector<std::string> log;
    u32 keys = 0;
    ok = runner.Start(def, world, log);
    for (int frame = 0; frame < 400 && runner.Running(); frame++)
    {
        const ActionOutput out = runner.Update(world, log);
        keys |= out.keys;
        if (keys != 0 && out.keys == 0 && after != nullptr)
            world.scene = after;
    }
    ok = ok && !runner.Running() && !log.empty() && log.back().find(" ok ") != std::string::npos;
    return keys;
}

// --- Options ---

static void TestOptionsSaveWord(void)
{
    int v[OptionRows];
    // This save as it booted, and after all five were changed and confirmed.
    OptionsFromSaveWord(0x0002A026, v);
    CHECK(v[0] == 2 && v[1] == 1 && v[2] == 0 && v[3] == 1 && v[4] == 1);
    CHECK(DescribeOptions(v) == "Text Speed Fast, Battle Effects Off, Battle Style Switch, Button Mode L=A, Party/Box Automatic");
    OptionsFromSaveWord(0x00020028, v);
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 1 && v[3] == 0 && v[4] == 0);
    // A value the row doesn't have.
    OptionsFromSaveWord(3u | (2u << 13), v);
    CHECK(v[0] == -1 && v[3] == -1);
}

static void TestOptionsArrows(void)
{
    CHECK(OptionValueFromArrows(0, false, true) == 0);     // Slow
    CHECK(OptionValueFromArrows(0, true, true) == 1);      // Normal
    CHECK(OptionValueFromArrows(0, true, false) == 2);     // Fast
    CHECK(OptionValueFromArrows(1, false, true) == 0);     // On
    CHECK(OptionValueFromArrows(1, true, false) == 1);     // Off
    CHECK(OptionValueFromArrows(1, true, true) == -1);
    CHECK(OptionValueFromArrows(2, false, false) == -1);
}

static void TestOptionsDecode(void)
{
    const int values[OptionRows] = { 1, 0, 1, 1, 0 };
    for (int row = 0; row < OptionRows; row++)
    {
        const OptionsShown shown = DecodeOptions(OptionsList(row, values).samples.data());
        CHECK(shown.valid && !shown.dialog && shown.row == row);
        for (int i = 0; i < OptionRows; i++)
            CHECK(shown.values[i] == values[i]);
    }

    // No cursor, or two: not the list.
    FakeScreen none = OptionsList(0, values);
    none.Paint(0, 12, 19, 19, RowBlue);
    CHECK(!DecodeOptions(none.samples.data()).valid);
    FakeScreen two = OptionsList(0, values);
    two.Paint(0, 12, 51, 51, Red);
    CHECK(!DecodeOptions(two.samples.data()).valid);
    // A row with neither arrow (sliding in): not valid.
    FakeScreen bare = OptionsList(0, values);
    bare.Paint(160, 316, 83, 83, Strip);
    CHECK(!DecodeOptions(bare.samples.data()).valid);

    for (int choice = 0; choice < 2; choice++)
    {
        const OptionsShown q = DecodeOptions(OptionsQuestion(choice).samples.data());
        CHECK(q.valid && q.dialog && q.choice == choice);
    }
    // The question without its cursor (it slides in) isn't valid.
    FakeScreen noCursor = OptionsQuestion(0);
    noCursor.Paint(200, 220, 108, 108, Pill);
    CHECK(!DecodeOptions(noCursor.samples.data()).valid);
}

static void TestOptionsScreen(void)
{
    OptionsScreen screen;
    std::vector<std::string> log;
    screen.SetSaveWord(0x0002A026, true);
    screen.Begin(log);
    CHECK(Logged(log, "options: save word 0002A026: Text Speed Fast"));

    const int start[OptionRows] = { 2, 1, 0, 1, 1 };
    const FakeScreen list = OptionsList(0, start);
    Feed(screen, list, OptionsScreen::StableSamples - 1, log);
    CHECK(!screen.Ready());
    Feed(screen, list, 1, log);
    CHECK(screen.Ready());
    CHECK(Logged(log, "options: shown, game row 0 (Text Speed)"));
    CHECK(Logged(log, "options: screen matches the save word"));

    // At an end nothing is pressed; inside, the D-pad key is.
    CHECK(screen.Key(ScreenKey::Right, log) == nullptr);   // Fast is the last
    const ActionDef *left = screen.Key(ScreenKey::Left, log);
    CHECK(left != nullptr && std::string(left->name) == "options-left");
    bool ok = false;
    CHECK(left != nullptr && RunAction(*left, "Config", nullptr, ok) == ScreenKey::Left && ok);
    const ActionDef *down = screen.Key(ScreenKey::Down, log);
    CHECK(down != nullptr && RunAction(*down, "Config", nullptr, ok) == ScreenKey::Down && ok);
    const ActionDef *up = screen.Key(ScreenKey::Up, log);
    CHECK(up != nullptr && std::string(up->name) == "options-up");
    CHECK(screen.Key(ScreenKey::L, log) == nullptr);
    CHECK(screen.Key(ScreenKey::Start, log) == nullptr);

    // The game moved: the screen follows, a row and a value at a time.
    const int changed[OptionRows] = { 1, 1, 0, 1, 1 };
    log.clear();
    Feed(screen, OptionsList(1, changed), OptionsScreen::StableSamples, log);
    CHECK(screen.Shown().row == 1 && screen.Shown().values[0] == 1);
    CHECK(Logged(log, "options: row Battle Effects"));
    CHECK(Logged(log, "options: Text Speed Normal"));
    // A reading that doesn't decode keeps the last one.
    FakeScreen blank(OptionsSamplePoints(), Water);
    Feed(screen, blank, 10, log);
    CHECK(screen.Ready() && screen.Shown().row == 1);

    // A confirms: the action presses A and waits for the field.
    log.clear();
    const ActionDef *confirm = screen.Key(ScreenKey::A, log);
    CHECK(confirm != nullptr && std::string(confirm->name) == "options-confirm");
    CHECK(Logged(log, "options: confirm: Text Speed Normal"));
    CHECK(confirm != nullptr && RunAction(*confirm, "Config", SceneField, ok) == ScreenKey::A && ok);
    CHECK(screen.Key(ScreenKey::Down, log) == nullptr);    // nothing more while confirming
    // If the list is still there after the action, it didn't take.
    screen.ActionDone(log);
    Feed(screen, OptionsList(1, changed), OptionsScreen::StableSamples, log);
    CHECK(!screen.Confirming());
    CHECK(Logged(log, "options: confirm didn't leave the screen"));
    CHECK(confirm != nullptr && RunAction(*confirm, "Config", nullptr, ok) == ScreenKey::A && !ok);

    // B (the game's) with changes: the question. Values stay the list's.
    log.clear();
    Feed(screen, OptionsQuestion(0), OptionsScreen::StableSamples, log);
    CHECK(screen.Shown().dialog && screen.Shown().choice == 0 && screen.Shown().values[0] == 1);
    CHECK(Logged(log, "options: question (make these changes?), cursor Yes"));
    CHECK(screen.Key(ScreenKey::Left, log) == nullptr);
    const ActionDef *toNo = screen.Key(ScreenKey::Down, log);
    CHECK(toNo != nullptr && std::string(toNo->name) == "options-down");
    Feed(screen, OptionsQuestion(1), OptionsScreen::StableSamples, log);
    CHECK(screen.Shown().choice == 1);
    CHECK(Logged(log, "options: question cursor No"));
    const ActionDef *answer = screen.Key(ScreenKey::A, log);
    CHECK(answer != nullptr && std::string(answer->name) == "options-answer");
    CHECK(Logged(log, "options: answer No"));

    // Every action presses keys only: nothing is tapped.
    for (const ActionDef *def : { left, down, up, confirm, answer })
        CHECK(def != nullptr && CheckAction(*def).empty());

    // A new visit starts from scratch.
    log.clear();
    screen.Begin(log);
    CHECK(!screen.Ready() && !screen.Confirming());
}

// --- Trainer Passport ---

static FakeScreen PassportPage(bool earned)
{
    FakeScreen s(PassportSamplePoints(), Paper);
    if (earned)
        for (size_t i = 2; i < s.points.size(); i += 3)
            s.samples[i] = Ink;
    return s;
}

static void TestPassportFormat(void)
{
    CHECK(FormatTrainerId(0x2C422D1C) == "534428");     // this save's card
    CHECK(FormatTrainerId(5) == "000005");
    CHECK(FormatMoney(27068) == "$27,068");
    CHECK(FormatMoney(0) == "$0");
    CHECK(FormatMoney(999) == "$999");
    CHECK(FormatMoney(9999999) == "$9,999,999");
    CHECK(FormatPlayTime(14, 44) == "14:44");
    CHECK(FormatPlayTime(0, 5) == "0:05");
    CHECK(FormatPlayTime(999, 59) == "999:59");
}

static void TestPassportScreen(void)
{
    CHECK(PassportSamplePoints().size() <= MaxScreenSamples);
    CHECK(DecodePassport(PassportPage(true).samples.data()).earned);
    CHECK(DecodePassport(PassportPage(true).samples.data()).valid);
    CHECK(!DecodePassport(PassportPage(false).samples.data()).earned);
    CHECK(DecodePassport(PassportPage(false).samples.data()).valid);
    // Not the paper (sliding in, or another scene's screen): not valid.
    FakeScreen water(PassportSamplePoints(), Water);
    CHECK(!DecodePassport(water.samples.data()).valid);

    PassportScreen screen;
    std::vector<std::string> log;
    screen.Begin(log);
    PassportData data;
    data.found = true;
    data.name = "Tyler";
    data.idValid = data.moneyValid = data.timeValid = true;
    data.id = 0x2C422D1C;
    data.money = 27068;
    data.hours = 14;
    data.minutes = 44;
    data.stampName[0] = "Official Pokémon\nTrainer";
    data.stampText[0] = "You took the first step as a Pokémon Trainer!\n";
    screen.SetData(data, log);
    CHECK(Logged(log, "passport: Tyler, ID No. 534428, money $27,068, play time 14:44"));

    screen.SetPage(0);
    Feed(screen, PassportPage(true), PassportScreen::StableSamples, log);
    CHECK(screen.Ready());
    CHECK(Logged(log, "passport: page 1/15, earned: Official Pokémon Trainer"));

    // No wrap: nothing left of page 1, nothing right of page 15.
    CHECK(screen.Key(ScreenKey::Left, log) == nullptr);
    CHECK(screen.Key(ScreenKey::L, log) == nullptr);
    CHECK(screen.Key(ScreenKey::A, log) == nullptr);       // A (Portrait Photo) is kept from the game
    const ActionDef *next = screen.Key(ScreenKey::R, log);
    CHECK(next != nullptr && std::string(next->name) == "passport-next");
    bool ok = false;
    CHECK(next != nullptr && RunAction(*next, "TrainerPass", nullptr, ok) == ScreenKey::Right && ok);
    CHECK(screen.Key(ScreenKey::Right, log) == nullptr);   // one turn at a time
    screen.ActionDone(log);
    log.clear();
    screen.SetPage(1);
    Feed(screen, PassportPage(false), PassportScreen::StableSamples, log);
    CHECK(Logged(log, "passport: page 2/15, not earned"));
    const ActionDef *prev = screen.Key(ScreenKey::Left, log);
    CHECK(prev != nullptr && std::string(prev->name) == "passport-prev");
    CHECK(prev != nullptr && CheckAction(*prev).empty());
    screen.ActionDone(log);
    screen.SetPage(PassportPages - 1);
    CHECK(screen.Key(ScreenKey::Right, log) == nullptr);
    screen.SetPage(-1);                                     // page not readable: no turns
    CHECK(screen.Key(ScreenKey::Left, log) == nullptr);
}

// --- Battle Video ---

static FakeScreen VideoIntro(void)
{
    FakeScreen s(VideoSamplePoints(), Water);
    s.Paint(0, 319, 20, 20, DarkBox);
    s.Paint(20, 300, 110, 114, Pill);
    return s;
}

static void TestVideo(void)
{
    CHECK(VideoSamplePoints().size() <= MaxScreenSamples);
    FakeScreen empty(VideoSamplePoints(), Water);
    CHECK(DecodeVideo(VideoIntro().samples.data()) == VideoState::Intro);
    CHECK(DecodeVideo(empty.samples.data()) == VideoState::Empty);
    FakeScreen list = empty;
    list.Paint(30, 290, 50, 70, Pill);                      // something light: not checked
    CHECK(DecodeVideo(list.samples.data()) == VideoState::Other);

    VideoScreen screen;
    std::vector<std::string> log;
    screen.Begin(log);
    CHECK(!screen.Ready());
    Feed(screen, VideoIntro(), VideoScreen::StableSamples, log);
    CHECK(screen.State() == VideoState::Intro && screen.Ready());
    CHECK(screen.CaptureKeys() & ScreenKey::A);
    CHECK(screen.Key(ScreenKey::Down, log) == nullptr);
    const ActionDef *open = screen.Key(ScreenKey::A, log);
    CHECK(open != nullptr && std::string(open->name) == "video-open" && CheckAction(*open).empty());
    CHECK(screen.Key(ScreenKey::A, log) == nullptr);       // once
    screen.ActionDone(log);

    // The fade to the recorder reads as "other" for a while: not taken.
    Feed(screen, list, VideoScreen::StableOther - 1, log);
    CHECK(screen.State() == VideoState::Intro);
    Feed(screen, empty, VideoScreen::StableSamples, log);
    CHECK(screen.State() == VideoState::Empty);
    CHECK(Logged(log, "video: recorder empty (no Battle Videos)"));
    CHECK(screen.Key(ScreenKey::A, log) == nullptr);
    // Held long enough, "other" counts, and the keys go back to the game.
    Feed(screen, list, VideoScreen::StableOther, log);
    CHECK(screen.State() == VideoState::Other && screen.CaptureKeys() == 0);
}

// --- Shared ---

static void TestPressAction(void)
{
    const ActionDef &a = ScreenPressAction("t-up", "Config", ScreenKey::Up, 4);
    const ActionDef &b = ScreenPressAction("t-up", "Config", ScreenKey::Up, 4);
    const ActionDef &c = ScreenPressAction("t-down", "Config", ScreenKey::Down, 4);
    CHECK(&a == &b && &a != &c);
    CHECK(std::string(a.name) == "t-up" && a.steps.size() == 2);
    bool ok = false;
    CHECK(RunAction(a, "Config", nullptr, ok) == ScreenKey::Up && ok);
    CHECK(RunAction(a, "FieldRo", nullptr, ok) == 0 && !ok);   // refused outside its scene
}

static UI::Color At(const UI::Canvas &canvas, int x, int y)
{
    UI::Color c = {};
    canvas.GetPixel(x, y, c);
    return c;
}

static void Draw(NativeScreen &screen, const char *dir, const char *file)
{
    const int bpp = 4;
    std::vector<u8> pixels(UI::ScreenW * UI::ScreenH * bpp, 0);
    const UI::Surface surface = { pixels.data(), bpp, UI::ScreenW * bpp, UI::PixelFormat::RGBA8,
                                  UI::ScreenW, UI::ScreenH };
    UI::Canvas canvas(surface);
    canvas.FillRect(UI::Rect{ 0, 0, UI::ScreenW, UI::ScreenH }, UI::Rgb(10, 200, 10));
    canvas.SetClip(screen.Area());
    screen.Draw(canvas);
    canvas.ResetClip();

    // Inside the area: drawn; outside: untouched (the Layer copies only it).
    const UI::Rect a = screen.Area();
    CHECK(At(canvas, a.x + a.w / 2, a.y + 40).g != 200);
    CHECK(At(canvas, a.x - 1, a.y + 50).g == 200);
    if (a.Bottom() < UI::ScreenH)
        CHECK(At(canvas, a.x + 50, a.Bottom() + 1).g == 200);

    if (dir == nullptr)
        return;
    const std::string path = std::string(dir) + "/" + file;
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

static void TestDraw(const char *dir)
{
    std::vector<std::string> log;
    OptionsScreen options;
    options.Begin(log);
    const int values[OptionRows] = { 2, 1, 0, 1, 1 };
    Feed(options, OptionsList(2, values), OptionsScreen::StableSamples, log);
    Draw(options, dir, "native-options.ppm");
    Feed(options, OptionsQuestion(1), OptionsScreen::StableSamples, log);
    Draw(options, dir, "native-options-question.ppm");

    PassportScreen passport;
    passport.Begin(log);
    PassportData data;
    data.found = data.idValid = data.moneyValid = data.timeValid = true;
    data.name = "Tyler";
    data.id = 0x2C422D1C;
    data.money = 27068;
    data.hours = 14;
    data.minutes = 44;
    data.stampName[0] = "Official Pokémon\nTrainer";
    data.stampText[0] = "You took the first step as a Pokémon Trainer!\n";
    passport.SetData(data, log);
    passport.SetPage(0);
    Feed(passport, PassportPage(true), PassportScreen::StableSamples, log);
    Draw(passport, dir, "native-passport.ppm");

    VideoScreen video;
    video.Begin(log);
    Feed(video, VideoIntro(), VideoScreen::StableSamples, log);
    Draw(video, dir, "native-video.ppm");
    if (dir != nullptr)
        std::printf("native_screens_test: previews written to %s\n", dir);
}

int main(int argc, char **argv)
{
    TestOptionsSaveWord();
    TestOptionsArrows();
    TestOptionsDecode();
    TestOptionsScreen();
    TestPassportFormat();
    TestPassportScreen();
    TestVideo();
    TestPressAction();
    TestDraw(argc > 1 ? argv[1] : nullptr);
    if (s_failures != 0)
    {
        std::printf("native_screens_test: %d failure(s)\n", s_failures);
        return 1;
    }
    std::printf("native_screens_test: all passed\n");
    return 0;
}
