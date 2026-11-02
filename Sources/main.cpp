#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Game.hpp"
#include <cstdio>
#include <vector>

#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Cursor.hpp"
#include "Panel.hpp"
#include "Scene.hpp"
#include "Shot.hpp"

namespace CTRPluginFramework
{
    static const OneScreen::GameInfo *s_game = nullptr;

    static const char *LogPath = "/usum-onescreen.log";

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
    //   auto on | auto off   (panel follows the scene, or stays as set)
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
        else if (line == "scene")
            WriteLog("scene: " + OneScreen::SceneModule(s_game->offsets.sceneManager), false);
        else if (line == "auto on" || line == "auto off")
            OneScreen::AutoPanelSetEnabled(line == "auto on");
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
    // (AutoPanel); ZR cycles it by hand (off -> panel -> full) until the next
    // scene change. C-stick moves the touch cursor and ZL touches. Scene and
    // panel changes go to the log.
    static void PanelEntry(MenuEntry *entry)
    {
        static u32 frames = 0;
        static std::string scene;

        if (Controller::IsKeyPressed(Key::ZR))
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
        }

        if (OneScreen::AutoPanelUpdate(scene, OneScreen::ButtonsGameHeld()))
            WriteLog(OneScreen::AutoPanelStatus() + " -> " + OneScreen::PanelStatus(), false);

        OneScreen::ShotPoll();
        PollCommands();
    }

    static void InitMenu(PluginMenu &menu)
    {
        MenuEntry *panel = new MenuEntry("Bottom screen on top (auto, ZR to override)", PanelEntry,
                                         "Shows the bottom screen on the top screen. ZR cycles: off, small panel, full view. "
                                         "C-stick moves the touch cursor, ZL touches.");
        menu += panel;
        panel->Enable();
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

        InitMenu(*menu);

        menu->Run();

        delete menu;

        return 0;
    }
}
