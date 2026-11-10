#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Game.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Battle.hpp"
#include "Box.hpp"
#include "Buttons.hpp"
#include "Cursor.hpp"
#include "InputCapture.hpp"
#include "Panel.hpp"
#include "Party.hpp"
#include "PcAnywhere.hpp"
#include "QuickSave.hpp"
#include "UI/LayerGpu.hpp"
#include "UI/GpuQuad.hpp"
#include "Scene.hpp"
#include "Screens/BattleScreen.hpp"
#include "Screens/Keyboard.hpp"
#include "Screens/Pokedex.hpp"
#include "Screens/SubScreens.hpp"
#include "Screens/Bag.hpp"
#include "Screens/Map.hpp"
#include "Screens/PartyScreen.hpp"
#include "Screens/RotomPrompt.hpp"
#include "Screens/Boxes.hpp"
#include "Screens/XMenu.hpp"
#include "Screens/ThemeScreen.hpp"
#include "UI/Assets.hpp"
#include "UI/ScreenBuffer.hpp"
#include "Settings.hpp"
#include "SettingsFile.hpp"
#include "Shot.hpp"
#include "UI/Device.hpp"
#include "Where.hpp"
#include "Widescreen.hpp"
#include "BattleWidescreen.hpp"
#include "FastText.hpp"

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
        OneScreen::SetSettings(settings);
        OneScreen::AutoPanelSetEnabled(settings.autoPanel);
        WriteLog("settings: " + OneScreen::DescribeSettings(settings), false);
        WriteLog(OneScreen::SetWidescreenAspect(settings.aspect), false);
        OneScreen::BattleScreenSetLayout(settings.battleLayout);
        WriteLog(OneScreen::ApplyFastTextSettings(s_game->offsets.sceneManager), false);
    }

    // Sets `key = value` in [section] of /usum-onescreen.ini (SetIniValue:
    // every other line and comment is kept) and writes the file back. A
    // missing file starts from the commented defaults. Logs the result;
    // `text` is the file as written, for the caller to check.
    static bool SaveSetting(const std::string &section, const std::string &key, const std::string &value,
                            std::string &text)
    {
        const std::string what = "[" + section + "] " + key + " = " + value;
        text.clear();
        if (File::Exists(SettingsPath) == 1)
        {
            File file;
            if (File::Open(file, SettingsPath, File::READ) != File::SUCCESS)
            {
                WriteLog(Utils::Format("settings: can't open %s, %s not saved", SettingsPath, what.c_str()), false);
                return false;
            }
            text.resize(file.GetSize());
            const int read = text.empty() ? (int)File::SUCCESS : file.Read(&text[0], text.size());
            file.Close();
            if (read != File::SUCCESS)
            {
                WriteLog(Utils::Format("settings: can't read %s (%d), %s not saved", SettingsPath, read, what.c_str()),
                         false);
                return false;
            }
        }
        else
            text = OneScreen::DefaultSettingsText();

        text = OneScreen::SetIniValue(text, section, key, value);

        File file;
        if (File::Open(file, SettingsPath, File::WRITE | File::CREATE | File::TRUNCATE | File::SYNC) != File::SUCCESS)
        {
            WriteLog(Utils::Format("settings: can't write %s, %s not saved", SettingsPath, what.c_str()), false);
            return false;
        }
        const int written = file.Write(text.data(), text.size());
        file.Close();
        if (written != File::SUCCESS)
        {
            WriteLog(Utils::Format("settings: write to %s failed (%d), %s may not be saved", SettingsPath, written,
                                   what.c_str()),
                     false);
            return false;
        }
        WriteLog("settings: saved " + what, false);
        return true;
    }

    // Menu entry and `battlelayout NAME`: shows `layout` now, keeps it in
    // the settings in use (View settings) and saves it to the file.
    static void SetBattleLayout(OneScreen::BattleLayout layout, const char *source)
    {
        OneScreen::BattleScreenSetLayout(layout);
        OneScreen::Settings settings = OneScreen::GetSettings();
        settings.battleLayout = layout;
        OneScreen::SetSettings(settings);
        WriteLog(Utils::Format("battle layout: %s (%s)", OneScreen::BattleLayoutName(layout), source), false);

        std::string text;
        if (!SaveSetting("battle", "layout", OneScreen::BattleLayoutName(layout), text))
            return;
        // A second `layout` line later in the file would win at the next boot.
        std::vector<std::string> warnings;
        if (OneScreen::ParseSettings(text, warnings).battleLayout != layout)
            WriteLog(Utils::Format("settings: another layout line in %s overrides it at the next boot; "
                                   "remove the extra one", SettingsPath + 1),
                     false);
    }

    static void SaveSpeedSetting(const std::string &key, const std::string &value)
    {
        std::string text;
        if (!SaveSetting("speed", key, value, text)) return;
        std::vector<std::string> warnings;
        const auto saved = OneScreen::ParseSettings(text, warnings);
        const auto &current = OneScreen::GetSettings();
        const bool matches = key == "instant_text" ? saved.instantText == current.instantText
            : key == "battle_messages" ? saved.battleMessages == current.battleMessages
            : saved.battleBars == current.battleBars;
        if (!matches)
            WriteLog("settings: another " + key + " line overrides it at next boot; remove the extra one", false);
    }

    static void SetInstantText(bool enabled)
    {
        auto settings = OneScreen::GetSettings();
        settings.instantText = enabled;
        OneScreen::SetSettings(settings);
        WriteLog(OneScreen::ApplyFastTextSettings(s_game->offsets.sceneManager), false);
        SaveSpeedSetting("instant_text", enabled ? "on" : "off");
    }

    static void SetBattleSpeed(OneScreen::BattleSpeed speed)
    {
        auto settings = OneScreen::GetSettings();
        settings.battleMessages = settings.battleBars = speed;
        OneScreen::SetSettings(settings);
        WriteLog(OneScreen::ApplyFastTextSettings(s_game->offsets.sceneManager), false);
        WriteLog(std::string("battle speed: ") + OneScreen::BattleSpeedName(speed), false);
        SaveSpeedSetting("battle_messages", OneScreen::BattleSpeedName(speed));
        SaveSpeedSetting("battle_bars", OneScreen::BattleSpeedName(speed));
    }

    // Test commands, one per line, in /usum-cmd.txt on the SD card. The file is
    // removed once read, and each command plus the resulting state is logged:
    //   move X Y | touch on | touch off | tap FRAMES | mode off|panel|full | status
    //   press KEY [FRAMES] | hold KEY | release KEY   (KEY: a b x y l r start select up down left right)
    //     (default 3 frames: menus start key repeat by 8, so one press moved two steps)
    //   stick X Y [FRAMES]   (circle pad, up is +Y, ~156 full tilt; default 30 frames)
    //   snap NAME   (dump global data to /usum-snap-NAME.bin)
    //   peek ADDR [COUNT]   (log COUNT words at hex ADDR, default 4)
    //   poke ADDR VALUE | pokef ADDR FLOAT   (write data memory; hex ADDR/VALUE, decimal FLOAT)
    //   vtscan NAME   (global -> object -> vtable scan to /usum-vt-NAME.bin)
    //   scene   (log the current scene's module name)
    //   aspect [VALUE|off]   (inspect / set perspective aspect; docs/WIDESCREEN.md)
    //   cullnames            (log culled pane names for the next frame, max 32)
    //   battle   (log the battle state, docs/RE1-battle.md; also logged on change)
    //   where   (log the zone, position and facing, docs/RE4-map.md)
    //   party | bag   (log the party / the bag pockets, docs/RE3-party-bag.md)
    //   box [N]   (log PC box N, default the one on screen, and the Box screen, docs/RE6-boxes.md)
    //   memmap | dump ADDR SIZE NAME   (readable blocks; raw memory to /usum-dump-NAME.bin)
    //   auto on | auto off   (panel follows the scene, or stays as set)
    //   perf on | perf off | perf   (panel and uidemo timing; perf logs and restarts the window)
    //   uidemo off|on|shade|direct | uifont   (UI layer test menu; system font info)
    //   capture on|off [KEY|stick|touch ...]   (hide input from the game as a plugin
    //     menu would; on adds, off removes; no list = everything)
    //   action NAME | action status | action cancel | action list   (drive the bottom
    //     screen: open-pokedex, open-party, open-bag, battle-run, close; Actions.hpp)
    //   battleui on | battleui off | battleui   (native battle menu on the top screen,
    //     Screens/BattleScreen.hpp; on by default)
    //   fasttext [on|off] | battlespeed normal|fast|instant (apply and save [speed])
    //   battlelayout | battlelayout classic|bar|rail   (log / set the battle menu's look;
    //     setting it also saves [battle] layout to /usum-onescreen.ini)
    //   xmenu on|off|status | xmenu select NAME | xmenu open NAME   (native X menu,
    //     Screens/XMenu.hpp; NAME as in XMenuGrid.cpp: pokemon, pokedex, ..., map, save)
    //   keyboard on|off|status|read   (native keyboard, Screens/Keyboard.hpp; read
    //     looks for the game's keyboard now and logs its text)
    //   pokedex on|off|status|rows   (native Pokedex, Screens/Pokedex.hpp; rows logs
    //     the seen entries of each dex)
    //   screens on|off|status   (native Options, Trainer Passport, Battle Video;
    //     Screens/SubScreens.hpp)
    //   bagui on|off|status|read | bagui select N   (native Bag, Screens/Bag.hpp;
    //     read logs what the plugin reads of the game's Bag; select moves the list cursor)
    //   map on|off|open|close|status   (native top-screen map, Screens/Map.hpp)
    //   rotom on|off|status|read|select N|answer [N]|talk|next   (Rotom's field prompts, Screens/RotomPrompt.hpp)
    //   partyui on|off|status   (native Party screen and summary, Screens/PartyScreen.hpp)
    //   pc | pc status   (open the PC boxes from the field, PcAnywhere.hpp;
    //     logs pc: opened / pc: refused (reason))
    //   boxes on|off|status|details|pick|drop|prev|next   (native PC boxes, Screens/Boxes.hpp;
    //     pick/drop/prev/next as A / L / R)
    //   uitheme on|off|load|status|page N|select N   (USUM theme demo with the game's own
    //     font and icons, Screens/ThemeScreen.hpp)
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
        else if (line == "where")
            WriteLog(OneScreen::DescribeWhere(OneScreen::ReadWhere()), false);
        else if (line == "party")
            WriteLog(OneScreen::DescribeParty(OneScreen::ReadParty(s_game->offsets.sceneManager)), false);
        else if (line == "bag")
            WriteLog(OneScreen::DescribeBag(OneScreen::ReadBag()), false);
        else if (line == "box" || std::sscanf(line.c_str(), "box %u", &a) == 1)
            WriteLog(OneScreen::DescribeBox(s_game->offsets.sceneManager, line == "box" ? 0 : (int)a), false);
        else if (line == "memmap")
            WriteLog(OneScreen::MemoryMap(), false);
        else if (std::sscanf(line.c_str(), "dump %x %x %15s", &a, &b, word) == 3)
            WriteLog(OneScreen::DumpMemory(a, b, word), false);
        else if (line == "glyphs status")
            WriteLog(std::string("glyphs: ") + OneScreen::GlyphFamilyName(OneScreen::GetSettings().glyphs), false);
        else if (line.compare(0, 7, "glyphs ") == 0)
        {
            OneScreen::Settings settings = OneScreen::GetSettings();
            const std::string arg = line.substr(7);
            if (arg == "auto" || !OneScreen::ParseGlyphFamily(arg, settings.glyphs))
                WriteLog("cmd: use glyphs 3ds|xbox|playstation|nintendo|status", false);
            else
            {
                OneScreen::SetSettings(settings);
                WriteLog(std::string("glyphs: ") + OneScreen::GlyphFamilyName(settings.glyphs), false);
            }
        }
        else if (line == "aspect")
            WriteLog(OneScreen::DescribeWidescreenAspect(), false);
        else if (line == "cullnames")
            OneScreen::RequestWidescreenCullNames();
        else if (line.compare(0, 7, "aspect ") == 0 || line.compare(0, 7, "aspect\t") == 0)
        {
            float aspect;
            WriteLog(OneScreen::ParseAspect(line.substr(7), aspect)
                ? OneScreen::SetWidescreenAspect(aspect)
                : "widescreen: invalid aspect (use 0, off, a positive float, 16:9 or 21:9)", false);
        }
        else if (line == "scene")
            WriteLog("scene: " + OneScreen::SceneModule(s_game->offsets.sceneManager), false);
        else if (line.compare(0, 7, "uidemo ") == 0)
        {
            static const char *modes[] = { "off", "on", "shade", "direct" };
            const std::string arg = line.substr(7);
            int mode = 3;
            while (mode >= 0 && arg != modes[mode])
                mode--;
            if (mode < 0)
            {
                WriteLog("cmd: unknown uidemo mode in '" + line + "'", false);
                return;
            }
            OneScreen::UiDemoSetMode((OneScreen::UiDemoMode)mode);
        }
        else if (line == "uifont")
            WriteLog(OneScreen::UiFontInfo(), false);
        else if (line == "battleui on" || line == "battleui off")
            OneScreen::BattleScreenSetEnabled(line == "battleui on");
        else if (line == "battleui")
            WriteLog(OneScreen::BattleScreenStatus(), false);
        else if (line == "fasttext")
            WriteLog(OneScreen::FastTextStatus(), false);
        else if (line == "fasttext on" || line == "fasttext off")
            SetInstantText(line == "fasttext on");
        else if (line.compare(0, 12, "battlespeed ") == 0)
        {
            OneScreen::BattleSpeed speed;
            if (!OneScreen::ParseBattleSpeed(line.substr(12), speed))
            {
                WriteLog("cmd: use battlespeed normal|fast|instant", false);
                return;
            }
            SetBattleSpeed(speed);
        }
        else if (line == "battlelayout")
            WriteLog(std::string("battle layout: ") + OneScreen::BattleLayoutName(OneScreen::BattleScreenLayout()),
                     false);
        else if (line.compare(0, 13, "battlelayout ") == 0)
        {
            OneScreen::BattleLayout layout = OneScreen::BattleLayout::Classic;
            if (!OneScreen::ParseBattleLayout(line.substr(13), layout))
            {
                WriteLog("cmd: unknown battle layout in '" + line + "' (use classic, bar or rail)", false);
                return;
            }
            SetBattleLayout(layout, "cmd");
        }
        else if (line == "auto on" || line == "auto off")
            OneScreen::AutoPanelSetEnabled(line == "auto on");
        else if (line == "perf on" || line == "perf off")
            OneScreen::PanelPerfSetEnabled(line == "perf on");
        else if (line == "perf")
        {
            WriteLog(OneScreen::PanelPerfReport(), false);
            WriteLog(OneScreen::UiPerfReport(), false);
        }
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
        else if (line.compare(0, 7, "action ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::ActionCommand(line.substr(7), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown action in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 6, "xmenu ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::XMenuCommand(line.substr(6), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown xmenu command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 9, "keyboard ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::KeyboardCommand(line.substr(9), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown keyboard command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 8, "pokedex ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::PokedexCommand(line.substr(8), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown pokedex command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 8, "screens ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::SubScreensCommand(line.substr(8), log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown screens command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 6, "bagui ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::BagCommandLine(line.substr(6), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown bagui command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 4, "map ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::MapCommand(line.substr(4), log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown map command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 6, "rotom ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::RotomPromptCommand(line.substr(6), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown rotom command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 8, "partyui ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::PartyScreenCommand(line.substr(8), log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown partyui command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 6, "hires ") == 0)
            WriteLog(OneScreen::UI::HiResCommand(line.substr(6)), false);
        else if (line.compare(0, 8, "gpuquad ") == 0)
            WriteLog(OneScreen::UI::GpuQuadCommand(line.substr(8)), false);
        else if (line == "layergpu on" || line == "layergpu off" || line == "layergpu status")
        {
            if (line != "layergpu status")
                OneScreen::UI::SetLayerGpu(line == "layergpu on");
            WriteLog(OneScreen::UI::LayerGpuStatus(), false);
        }
        else if (line == "save")
        {
            std::vector<std::string> log;
            OneScreen::QuickSaveRequest(s_game->offsets.sceneManager, log);
            for (const std::string &l : log) WriteLog(l, false);
        }
        else if (line == "pc" || line.compare(0, 3, "pc ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::PcAnywhereCommand(line == "pc" ? "" : line.substr(3),
                                                            s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown pc command in '" + line + "' (use pc or pc status)", false);
                return;
            }
        }
        else if (line.compare(0, 6, "boxes ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::BoxesRunCommand(line.substr(6), s_game->offsets.sceneManager, log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown boxes command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 8, "uitheme ") == 0)
        {
            std::vector<std::string> log;
            const bool known = OneScreen::ThemeScreenCommand(line.substr(8), log);
            for (const std::string &l : log)
                WriteLog(l, false);
            if (!known)
            {
                WriteLog("cmd: unknown uitheme command in '" + line + "'", false);
                return;
            }
        }
        else if (line.compare(0, 5, "poke ") == 0 && std::sscanf(line.c_str(), "poke %x %x", &a, &b) == 2)
        {
            u32 old = 0, value = 0;
            if (a < 0x08000000)
                WriteLog("poke: refusing code address", false);
            else if (!Process::CheckAddress(a, MEMPERM_WRITE)
                || !Process::Read32(a, old) || !Process::Write32(a, b)
                || !Process::Read32(a, value))
                WriteLog(Utils::Format("poke: %08X not writable", a), false);
            else
                WriteLog(Utils::Format("poke: %08X old %08lX -> new %08lX", a, old, value), false);
        }
        else if (int offset = 0; std::sscanf(line.c_str(), "pokef %x %n", &a, &offset) == 1)
        {
            if (a < 0x08000000)
                WriteLog("poke: refusing code address", false);
            else
            {
                const char *start = line.c_str() + offset;
                char *end = nullptr;
                const float target = std::strtof(start, &end);
                if (end == start || !std::isfinite(target)
                    || line.find_first_not_of(" \t\r\n", end - line.c_str()) != std::string::npos)
                    WriteLog("pokef: invalid float", false);
                else
                {
                    u32 bits = 0, old = 0, value = 0;
                    std::memcpy(&bits, &target, sizeof(bits));
                    if (!Process::CheckAddress(a, MEMPERM_WRITE)
                        || !Process::Read32(a, old) || !Process::Write32(a, bits)
                        || !Process::Read32(a, value))
                        WriteLog(Utils::Format("pokef: %08X not writable", a), false);
                    else
                    {
                        float oldFloat = 0.0f, newFloat = 0.0f;
                        std::memcpy(&oldFloat, &old, sizeof(oldFloat));
                        std::memcpy(&newFloat, &value, sizeof(newFloat));
                        WriteLog(Utils::Format("pokef: %08X %f -> %f", a, oldFloat, newFloat), false);
                    }
                }
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

    // Runs every frame while the menu is closed. panel_key (L+R) cycles the
    // panel by hand (off -> panel -> full). With auto on, the panel follows the
    // scene (AutoPanel) and a hand choice holds until the next scene change.
    // C-stick moves the touch cursor and ZL touches (cursor_move, touch_key).
    // Scene and panel changes go to the log.
    // True on the plugin's own threads, whose stacks are in the plugin's
    // memory (0x06000000-0x07FFFFFF). CTRPF also runs the menu entries from
    // its LoadCRO hook, on the game thread that loads a CRO; that thread's
    // stack is small and sits in the game's heap. PanelEntry's work needs
    // more stack than that: on a wild battle's Battle.cro load it overflowed
    // into the game's ExpHeap mutex (gm + 0x15C) and the game froze
    // (docs/PC-ANYWHERE.md, "The battle freeze").
    static bool OnPluginStack(void)
    {
        u32 sp;
        __asm__ volatile("mov %0, sp" : "=r"(sp));
        return sp >= 0x06000000 && sp < 0x08000000;
    }

    static void PanelEntry(MenuEntry *entry)
    {
        static u32 frames = 0;
        static std::string scene;

        if (!OnPluginStack())
            return;

        const u32 panelKey = OneScreen::GetSettings().panelKey;
        if (!OneScreen::QuickSaveRunning() && panelKey != 0 && Controller::IsKeysPressed(panelKey))
        {
            OneScreen::CyclePanelMode();
            OneScreen::AutoPanelManualOverride();
            WriteLog(OneScreen::PanelStatus(), false);
        }

        OneScreen::CursorUpdate();
        OneScreen::ButtonsUpdate();

        const std::string speedLog = OneScreen::FastTextFrame(s_game->offsets.sceneManager);
        if (!speedLog.empty())
            WriteLog(speedLog, false);

        const std::string battleWideLog = OneScreen::BattleWidescreenFrame(s_game->offsets.sceneManager);
        if (!battleWideLog.empty())
            WriteLog(battleWideLog, false);

        const std::string cullLog = OneScreen::WidescreenFrame();
        if (!cullLog.empty())
            WriteLog(cullLog, false);

        std::vector<std::string> actionLog;
        OneScreen::ActionsUpdate(s_game->offsets.sceneManager, actionLog);
        for (const std::string &line : actionLog)
            WriteLog(line, false);

        std::vector<std::string> saveLog;
        OneScreen::QuickSaveUpdate(s_game->offsets.sceneManager, saveLog);
        for (const std::string &line : saveLog) WriteLog(line, false);

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

        std::vector<std::string> battleLog;
        OneScreen::BattleScreenUpdate(s_game->offsets.sceneManager, scene, battleLog);
        for (const std::string &line : battleLog)
            WriteLog(line, false);

        const int fieldState = scene == "FieldRo" ? OneScreen::FieldState(s_game->offsets.sceneManager) : -1;
        if (OneScreen::AutoPanelUpdate(scene, OneScreen::ButtonsGameHeld(), fieldState))
            WriteLog(OneScreen::AutoPanelStatus() + " -> " + OneScreen::PanelStatus(), false);

        std::vector<std::string> xmenuLog;
        OneScreen::XMenuUpdate(s_game->offsets.sceneManager, scene, fieldState, xmenuLog);
        for (const std::string &line : xmenuLog)
            WriteLog(line, false);

        std::vector<std::string> keyboardLog;
        OneScreen::KeyboardUpdate(s_game->offsets.sceneManager, keyboardLog);
        for (const std::string &line : keyboardLog)
            WriteLog(line, false);

        std::vector<std::string> dexLog;
        OneScreen::PokedexUpdate(s_game->offsets.sceneManager, scene, dexLog);
        for (const std::string &line : dexLog)
            WriteLog(line, false);

        std::vector<std::string> screensLog;
        OneScreen::SubScreensUpdate(s_game->offsets.sceneManager, scene, screensLog);
        for (const std::string &line : screensLog)
            WriteLog(line, false);

        std::vector<std::string> bagLog;
        OneScreen::BagUpdate(s_game->offsets.sceneManager, scene, bagLog);
        for (const std::string &line : bagLog)
            WriteLog(line, false);

        std::vector<std::string> mapLog;
        OneScreen::MapUpdate(s_game->offsets.sceneManager, scene, fieldState, mapLog);
        for (const std::string &line : mapLog)
            WriteLog(line, false);

        std::vector<std::string> rotomLog;
        OneScreen::RotomPromptUpdate(s_game->offsets.sceneManager, scene, fieldState, rotomLog);
        for (const std::string &line : rotomLog)
            WriteLog(line, false);

        std::vector<std::string> partyLog;
        OneScreen::PartyScreenUpdate(s_game->offsets.sceneManager, scene, partyLog);
        for (const std::string &line : partyLog)
            WriteLog(line, false);

        std::vector<std::string> boxesLog;
        OneScreen::BoxesUpdate(s_game->offsets.sceneManager, scene, boxesLog);
        for (const std::string &line : boxesLog)
            WriteLog(line, false);

        std::vector<std::string> pcLog;
        OneScreen::PcAnywhereUpdate(s_game->offsets.sceneManager, scene, pcLog);
        for (const std::string &line : pcLog)
            WriteLog(line, false);

        std::vector<std::string> assetsLog;
        OneScreen::UI::AssetsUpdate(scene, assetsLog);
        for (const std::string &line : assetsLog)
            WriteLog(line, false);

        std::vector<std::string> themeLog;
        OneScreen::ThemeScreenUpdate(s_game->offsets.sceneManager, themeLog);
        for (const std::string &line : themeLog)
            WriteLog(line, false);

        OneScreen::ShotPoll();
        PollCommands();
    }

    // Menu entries below are actions: A in the plugin menu (opened with
    // Select, CTRPF's default menu hotkey) runs them. Their names show the
    // current state and are refreshed each time the menu opens.
    static MenuEntry *s_autoEntry = nullptr;
    static MenuEntry *s_layoutEntry = nullptr;
    static MenuEntry *s_textEntry = nullptr;
    static MenuEntry *s_speedEntry = nullptr;

    static void RefreshMenuNames(void)
    {
        const auto &settings = OneScreen::GetSettings();
        if (s_textEntry)
            s_textEntry->Name() = std::string("Instant text: ") + (settings.instantText ? "on" : "off");
        if (s_speedEntry)
            s_speedEntry->Name() = std::string("Battle speed: ")
                + (settings.battleMessages == settings.battleBars
                    ? OneScreen::BattleSpeedTitle(settings.battleMessages) : "Custom");
        if (s_autoEntry != nullptr)
            s_autoEntry->Name() = std::string("Auto panel: ") + (OneScreen::AutoPanelEnabled() ? "on" : "off");
        if (s_layoutEntry != nullptr)
            s_layoutEntry->Name() = std::string("Battle layout: ")
                                    + OneScreen::BattleLayoutTitle(OneScreen::BattleScreenLayout());
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

    // Classic -> Bar -> Rail -> Classic; saved to the file (the first setting
    // the plugin writes back).
    static void CycleBattleLayout(MenuEntry *entry)
    {
        SetBattleLayout(OneScreen::NextBattleLayout(OneScreen::BattleScreenLayout()), "menu");
        RefreshMenuNames();
    }

    static void ToggleInstantText(MenuEntry *entry)
    {
        SetInstantText(!OneScreen::GetSettings().instantText);
        RefreshMenuNames();
    }

    static void CycleBattleSpeed(MenuEntry *entry)
    {
        const auto &s = OneScreen::GetSettings();
        const auto next = s.battleMessages != s.battleBars ? OneScreen::BattleSpeed::Normal
            : s.battleMessages == OneScreen::BattleSpeed::Normal ? OneScreen::BattleSpeed::Fast
            : s.battleMessages == OneScreen::BattleSpeed::Fast ? OneScreen::BattleSpeed::Instant
            : OneScreen::BattleSpeed::Normal;
        SetBattleSpeed(next);
        RefreshMenuNames();
    }

    // Asks for the PC boxes; they open once the plugin menu closes.
    static void OpenPc(MenuEntry *entry)
    {
        std::vector<std::string> log;
        OneScreen::PcAnywhereRequest(s_game->offsets.sceneManager, "menu", log);
        for (const std::string &line : log)
            WriteLog(line, false);
        if (!log.empty())
        {
            MessageBox box("Open PC (Boxes)", log.back());
            box();
        }
    }

    static void QuickSave(MenuEntry *)
    {
        std::vector<std::string> log;
        OneScreen::QuickSaveRequest(s_game->offsets.sceneManager, log);
        for (const std::string &line : log) WriteLog(line, false);
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
        MenuEntry *panel = new MenuEntry("Bottom screen on top", PanelEntry,
                                         "Shows the bottom screen on the top screen. The panel combo cycles: off, small panel, full view. "
                                         "Cursor controls and keys are set in usum-onescreen.ini.");
        menu += panel;
        panel->Enable();

        s_autoEntry = new MenuEntry("Auto panel", nullptr, ToggleAutoPanel,
                                    "Switch between following the scene and staying as set. "
                                    "Lasts until the next boot; set auto in usum-onescreen.ini to keep it.");
        menu += s_autoEntry;
        s_layoutEntry = new MenuEntry("Battle layout", nullptr, CycleBattleLayout,
                                      "Change how the battle menu looks. Saved to usum-onescreen.ini.");
        menu += s_layoutEntry;
        s_textEntry = new MenuEntry("Instant text", nullptr, ToggleInstantText,
            "Toggle instant typing. Page ends still need a press. Saved to usum-onescreen.ini.");
        menu += s_textEntry;
        s_speedEntry = new MenuEntry("Battle speed", nullptr, CycleBattleSpeed,
            "Cycle Normal, Fast, Instant for message waits and HP/EXP bars. Saved to usum-onescreen.ini.");
        menu += s_speedEntry;
        menu += new MenuEntry("Open PC (Boxes)", nullptr, OpenPc,
                              "Open the PC boxes here, as at a Pokemon Center PC. Works in the overworld "
                              "when nothing else is going on; they open when this menu closes. "
                              "pc_key in usum-onescreen.ini sets a button combo for it.");
        menu += new MenuEntry("Quick save", nullptr, QuickSave,
                              "Save your adventure through the game's save flow after closing this menu. "
                              "Only available in the idle overworld. save_key sets an optional combo.");
        menu += new MenuEntry("View settings", nullptr, ShowSettings,
                              "Show the settings in use, read from /usum-onescreen.ini at boot.");
        menu += new MenuEntry("Reload settings file", nullptr, ReloadSettings,
                              "Read /usum-onescreen.ini again. Problems go to usum-onescreen.log.");
        RefreshMenuNames();
    }

    int main(void)
    {
        // First, before the heap fragments (UI/ScreenBuffer.hpp).
        const bool bufferOk = OneScreen::UI::ScreenBuffer::Init();
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
        WriteLog(Utils::Format("screen buffer: %u KB%s; ", (unsigned)(OneScreen::UI::ScreenBuffer::Bytes / 1024),
                               bufferOk ? "" : ", NO MEMORY (native screens stay hidden)")
                     + OneScreen::UI::ScreenBuffer::HeapStatus(),
                 false);
        OSD::Notify(msg);
        if (!hooksOk)
        {
            OSD::Notify("USUM One Screen: hook check failed, see usum-onescreen.log");
            return 0;
        }

        LoadSettings();

        OneScreen::BattleScreenSetEnabled(true);
        // Before the panel: its callback takes the screenshots, so it must
        // run after the native menus have drawn.
        OneScreen::StartXMenu();
        OneScreen::StartKeyboard();
        OneScreen::StartPokedex();
        OneScreen::StartSubScreens();
        OneScreen::StartBag();
        OneScreen::StartMap();
        OneScreen::StartRotomPrompt();
        OneScreen::StartPartyScreen();
        OneScreen::StartBoxes();
        OneScreen::StartThemeScreen();
        OneScreen::StartPanel(OneScreen::PanelMode::Off, s_game->offsets.gxDisplayTransfer.address,
                              s_game->offsets.gxTextureCopy.address);
        OneScreen::StartActions();
        WriteLog(OneScreen::UI::StartGpuQuad(s_game->offsets.gxDisplayTransfer.address), false);

        std::string cursorReport;
        OneScreen::StartCursor(s_game->offsets.hidTouchRead.address, cursorReport);
        WriteLog(cursorReport, false);

        std::string buttonsReport;
        OneScreen::StartButtons(s_game->offsets.hidPadRead.address, buttonsReport);
        WriteLog(buttonsReport, false);

        std::string pcReport;
        OneScreen::StartPcAnywhere(pcReport);
        WriteLog(pcReport, false);

        std::string widescreenReport;
        OneScreen::StartWidescreen(widescreenReport);
        WriteLog(widescreenReport, false);

        PluginMenu *menu = new PluginMenu("USUM One Screen", 0, 1, 1,
                                          "Top-screen-only play for Ultra Sun / Ultra Moon.");

        menu->SynchronizeWithFrame(true);
        menu->OnOpening = OnMenuOpening;

        InitMenu(*menu);

        menu->Run();

        delete menu;

        return 0;
    }
}
