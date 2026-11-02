#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Game.hpp"
#include <cstdio>
#include <vector>

#include "AutoPanel.hpp"
#include "Battle.hpp"
#include "Buttons.hpp"
#include "Cursor.hpp"
#include "InputCapture.hpp"
#include "Panel.hpp"
#include "Party.hpp"
#include "Scene.hpp"
#include "Settings.hpp"
#include "Shot.hpp"
#include "UI/Device.hpp"

namespace CTRPluginFramework
{
    static const OneScreen::GameInfo *s_game = nullptr;

    static const char *LogPath = "/usum-onescreen.log";
    static const char *SettingsPath = "/usum-onescreen.ini";

    // Runs before the game starts. Keep it short: nothing else is set up yet.
    void PatchProcess(FwkSettings &settings)
    {
        s_game = OneScreen::DetectGame();
    }

    // Runs when the game is closing.
    void OnProcessExit(void)
    {
    }

    // usum-onescreen.log at the SD card root is rewritten each boot, so a test
    // run can be checked without watching the screen.
    static void WriteLog(const std::string &line, bool truncate)
    {
        File file;
        int mode = File::WRITE | File::CREATE | File::SYNC | (truncate ? File::TRUNCATE : File::APPEND);
        if (File::Open(file, LogPath, mode) == File::SUCCESS)
        {
            file.WriteLine(line);
            file.Close();
        }
    }

    // Reads /usum-onescreen.ini (writing the commented default if it is
    // missing) and makes it the settings in use. Problems go to the log; a
    // file that can't be read leaves the defaults.
    static void LoadSettings(void)
    {
        std::string text;
        if (File::Exists(SettingsPath) != 1)
        {
            text = OneScreen::DefaultSettingsText();
            File file;
            if (File::Open(file, SettingsPath, File::WRITE | File::CREATE | File::TRUNCATE | File::SYNC) == File::SUCCESS)
            {
                file.Write(text.data(), text.size());
                file.Close();
                WriteLog(Utils::Format("settings: wrote defaults to %s", SettingsPath), false);
            }
            else
                WriteLog(Utils::Format("settings: can't write %s, using defaults", SettingsPath), false);
        }
        else
        {
            File file;
            if (File::Open(file, SettingsPath, File::READ) == File::SUCCESS)
            {
                text.resize(file.GetSize());
                if (!text.empty())
                    file.Read(&text[0], text.size());
                file.Close();
            }
            else
                WriteLog(Utils::Format("settings: can't open %s, using defaults", SettingsPath), false);
        }

        std::vector<std::string> warnings;
        const OneScreen::Settings settings = OneScreen::ParseSettings(text, warnings);
        for (const std::string &w : warnings)
            WriteLog(Utils::Format("settings: %s %s", SettingsPath + 1, w.c_str()), false);
        if (settings.panelSize == OneScreen::PanelSize::TwoThirds)
            WriteLog("settings: size 2/3 is not drawn yet, using 1/2", false);
        OneScreen::SetSettings(settings);
        OneScreen::AutoPanelSetEnabled(settings.autoPanel);
        WriteLog("settings: " + OneScreen::DescribeSettings(settings), false);
    }

    // Test commands, one per line, in /usum-cmd.txt on the SD card. The file is
    // removed once read, and each command plus the resulting state is logged:
    //   move X Y | touch on | touch off | tap FRAMES | mode off|panel|full | status
    //   press KEY [FRAMES] | hold KEY | release KEY   (KEY: a b x y l r start select up down left right)
    //     (default 3 frames: menus start key repeat by 8, so one press moved two steps)
    //   stick X Y [FRAMES]   (circle pad, up is +Y, ~156 full tilt; default 30 frames)
    //   snap NAME   (dump global data to /usum-snap-NAME.bin)
    //   peek ADDR [COUNT]   (log COUNT words at hex ADDR, default 4)
    //   vtscan NAME   (global -> object -> vtable scan to /usum-vt-NAME.bin)
    //   scene   (log the current scene's module name)
    //   battle   (log the battle state, docs/RE1-battle.md; also logged on change)
    //   party | bag   (log the party / the bag pockets, docs/RE3-party-bag.md)
    //   memmap | dump ADDR SIZE NAME   (readable blocks; raw memory to /usum-dump-NAME.bin)
    //   auto on | auto off   (panel follows the scene, or stays as set)
    //   perf on | perf off | perf   (panel timing; perf logs and restarts the window)
    //   capture on|off [KEY|stick|touch ...]   (hide input from the game as a plugin
    //     menu would; on adds, off removes; no list = everything)
    static const char *CommandPath = "/usum-cmd.txt";

    // Global data + bss of USUM v1.2 (from the exheader): 0x669000 .. 0x6D5F44.
    static const u32 SnapStart = 0x00669000;
    static const u32 SnapEnd   = 0x006D6000;

    // Writes the game's global data to /usum-snap-NAME.bin for offline diffing.
    static void WriteSnapshot(const std::string &name)
    {
        File file;
        const std::string path = "/usum-snap-" + name + ".bin";
        if (File::Open(file, path, File::WRITE | File::CREATE | File::TRUNCATE) != File::SUCCESS)
            return;
        file.Write((const void *)SnapStart, SnapEnd - SnapStart);
        file.Close();
    }

    static bool IsHeapPointer(u32 v)
    {
        return (v >= 0x08000000 && v < 0x10000000) || (v >= 0x30000000 && v < 0x40000000);
    }

    static bool IsVtable(u32 v)
    {
        return v >= 0x005BC000 && v < 0x00669000 && (v & 3) == 0;
    }

    // For every global word that points into the heap, looks at the first 64
    // words of that object; for each of those that points to another heap
    // object whose first word is a rodata address (a vtable), records
    // (global, offset, vtable). Written to /usum-vt-NAME.bin as u32 triples.
    static void WriteVtableScan(const std::string &name)
    {
        std::vector<u32> records;
        for (u32 g = SnapStart; g < SnapEnd; g += 4)
        {
            const u32 p = *(const u32 *)g;
            if (!IsHeapPointer(p) || (p & 3) != 0)
                continue;
            if (!Process::CheckAddress(p, MEMPERM_READ) || !Process::CheckAddress(p + 0xFC, MEMPERM_READ))
                continue;
            for (u32 off = 0; off < 0x100; off += 4)
            {
                const u32 q = *(const u32 *)(p + off);
                if (!IsHeapPointer(q) || (q & 3) != 0)
                    continue;
                u32 vt = 0;
                if (!Process::Read32(q, vt) || !IsVtable(vt))
                    continue;
                records.push_back(g);
                records.push_back(off);
                records.push_back(vt);
            }
        }

        File file;
        const std::string path = "/usum-vt-" + name + ".bin";
        if (File::Open(file, path, File::WRITE | File::CREATE | File::TRUNCATE) != File::SUCCESS)
            return;
        file.Write(records.data(), records.size() * 4);
        file.Close();
        WriteLog(Utils::Format("vtscan %s: %u records", name.c_str(), records.size() / 3), false);
    }

    // "capture on|off [KEY|stick|touch ...]": parses the list into a request
    // word (InputCapture.hpp); false on an unknown name.
    static bool RunCapture(const std::string &args, bool on)
    {
        u32 mask = 0;
        size_t start = 0;
        while (start < args.size())
        {
            size_t end = args.find(' ', start);
            if (end == std::string::npos)
                end = args.size();
            const std::string name = args.substr(start, end - start);
            start = end + 1;
            if (name.empty())
                continue;
            const u32 key = name == "stick" ? OneScreen::CaptureStickBits
                          : name == "touch" ? OneScreen::CaptureTouchBit
                          : OneScreen::ButtonFromName(name);
            if (key == 0)
                return false;
            mask |= key;
        }
        if (mask == 0)
            mask = ~0u;

        const u32 current = OneScreen::InputCaptureRequest();
        const u32 next = on ? (current | mask) : (current & ~mask);
        OneScreen::InputCaptureBegin(next, (next & OneScreen::CaptureStickBits) != 0,
                                     (next & OneScreen::CaptureTouchBit) != 0);
        return true;
    }

    static void RunCommand(const std::string &line)
    {
        unsigned a = 0, b = 0;
        char word[16] = {};
        int fields = 0;

        if ((fields = std::sscanf(line.c_str(), "press %15s %u", word, &a)) >= 1
            && OneScreen::ButtonFromName(word) != 0)
            OneScreen::ButtonsPress(OneScreen::ButtonFromName(word), fields == 2 ? a : 3);
        else if (std::sscanf(line.c_str(), "hold %15s", word) == 1 && OneScreen::ButtonFromName(word) != 0)
            OneScreen::ButtonsPress(OneScreen::ButtonFromName(word), 0);
        else if (std::sscanf(line.c_str(), "release %15s", word) == 1 && OneScreen::ButtonFromName(word) != 0)
            OneScreen::ButtonsRelease(OneScreen::ButtonFromName(word));
        else if (std::sscanf(line.c_str(), "snap %15s", word) == 1)
            WriteSnapshot(word);
        else if (std::sscanf(line.c_str(), "vtscan %15s", word) == 1)
            WriteVtableScan(word);
        else if (line == "battle")
            WriteLog(OneScreen::DescribeBattle(OneScreen::ReadBattle(s_game->offsets.sceneManager)), false);
        else if (line == "party")
            WriteLog(OneScreen::DescribeParty(OneScreen::ReadParty(s_game->offsets.sceneManager)), false);
        else if (line == "bag")
            WriteLog(OneScreen::DescribeBag(OneScreen::ReadBag()), false);
        else if (line == "memmap")
            WriteLog(OneScreen::MemoryMap(), false);
        else if (std::sscanf(line.c_str(), "dump %x %x %15s", &a, &b, word) == 3)
            WriteLog(OneScreen::DumpMemory(a, b, word), false);
        else if (line == "scene")
            WriteLog("scene: " + OneScreen::SceneModule(s_game->offsets.sceneManager), false);
        else if (line == "uidemo on" || line == "uidemo off")
            OneScreen::UiDemoSetEnabled(line == "uidemo on");
        else if (line == "auto on" || line == "auto off")
            OneScreen::AutoPanelSetEnabled(line == "auto on");
        else if (line == "perf on" || line == "perf off")
            OneScreen::PanelPerfSetEnabled(line == "perf on");
        else if (line == "perf")
            WriteLog(OneScreen::PanelPerfReport(), false);
        else if (line.compare(0, 11, "capture on ") == 0 || line == "capture on"
                 || line.compare(0, 12, "capture off ") == 0 || line == "capture off")
        {
            const bool on = line.compare(0, 10, "capture on") == 0;
            if (!RunCapture(line.substr(on ? 10 : 11), on))
            {
                WriteLog("cmd: unknown key in '" + line + "'", false);
                return;
            }
        }
        else if ((fields = std::sscanf(line.c_str(), "peek %x %u", &a, &b)) >= 1)
        {
            // Logs COUNT words (default 4) at ADDR, if readable.
            const u32 count = fields == 2 && b > 0 && b <= 64 ? b : 4;
            std::string out = Utils::Format("peek %08X:", a);
            for (u32 i = 0; i < count; i++)
            {
                u32 value = 0;
                if (!Process::Read32(a + i * 4, value))
                {
                    out += " ????????";
                    break;
                }
                out += Utils::Format(" %08lX", value);
            }
            WriteLog(out, false);
        }
        else if (int sx = 0, sy = 0; (fields = std::sscanf(line.c_str(), "stick %d %d %u", &sx, &sy, &a)) >= 2)
            OneScreen::ButtonsStick(sx, sy, fields == 3 ? a : 30);
        else if (std::sscanf(line.c_str(), "move %u %u", &a, &b) == 2)
            OneScreen::CursorMoveTo(a, b);
        else if (line == "touch on")
            OneScreen::CursorSetTouching(true);
        else if (line == "touch off")
            OneScreen::CursorSetTouching(false);
        else if (std::sscanf(line.c_str(), "tap %u", &a) == 1)
            OneScreen::CursorTap(a);
        else if (std::sscanf(line.c_str(), "mode %15s", word) == 1)
        {
            const std::string mode = word;
            if (mode == "off")        OneScreen::SetPanelMode(OneScreen::PanelMode::Off);
            else if (mode == "panel") OneScreen::SetPanelMode(OneScreen::PanelMode::Panel);
            else if (mode == "full")  OneScreen::SetPanelMode(OneScreen::PanelMode::Full);
        }
        else if (line != "status")
        {
            WriteLog("cmd: unknown '" + line + "'", false);
            return;
        }

        WriteLog("cmd: " + line, false);
    }

    static void PollCommands(void)
    {
        static u32 frames = 0;

        // Checking the SD card costs a file-system call, so only look twice a second.
        if (++frames % 30 != 0 || File::Exists(CommandPath) != 1)
            return;

        File file;
        if (File::Open(file, CommandPath, File::READ) != File::SUCCESS)
            return;
        std::string text(file.GetSize(), '\0');
        file.Read(&text[0], text.size());
        file.Close();
        File::Remove(CommandPath);

        size_t start = 0;
        while (start < text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(start, end - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!line.empty())
                RunCommand(line);
            start = end + 1;
        }

        WriteLog(OneScreen::AutoPanelStatus(), false);
        WriteLog(OneScreen::PanelStatus(), false);
        WriteLog(OneScreen::CursorStatus(), false);
        WriteLog(OneScreen::ButtonsStatus(), false);
    }

    // Runs every frame while the menu is closed. The panel follows the scene
    // (AutoPanel); panel_key (ZR) cycles it by hand (off -> panel -> full)
    // until the next scene change. C-stick moves the touch cursor and ZL
    // touches (cursor_move, touch_key). Scene and panel changes go to the log.
    static void PanelEntry(MenuEntry *entry)
    {
        static u32 frames = 0;
        static std::string scene;

        const u32 panelKey = OneScreen::GetSettings().panelKey;
        if (panelKey != 0 && Controller::IsKeysPressed(panelKey))
        {
            OneScreen::CyclePanelMode();
            OneScreen::AutoPanelManualOverride();
            WriteLog(OneScreen::PanelStatus(), false);
        }

        OneScreen::CursorUpdate();
        OneScreen::ButtonsUpdate();

        if (++frames == 300)
        {
            WriteLog(OneScreen::PanelStatus(), false);
            WriteLog(OneScreen::CursorStatus(), false);
        }

        // Scene changes are checked four times a second.
        if (frames % 15 == 0)
        {
            const std::string now = OneScreen::SceneModule(s_game->offsets.sceneManager);
            if (now != scene)
            {
                scene = now;
                WriteLog("scene: " + (now.empty() ? std::string("(none)") : now), false);
            }

            static std::string battle;
            const std::string state = scene == "Battle"
                ? OneScreen::DescribeBattle(OneScreen::ReadBattle(s_game->offsets.sceneManager)) : "";
            if (state != battle)
            {
                battle = state;
                if (!state.empty())
                    WriteLog(state, false);
            }
        }

        const int fieldState = scene == "FieldRo" ? OneScreen::FieldState(s_game->offsets.sceneManager) : -1;
        if (OneScreen::AutoPanelUpdate(scene, OneScreen::ButtonsGameHeld(), fieldState))
            WriteLog(OneScreen::AutoPanelStatus() + " -> " + OneScreen::PanelStatus(), false);

        OneScreen::ShotPoll();
        PollCommands();
    }

    // Menu entries below are actions: A in the plugin menu (opened with
    // Select, CTRPF's default menu hotkey) runs them. Their names show the
    // current state and are refreshed each time the menu opens.
    static MenuEntry *s_autoEntry = nullptr;

    static void RefreshMenuNames(void)
    {
        if (s_autoEntry != nullptr)
            s_autoEntry->Name() = std::string("Auto panel: ") + (OneScreen::AutoPanelEnabled() ? "on" : "off");
    }

    static bool OnMenuOpening(void)
    {
        RefreshMenuNames();
        return true;
    }

    // Until the next boot or reload; the file's `auto` is not rewritten.
    static void ToggleAutoPanel(MenuEntry *entry)
    {
        OneScreen::AutoPanelSetEnabled(!OneScreen::AutoPanelEnabled());
        RefreshMenuNames();
        WriteLog(OneScreen::AutoPanelStatus() + " (menu)", false);
    }

    static void ShowSettings(MenuEntry *entry)
    {
        const std::string text = OneScreen::DescribeSettings(OneScreen::GetSettings())
                                 + "\nauto panel now: " + (OneScreen::AutoPanelEnabled() ? "on" : "off");
        MessageBox box("Settings (/usum-onescreen.ini)", text);
        box();
    }

    static void ReloadSettings(MenuEntry *entry)
    {
        LoadSettings();
        RefreshMenuNames();
        const std::string text = OneScreen::DescribeSettings(OneScreen::GetSettings())
                                 + "\nProblems, if any, are in usum-onescreen.log.";
        MessageBox box("Settings reloaded", text);
        box();
    }

    static void InitMenu(PluginMenu &menu)
    {
        MenuEntry *panel = new MenuEntry("Bottom screen on top (auto, ZR to override)", PanelEntry,
                                         "Shows the bottom screen on the top screen. ZR cycles: off, small panel, full view. "
                                         "C-stick moves the touch cursor, ZL touches. Keys are set in usum-onescreen.ini.");
        menu += panel;
        panel->Enable();

        s_autoEntry = new MenuEntry("Auto panel", nullptr, ToggleAutoPanel,
                                    "A: switch between following the scene and staying as set. "
                                    "Lasts until the next boot; set auto in usum-onescreen.ini to keep it.");
        menu += s_autoEntry;
        menu += new MenuEntry("View settings", nullptr, ShowSettings,
                              "A: show the settings in use, read from /usum-onescreen.ini at boot.");
        menu += new MenuEntry("Reload settings file", nullptr, ReloadSettings,
                              "A: read /usum-onescreen.ini again. Problems go to usum-onescreen.log.");
        RefreshMenuNames();
    }

    int main(void)
    {
        if (s_game == nullptr)
        {
            // Unsupported title or version: show what was detected so the
            // offset table can be checked, then stay out of the game's way.
            std::string msg = Utils::Format("USUM One Screen: unsupported game %016llX v%u (0x%04X)",
                                            Process::GetTitleID(), Process::GetVersion(), Process::GetVersion());
            WriteLog(msg, true);
            OSD::Notify(msg);
            return 0;
        }

        std::string msg = Utils::Format("USUM One Screen: %s %016llX v%u (0x%04X)",
                                        s_game->name, s_game->titleId, s_game->version, s_game->version);
        std::string hooks;
        const bool hooksOk = OneScreen::VerifyHooks(*s_game, hooks);
        WriteLog(msg + "\n" + hooks + (hooksOk ? "hooks: all ok" : "hooks: FAILED, nothing will be patched"), true);
        OSD::Notify(msg);
        if (!hooksOk)
        {
            OSD::Notify("USUM One Screen: hook check failed, see usum-onescreen.log");
            return 0;
        }

        LoadSettings();

        OneScreen::StartPanel(OneScreen::PanelMode::Off);

        std::string cursorReport;
        OneScreen::StartCursor(s_game->offsets.hidTouchRead.address, cursorReport);
        WriteLog(cursorReport, false);

        std::string buttonsReport;
        OneScreen::StartButtons(s_game->offsets.hidPadRead.address, buttonsReport);
        WriteLog(buttonsReport, false);

        PluginMenu *menu = new PluginMenu("USUM One Screen", 0, 1, 0,
                                          "Top-screen-only play for Ultra Sun / Ultra Moon.");

        menu->SynchronizeWithFrame(true);
        menu->OnOpening = OnMenuOpening;

        InitMenu(*menu);

        menu->Run();

        delete menu;

        return 0;
    }
}
