#include "Screens/Keyboard.hpp"

#include <deque>
#include <new>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Framebuffer.hpp"
#include "Buttons.hpp"
#include "Panel.hpp"
#include "Scene.hpp"
#include "Screens/KeyboardGrid.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // StrInput CRO, USUM v1.2 (docs/KEYBOARD.md). Vtables are offsets from
    // the module header. The keyboard object (VtFrame) is not in a global:
    // after a catch it hangs off a Zukan object that two scene manager stack
    // entries point to at +0xB0, at +0xB4. It is found by scanning two levels
    // of pointers from the stack entries, and the path is then re-checked
    // every frame.
    static const char *const StrInputModule = "StrInput";
    static const u32 VtFrame = 0xBFE4;          // the keyboard
    static const u32 VtText  = 0xBFC0;          // its text object, at frame +0x1C
    static const u32 FrameText = 0x1C;
    static const u32 TextChars  = 0x30;         // u16[16], 0-terminated
    static const u32 TextQwerty = 0x54;         // 1 QWERTY page, 0 the grid page
    static const u32 TextShift  = 0x5C;         // 1 while shift is on
    static const u32 TextLength = 0x60;
    static const u32 TextMax    = 0x64;         // 12 for a nickname
    static const u32 TextActive = 0x90;         // 1 while the keyboard is up, 0 once confirmed
    static const u32 TextMaxChars = 16;
    static const u32 ManagerSlots = 8;          // scene manager: stack of scene objects
    static const u32 ScanBytes = 0x100;         // words looked at per object and level
    static const u32 CroNext = 0x88, CroPrev = 0x8C;

    static const u32 MoveUp    = Key::DPadUp | Key::CPadUp;
    static const u32 MoveDown  = Key::DPadDown | Key::CPadDown;
    static const u32 MoveLeft  = Key::DPadLeft | Key::CPadLeft;
    static const u32 MoveRight = Key::DPadRight | Key::CPadRight;
    // Everything the game's keyboard could take from the pad. L and R aren't
    // used but would reach the game's keyboard; the panel key (L+R) is read
    // from the real pad, so it still works.
    static const u32 CaptureKeys = Key::A | Key::B | Key::X | Key::Y | Key::Start | Key::L | Key::R
                                   | Key::DPadUp | Key::DPadDown | Key::DPadLeft | Key::DPadRight;

    static const u32 RepeatDelay = 15, RepeatEvery = 5;
    // Rescan for the keyboard this often while it isn't found.
    static const u32 ScanEvery = 15;
    // The keyboard object exists as soon as Yes is pressed, while the
    // keyboard still slides in: keys typed before this wait in the queue.
    static const u32 ReadyFrames = 30;
    // Gone only after this many frames without it.
    static const u32 GoneFrames = 4;
    static const u32 MessageFrames = 90;
    static const size_t QueueMax = 16;

    // --- Shared with the OSD callback (game thread) ---
    static Mutex         s_viewLock;
    static KeyboardView  s_view;                // under s_viewLock
    static volatile u32  s_viewVersion = 0;
    static volatile bool s_shown = false;
    static u8           *s_pixels = nullptr;
    static const size_t  PixelBytes = KeyboardArea.w * KeyboardArea.h * 4;

    // --- Plugin thread ---
    struct GamePath
    {
        u32 slot = 0, off1 = 0, off2 = 0;
        u32 module = 0;
        bool valid = false;
    };
    static bool s_enabled = true;
    static GamePath s_path;
    static KeyboardGame s_game;
    static u32  s_frames = 0, s_lastScan = 0, s_shownAt = 0, s_missing = GoneFrames;
    static u32  s_heapStart = 0, s_heapEnd = 0;
    static int  s_cursor = 0;
    static bool s_found = false;
    static bool s_forcedOff = false, s_yield = false, s_capturing = false;
    static std::deque<int> s_queue;
    static int  s_running = -1;                 // key whose tap is running, -1 none
    static const char *s_message = nullptr;
    static u32  s_messageUntil = 0;
    static u32  s_repeatKey = 0, s_repeatAt = 0;
    static KeyboardView s_published;

    // --- Reading the game ---

    // The linear heap, where the game's objects live: reads inside it go
    // straight to memory (a scan does tens of thousands).
    static void UpdateHeap(void)
    {
        MemInfo info;
        PageInfo page;
        if (R_SUCCEEDED(svcQueryMemory(&info, &page, 0x30000000)) && (info.perm & MEMPERM_READ) != 0)
        {
            s_heapStart = info.base_addr;
            s_heapEnd = info.base_addr + info.size;
        }
        else
            s_heapStart = s_heapEnd = 0;
    }

    static bool InHeap(u32 address, u32 size)
    {
        return (address & 3) == 0 && address >= s_heapStart && address < s_heapEnd && s_heapEnd - address >= size;
    }

    static u32 Word(u32 address)
    {
        return *(const volatile u32 *)address;
    }

    // Header of the loaded CRO named `name`, from the current scene's module
    // along the loaded-module links both ways; 0 if not loaded.
    static u32 FindModule(u32 sceneManager, const char *name)
    {
        u32 manager = 0, scene = 0, vtable = 0;
        if (!Process::Read32(sceneManager, manager) || manager == 0 || !Process::Read32(manager, scene)
            || scene == 0 || !Process::Read32(scene, vtable))
            return 0;
        const u32 start = CroModuleOf(vtable);
        if (start == 0)
            return 0;
        for (u32 link : { CroNext, CroPrev })
        {
            u32 m = start;
            for (int i = 0; i < 32 && m != 0; i++)
            {
                if (CroModuleName(m) == name)
                    return m;
                u32 next = 0;
                if (!Process::Read32(m + link, next) || next == start)
                    break;
                m = next;
            }
        }
        return 0;
    }

    // Follows `path` to the text object and reads it; false if anything on
    // the way doesn't check out or the keyboard isn't active.
    static bool ReadGame(u32 sceneManager, const GamePath &path, KeyboardGame &out)
    {
        u32 manager = 0;
        if (!path.valid || !Process::Read32(sceneManager, manager) || !InHeap(manager, 4 * ManagerSlots))
            return false;
        const u32 entry = Word(manager + 4 * path.slot);
        if (!InHeap(entry, ScanBytes))
            return false;
        const u32 holder = Word(entry + path.off1);
        if (!InHeap(holder, ScanBytes))
            return false;
        const u32 frame = Word(holder + path.off2);
        if (!InHeap(frame, FrameText + 4) || Word(frame) != path.module + VtFrame)
            return false;
        const u32 text = Word(frame + FrameText);
        if (!InHeap(text, TextActive + 4) || Word(text) != path.module + VtText || Word(text + TextActive) != 1)
            return false;
        const u32 length = Word(text + TextLength), max = Word(text + TextMax);
        if (max == 0 || max > TextMaxChars || length > max)
            return false;
        out.length = (int)length;
        out.max = (int)max;
        out.shift = Word(text + TextShift) != 0;
        out.qwerty = Word(text + TextQwerty) == 1;
        for (u32 i = 0; i < TextMaxChars; i++)
            out.text[i] = i < length ? *(const volatile u16 *)(text + TextChars + 2 * i) : 0;
        return true;
    }

    // Two levels of pointers from each scene manager stack entry, looking
    // for the keyboard object (StrInput + VtFrame).
    static bool Scan(u32 sceneManager, GamePath &path, KeyboardGame &game)
    {
        UpdateHeap();
        const u32 module = FindModule(sceneManager, StrInputModule);
        u32 manager = 0;
        if (module == 0 || !Process::Read32(sceneManager, manager) || !InHeap(manager, 4 * ManagerSlots))
            return false;
        for (u32 slot = 0; slot < ManagerSlots; slot++)
        {
            const u32 entry = Word(manager + 4 * slot);
            if (!InHeap(entry, ScanBytes))
                continue;
            for (u32 off1 = 0; off1 < ScanBytes; off1 += 4)
            {
                const u32 holder = Word(entry + off1);
                if (!InHeap(holder, ScanBytes))
                    continue;
                for (u32 off2 = 0; off2 < ScanBytes; off2 += 4)
                {
                    const u32 frame = Word(holder + off2);
                    if (!InHeap(frame, 4) || Word(frame) != module + VtFrame)
                        continue;
                    GamePath found;
                    found.slot = slot;
                    found.off1 = off1;
                    found.off2 = off2;
                    found.module = module;
                    found.valid = true;
                    if (ReadGame(sceneManager, found, game))
                    {
                        path = found;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // --- OSD callback ---

    static UI::Layer s_layer;
    static u32 s_drawnVersion = ~0u;
    static bool s_drawnGameFont = false;    // the game font was loaded when last drawn

    static bool KeyboardOnScreen(const Screen &screen)
    {
        if (!screen.IsTop || !s_shown || s_pixels == nullptr)
            return false;

        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        // Also once the game's font and icons have loaded (UI::Theme draws
        // with them from then on).
        const bool gameFont = UI::Theme::UsingGameFont();
        bool redraw = s_viewVersion != s_drawnVersion || gameFont != s_drawnGameFont;
        if (!s_layer.Matches(fb))
        {
            if (!s_layer.Setup(fb, KeyboardArea, KeyboardRadius, s_pixels, PixelBytes))
                return false;
            redraw = true;
        }
        // Never wait for the plugin thread here (this is the game's frame):
        // if it is handing over a new view, draw it next frame.
        if (redraw && !s_viewLock.TryLock())     // CTRPF: TryLock() is true on failure
        {
            const u32 version = s_viewVersion;
            UI::Canvas canvas = s_layer.Begin();
            DrawKeyboard(canvas, s_view);
            s_drawnVersion = version;
            s_drawnGameFont = gameFont;
            s_viewLock.Unlock();
        }
        s_layer.Present(fb);
        return true;
    }

    void StartKeyboard(void)
    {
        OSD::Run(KeyboardOnScreen);
    }

    // --- Plugin thread ---

    static void Publish(void)
    {
        KeyboardView view;
        view.game = s_game;
        view.cursor = s_cursor;
        view.message = s_frames < s_messageUntil ? s_message : nullptr;
        if (!s_game.qwerty)
            view.message = "Switching the game's keyboard back to QWERTY...";
        bool same = view.cursor == s_published.cursor && view.message == s_published.message
                    && view.game.length == s_published.game.length && view.game.max == s_published.game.max
                    && view.game.shift == s_published.game.shift && view.game.qwerty == s_published.game.qwerty;
        for (int i = 0; same && i < (int)TextMaxChars; i++)
            same = view.game.text[i] == s_published.game.text[i];
        if (same)
            return;
        s_published = view;
        s_viewLock.Lock();
        s_view = view;
        s_viewVersion = s_viewVersion + 1;
        s_viewLock.Unlock();
    }

    static void Say(const char *message)
    {
        s_message = message;
        s_messageUntil = s_frames + MessageFrames;
    }

    static std::string GameText(const KeyboardGame &game)
    {
        return KeyboardUtf8(game.text, game.length);
    }

    static const char *KeyName(int index)
    {
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        return index >= 0 && index < (int)keys.size() ? keys[index].name : "?";
    }

    static void PendingCounts(int &chars, int &deletes)
    {
        chars = deletes = 0;
        const std::vector<KeyboardKey> &keys = KeyboardKeys();
        auto count = [&](int i)
        {
            if (i < 0 || i >= (int)keys.size())
                return;
            if (keys[i].kind == KeyKind::Char || keys[i].kind == KeyKind::Space)
                chars++;
            else if (keys[i].kind == KeyKind::Delete)
                deletes++;
        };
        for (int i : s_queue)
            count(i);
        count(s_running);
    }

    static void Press(int index, std::vector<std::string> &log)
    {
        int chars, deletes;
        PendingCounts(chars, deletes);
        switch (KeyboardPressKey(index, s_game, chars, deletes))
        {
            case KeyboardPress::Full:
                Say("The name is full: B deletes, START goes to OK");
                log.push_back(Utils::Format("keyboard: %s refused, full (%d of %d)", KeyName(index),
                                            s_game.length + chars - deletes, s_game.max));
                return;
            case KeyboardPress::Empty:
                return;
            case KeyboardPress::Tap:
                break;
        }
        if (s_queue.size() >= QueueMax)
            return;
        s_queue.push_back(index);
        log.push_back(Utils::Format("keyboard: key %s", KeyName(index)));
    }

    static void MoveTo(int index, std::vector<std::string> &log)
    {
        if (index == s_cursor)
            return;
        s_cursor = index;
        log.push_back(Utils::Format("keyboard: cursor %s", KeyName(index)));
    }

    static void HandleInput(std::vector<std::string> &log)
    {
        const u32 held = CaptureRealHeld();
        const u32 pressed = CaptureRealPressed();

        const u32 dirs[] = { MoveUp, MoveDown, MoveLeft, MoveRight };
        u32 dir = 0;
        for (u32 d : dirs)
            if (pressed & d)
                dir = d;
        if (dir != 0)
        {
            s_repeatKey = dir;
            s_repeatAt = s_frames + RepeatDelay;
        }
        else if (s_repeatKey != 0 && (held & s_repeatKey) && s_frames >= s_repeatAt)
        {
            dir = s_repeatKey;
            s_repeatAt = s_frames + RepeatEvery;
        }
        else if (!(held & s_repeatKey))
            s_repeatKey = 0;

        if (dir == MoveUp)         MoveTo(KeyboardMove(s_cursor, 0, -1), log);
        else if (dir == MoveDown)  MoveTo(KeyboardMove(s_cursor, 0, 1), log);
        else if (dir == MoveLeft)  MoveTo(KeyboardMove(s_cursor, -1, 0), log);
        else if (dir == MoveRight) MoveTo(KeyboardMove(s_cursor, 1, 0), log);

        if (pressed & Key::A)
            Press(s_cursor, log);
        if (pressed & Key::B)
            Press(KeyboardFind("delete"), log);
        if (pressed & Key::Y)
            Press(KeyboardFind("shift"), log);
        if (pressed & Key::X)
            Press(KeyboardFind("space"), log);
        if (pressed & Key::Start)
            MoveTo(KeyboardFind("ok"), log);
    }

    // Starts the next queued tap once the last one is done.
    static void RunQueue(u32 sceneManager, std::vector<std::string> &log)
    {
        if (ActionRunning())
            return;
        s_running = -1;
        if (s_frames - s_shownAt < ReadyFrames)
            return;
        if (!s_game.qwerty)
        {
            // The player switched pages by touch: go back before typing.
            ActionStart(KeyboardToQwerty(), sceneManager, log);
            return;
        }
        if (s_queue.empty())
            return;
        const int index = s_queue.front();
        s_queue.pop_front();
        if (ActionStart(KeyboardAction(index), sceneManager, log))
            s_running = index;
    }

    void KeyboardUpdate(u32 sceneManager, std::vector<std::string> &log)
    {
        s_frames++;

        // Find or follow the game's keyboard.
        KeyboardGame game;
        bool found = ReadGame(sceneManager, s_path, game);
        if (!found && (s_found || s_frames - s_lastScan >= ScanEvery))
        {
            s_lastScan = s_frames;
            found = Scan(sceneManager, s_path, game);
        }
        if (!found)
            s_path.valid = false;
        if (found)
        {
            s_missing = 0;
            if (!s_found)
                log.push_back(Utils::Format("keyboard: game keyboard found (stack %lu +%lX +%lX), %d of %d: \"%s\"",
                                            (unsigned long)s_path.slot, (unsigned long)s_path.off1,
                                            (unsigned long)s_path.off2, game.length, game.max,
                                            GameText(game).c_str()));
            else if (game.length != s_game.length || GameText(game) != GameText(s_game))
                log.push_back(Utils::Format("keyboard: text \"%s\" (%d of %d)", GameText(game).c_str(),
                                            game.length, game.max));
            if (s_found && game.shift != s_game.shift)
                log.push_back(Utils::Format("keyboard: shift %s", game.shift ? "on" : "off"));
            if (s_found && game.qwerty != s_game.qwerty)
                log.push_back(Utils::Format("keyboard: game page %s", game.qwerty ? "QWERTY" : "grid"));
            s_game = game;
        }
        else if (s_missing < GoneFrames)
            s_missing++;
        const bool up = s_missing < GoneFrames;
        if (s_found && !up)
            log.push_back("keyboard: game keyboard gone, last text \"" + GameText(s_game) + "\"");
        s_found = up;

        if (!up)
            s_yield = false;

        // The panel: off while the native keyboard is up. A panel turned on
        // while we hold it off is the player's doing (L+R): step aside.
        bool holdOff = s_enabled && !s_yield && up;
        if (holdOff && s_forcedOff && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            holdOff = false;
            log.push_back("keyboard: panel turned on by hand, native keyboard hidden until the game's keyboard closes");
        }
        if (holdOff)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("keyboard: native keyboard up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }

        const bool shown = s_enabled && !s_yield && up;
        if (shown && !s_shown)
        {
            if (s_pixels == nullptr)
                s_pixels = new (std::nothrow) u8[PixelBytes];
            UI::RequestAssets();        // the game's font (normally loaded at the field already)
            s_shownAt = s_frames;
            s_cursor = 0;               // the game's own cursor starts on "!" too
            s_queue.clear();
            s_repeatKey = 0;
            s_message = nullptr;
            log.push_back(Utils::Format("keyboard: shown%s", s_pixels == nullptr ? ", NO MEMORY for the layer" : ""));
        }
        else if (!shown && s_shown)
        {
            if (!s_queue.empty())
                log.push_back(Utils::Format("keyboard: hidden, %u queued keys dropped", (unsigned)s_queue.size()));
            else
                log.push_back("keyboard: hidden");
            s_queue.clear();
        }
        s_shown = shown;

        // Input: ours while the keyboard shows. The action layer captures
        // everything while a tap runs and gives our capture back after, so
        // ours only ends once no tap is running.
        if (shown)
        {
            if (!s_capturing && !ActionRunning())
            {
                InputCaptureBegin(CaptureKeys, true, false);
                s_capturing = true;
            }
            if (s_capturing && !ActionRunning())
                HandleInput(log);
            RunQueue(sceneManager, log);
        }
        else if (s_capturing && !ActionRunning())
        {
            InputCaptureEnd();
            s_capturing = false;
        }

        if (shown)
            Publish();
    }

    bool KeyboardCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(KeyboardStatus());
        }
        else if (args == "status")
            log.push_back(KeyboardStatus());
        else if (args == "read")
        {
            // Finds the game's keyboard now, whether or not the screen shows.
            GamePath path;
            KeyboardGame game;
            if (Scan(sceneManager, path, game))
                log.push_back(Utils::Format("keyboard: read \"%s\" (%d of %d), shift %s, page %s, stack %lu +%lX +%lX",
                                            GameText(game).c_str(), game.length, game.max, game.shift ? "on" : "off",
                                            game.qwerty ? "QWERTY" : "grid", (unsigned long)path.slot,
                                            (unsigned long)path.off1, (unsigned long)path.off2));
            else
                log.push_back(Utils::Format("keyboard: read: no game keyboard (StrInput %s)",
                                            FindModule(sceneManager, StrInputModule) != 0 ? "loaded" : "not loaded"));
        }
        else
            return false;
        return true;
    }

    std::string KeyboardStatus(void)
    {
        return Utils::Format("keyboard: %s, %s, cursor %s, text \"%s\" (%d of %d), %u queued%s%s%s",
                             s_enabled ? "on" : "off", s_shown ? "shown" : "hidden", KeyName(s_cursor),
                             GameText(s_game).c_str(), s_game.length, s_game.max, (unsigned)s_queue.size(),
                             s_yield ? ", panel on by hand" : "", s_forcedOff ? ", holding panel off" : "",
                             s_capturing ? ", input captured" : "");
    }
}
