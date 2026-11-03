#include "Screens/SubScreens.hpp"

#include <new>

#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Framebuffer.hpp"
#include "Panel.hpp"
#include "PartyData.hpp"
#include "Screens/BattleVideo.hpp"
#include "Screens/Options.hpp"
#include "Screens/TrainerPassport.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/ScreenBuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Held direction: first repeat after RepeatDelay frames, then every
    // RepeatEvery (as the X menu).
    static const u32 RepeatDelay = 15, RepeatEvery = 5;

    static OptionsScreen  s_options;
    static PassportScreen s_passport;
    static VideoScreen    s_video;
    static NativeScreen *const s_screens[] = { &s_options, &s_passport, &s_video };
    static const int ScreenCount = sizeof(s_screens) / sizeof(s_screens[0]);

    // --- Shared with the OSD callback (game thread), under s_lock ---
    static Mutex          s_lock;
    static volatile int   s_sampling = -1;      // screen whose points the OSD samples
    static volatile int   s_drawing = -1;       // screen the OSD draws (shown)
    static volatile u32   s_sampleSeq = 0;
    static Sample         s_sampleBuf[MaxScreenSamples];
    static const char *const Owner = "subscreens";  // UI::ScreenBuffer

    // --- Plugin thread ---
    static bool s_enabled = true;
    static int  s_active = -1;          // screen whose scene is up
    static bool s_shown = false;
    static bool s_forcedOff = false;    // we hold the panel off (AutoPanel manual override)
    static bool s_yield = false;        // the player turned the panel on: stay hidden until the scene ends
    static bool s_capturing = false;
    static u32  s_captureKeys = 0;
    static bool s_running = false;      // an action from Key() runs
    static u32  s_seenSeq = 0;
    static u32  s_frames = 0;
    static u32  s_repeatKey = 0, s_repeatAt = 0;
    static u32  s_pendingKey = 0;       // pressed while an action ran: handled after it

    // --- Memory (docs/OFFSETS.md) ---

    static bool Readable(u32 address, u32 size)
    {
        return address != 0 && Process::CheckAddress(address, MEMPERM_READ)
               && Process::CheckAddress(address + size - 1, MEMPERM_READ);
    }

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        Process::Read32(address, v);
        return v;
    }

    static u16 Read16(u32 address)
    {
        u16 v = 0;
        Process::Read16(address, v);
        return v;
    }

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    static const u32 GameManagerGlobal = 0x6A59C4;  // GameManager*; +0x24 GameData, +0x04 SaveData
    static const u32 VtSaveData   = 0x64F4A0;
    static const u32 VtConfigSave = 0x64F064;       // Savedata::ConfigSave (RTTI)
    static const u32 VtMyStatus   = 0x64F460;       // Savedata::MyStatus (RTTI)
    static const u32 VtMisc       = 0x64F384;       // Savedata::Misc (RTTI)
    static const u32 VtPlayTime   = 0x64F480;       // play time (hours u16 +4, minutes u8 +6)
    static const u32 VtMsgData    = 0x64B104;       // gfl2::str::MsgData
    static const u32 SaveDataConfig = 0x3B30;
    static const u32 PassportLines = 52;            // the passport's message file

    static bool ReadSaveWord(u32 &word)
    {
        const u32 manager = Read32(GameManagerGlobal);
        const u32 data = Readable(manager + 0x24, 4) ? Read32(manager + 0x24) : 0;
        const u32 save = Readable(data + 4, 4) ? Read32(data + 4) : 0;
        if (!Readable(save, SaveDataConfig + 8) || Read32(save) != VtSaveData)
            return false;
        const u32 config = save + SaveDataConfig;
        if (Read32(config) != VtConfigSave)
            return false;
        word = Read32(config + 4);
        return true;
    }

    static u32 SceneObject(u32 sceneManager)
    {
        // As SceneModule(): the scene object is *(*sceneManager).
        const u32 manager = Readable(sceneManager, 4) ? Read32(sceneManager) : 0;
        return Readable(manager, 4) ? Read32(manager) : 0;
    }

    // The Trainer Passport's work object: scene +0x14. It holds (+0x10) the
    // stamp book, whose +0x1C is the scene and +0x20 the page (0-based),
    // MyStatus (+0x14), play time (+0x20), Misc (+0x24) and the passport's
    // message file (+0x2C, 52 lines).
    static u32 PassportWork(u32 sceneManager)
    {
        const u32 scene = SceneObject(sceneManager);
        const u32 work = Readable(scene + 0x14, 4) ? Read32(scene + 0x14) : 0;
        if (!Readable(work, 0x30) || Read32(Read32(work + 0x14)) != VtMyStatus)
            return 0;
        return work;
    }

    static int PassportPage(u32 sceneManager)
    {
        const u32 scene = SceneObject(sceneManager);
        const u32 work = PassportWork(sceneManager);
        const u32 book = work ? Read32(work + 0x10) : 0;
        if (!Readable(book, 0x24) || Read32(book + 0x1C) != scene)
            return -1;
        const u32 page = Read32(book + 0x20);
        return page < (u32)PassportPages ? (int)page : -1;
    }

    // One line of a full-load MsgData (docs/RE2-text.md); "" if anything is off.
    static std::string MsgLine(u32 msg, u32 line)
    {
        if (!Readable(msg, 0x30) || Read32(msg) != VtMsgData || Read8(msg + 0x08) != 1)
            return "";
        const u32 header = Read32(msg + 0x14), section = Read32(msg + 0x18);
        if (!Readable(header, 4) || !Readable(section, 4) || line >= Read16(header + 2))
            return "";
        const u32 size = Read32(section);
        const u32 entry = section + 4 + line * 8;
        if (4 + line * 8 + 8 > size)
            return "";
        const u32 offset = Read32(entry);
        const u32 length = Read16(entry + 4);
        if (length == 0 || length > 96 || offset + length * 2 > size || !Readable(section + offset, length * 2))
            return "";
        u16 encrypted[96], text[96];
        for (u32 i = 0; i < length; i++)
            encrypted[i] = Read16(section + offset + i * 2);
        DecryptMsgLine(encrypted, length, line, text, 96);
        char utf8[3 * 96 + 1];
        Utf16ToUtf8(text, 96, utf8, sizeof(utf8));
        return utf8;
    }

    static PassportData ReadPassport(u32 sceneManager)
    {
        PassportData d;
        const u32 work = PassportWork(sceneManager);
        if (work == 0)
            return d;
        d.found = true;
        const u32 status = Read32(work + 0x14);
        if (Readable(status + 0x40, 26))
        {
            u16 name[13];
            for (u32 i = 0; i < 13; i++)
                name[i] = Read16(status + 0x40 + i * 2);
            name[12] = 0;
            char utf8[3 * 13 + 1];
            Utf16ToUtf8(name, 13, utf8, sizeof(utf8));
            d.name = utf8;
        }
        d.idValid = Readable(status + 8, 4);
        d.id = d.idValid ? Read32(status + 8) : 0;
        const u32 misc = Read32(work + 0x24);
        d.moneyValid = Readable(misc, 12) && Read32(misc) == VtMisc;
        d.money = d.moneyValid ? Read32(misc + 8) : 0;
        const u32 time = Read32(work + 0x20);
        d.timeValid = Readable(time, 8) && Read32(time) == VtPlayTime;
        d.hours = d.timeValid ? Read16(time + 4) : 0;
        d.minutes = d.timeValid ? Read8(time + 6) : 0;

        const u32 msg = Read32(work + 0x2C);
        const u32 header = Readable(msg, 0x30) ? Read32(msg + 0x14) : 0;
        if (Readable(header, 4) && Read16(header + 2) == PassportLines)
        {
            d.labelName = MsgLine(msg, 0);
            d.labelId = MsgLine(msg, 2);
            d.labelMoney = MsgLine(msg, 7);
            d.labelTime = MsgLine(msg, 13);
            for (int i = 0; i < PassportPages; i++)
            {
                d.stampName[i] = MsgLine(msg, 22 + 2 * i);
                d.stampText[i] = MsgLine(msg, 23 + 2 * i);
            }
        }
        return d;
    }

    // --- OSD callback ---

    static UI::Layer s_layer;
    static int s_layerScreen = -1;
    static u32 s_drawnKey = ~0u;

    static void SampleBottom(const Screen &screen, int index)
    {
        const std::vector<SamplePoint> &points = s_screens[index]->Samples();
        const FbLayout fb = GetLayout(screen);
        const size_t count = points.size() < MaxScreenSamples ? points.size() : MaxScreenSamples;
        for (size_t i = 0; i < count; i++)
        {
            Sample &s = s_sampleBuf[i];
            DecodePixel(fb.origin + points[i].x * fb.stepX + points[i].y * fb.stepY, fb.format, s.r, s.g, s.b);
        }
        s_sampleSeq = s_sampleSeq + 1;
    }

    static bool SubScreensOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
        {
            const int index = s_sampling;
            if (index >= 0 && !s_lock.TryLock())
            {
                SampleBottom(screen, index);
                s_lock.Unlock();
            }
            return false;
        }
        const int index = s_drawing;
        if (index < 0)
        {
            UI::ScreenBuffer::GiveBack(Owner);
            return false;
        }
        bool fresh = false;
        u8 *pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
        if (pixels == nullptr)
            return false;

        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        NativeScreen *ns = s_screens[index];
        bool redraw = fresh || index != s_layerScreen;
        if (fresh || !s_layer.Matches(fb) || index != s_layerScreen)
        {
            if (!s_layer.Setup(fb, ns->Area(), ScreenStyle::Radius, pixels, UI::ScreenBuffer::Bytes))
                return false;
            s_layerScreen = index;
            redraw = true;
        }
        // Redraw only when the screen's content changed, and only if the
        // plugin thread isn't changing it right now (else next frame).
        if (!s_lock.TryLock())
        {
            const u32 key = ns->DrawKey();
            if (redraw || key != s_drawnKey)
            {
                UI::Canvas canvas = s_layer.Begin();
                ns->Draw(canvas);
                s_drawnKey = key;
            }
            s_lock.Unlock();
        }
        else if (redraw)
            return false;
        s_layer.Present(fb);
        return true;
    }

    void StartSubScreens(void)
    {
        // Build the sample point tables here, not first in the OSD thread.
        for (NativeScreen *ns : s_screens)
            ns->Samples();
        OSD::Run(SubScreensOnScreen);
    }

    // --- Plugin thread ---

    static u32 FoldPad(u32 keys)
    {
        if (keys & ScreenKey::CPadUp)    keys |= ScreenKey::Up;
        if (keys & ScreenKey::CPadDown)  keys |= ScreenKey::Down;
        if (keys & ScreenKey::CPadLeft)  keys |= ScreenKey::Left;
        if (keys & ScreenKey::CPadRight) keys |= ScreenKey::Right;
        return keys & ~ScreenKey::CPad;
    }

    // The one key to hand the screen this frame (0: none), with repeat for
    // a held direction.
    static u32 NextKey(void)
    {
        const u32 held = FoldPad(CaptureRealHeld());
        const u32 pressed = FoldPad(CaptureRealPressed());
        const u32 dirs[] = { ScreenKey::Up, ScreenKey::Down, ScreenKey::Left, ScreenKey::Right };
        for (u32 d : dirs)
            if (pressed & d)
            {
                s_repeatKey = d;
                s_repeatAt = s_frames + RepeatDelay;
                return d;
            }
        const u32 others[] = { ScreenKey::A, ScreenKey::L, ScreenKey::R, ScreenKey::Start };
        for (u32 k : others)
            if (pressed & k)
                return k;
        if (s_repeatKey != 0 && (held & s_repeatKey) && s_frames >= s_repeatAt)
        {
            s_repeatAt = s_frames + RepeatEvery;
            return s_repeatKey;
        }
        if (!(held & s_repeatKey))
            s_repeatKey = 0;
        return 0;
    }

    static void EndCapture(void)
    {
        if (s_capturing && !ActionRunning())
        {
            InputCaptureEnd();
            s_capturing = false;
            s_captureKeys = 0;
        }
    }

    static void ReleasePanel(void)
    {
        if (s_forcedOff)
        {
            // Clears the override: AutoPanel picks the mode for what is up now.
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }
    }

    static void Leave(std::vector<std::string> &log)
    {
        if (s_active < 0)
            return;
        if (s_shown)
        {
            log.push_back(std::string(s_screens[s_active]->Name()) + ": hidden");
            log.push_back(UI::ScreenBuffer::HeapLog((std::string(s_screens[s_active]->Name()) + " hidden").c_str()));
        }
        u32 word = 0;

        if (s_active == 0 && ReadSaveWord(word))
        {
            // What the game kept (Confirm / Yes) or not (back, No): in its
            // memory only, nothing is written to the save file here.
            int values[OptionRows];
            OptionsFromSaveWord(word, values);
            log.push_back(Utils::Format("options: closed, save word %08lX: ", word) + DescribeOptions(values));
        }
        s_shown = false;
        s_drawing = -1;
        s_sampling = -1;
        s_running = false;
        s_pendingKey = 0;
        s_yield = false;
        s_active = -1;
    }

    void SubScreensUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        s_frames++;
        int active = -1;
        if (s_enabled)
            for (int i = 0; i < ScreenCount; i++)
                if (scene == s_screens[i]->Scene())
                    active = i;

        if (active != s_active)
        {
            Leave(log);
            if (active >= 0)
            {
                std::vector<std::string> begin;
                if (active == 0)
                {
                    u32 word = 0;
                    const bool ok = ReadSaveWord(word);
                    s_options.SetSaveWord(word, ok);
                }
                s_lock.Lock();
                s_screens[active]->Begin(begin);
                s_lock.Unlock();
                log.insert(log.end(), begin.begin(), begin.end());
                s_active = active;
                s_seenSeq = s_sampleSeq;
                s_sampling = active;
                s_repeatKey = 0;
            }
        }
        if (s_active < 0)
        {
            ReleasePanel();
            EndCapture();
            return;
        }
        NativeScreen *ns = s_screens[s_active];

        s_lock.Lock();
        if (s_active == 1)
        {
            if (!s_passport.HasData() && s_frames % 10 == 0)
                s_passport.SetData(ReadPassport(sceneManager), log);
            s_passport.SetPage(PassportPage(sceneManager));
        }
        if (s_sampleSeq != s_seenSeq)
        {
            s_seenSeq = s_sampleSeq;
            Sample samples[MaxScreenSamples];
            for (u32 i = 0; i < MaxScreenSamples; i++)
                samples[i] = s_sampleBuf[i];
            ns->Sampled(samples, log);
        }
        if (s_running && !ActionRunning())
        {
            s_running = false;
            ns->ActionDone(log);
        }
        s_lock.Unlock();

        // The panel: off while the scene is up. A panel turned on while we
        // hold it off is the player's doing (L+R): step aside.
        if (!s_yield && s_forcedOff && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            log.push_back(std::string(ns->Name()) + ": panel turned on by hand, native screen hidden until the scene ends");
        }
        if (!s_yield)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back(std::string(ns->Name()) + ": native screen up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else
            ReleasePanel();

        const bool shown = !s_yield && ns->Ready();
        if (shown && !s_shown)
        {
            log.push_back(std::string(ns->Name()) + ": shown");
            log.push_back(UI::ScreenBuffer::HeapLog((std::string(ns->Name()) + " shown").c_str()));
        }
        else if (!shown && s_shown)
        {
            log.push_back(std::string(ns->Name()) + ": hidden");
            log.push_back(UI::ScreenBuffer::HeapLog((std::string(ns->Name()) + " hidden").c_str()));
        }

        s_shown = shown;
        s_drawing = shown ? s_active : -1;

        // Input: the screen's keys while it shows and no action runs. The
        // action layer captures everything while it runs and restores this
        // capture afterwards; a key pressed meanwhile (the player's real
        // keys are still read) waits for it, the last one only.
        if (ActionRunning())
        {
            if (shown && s_running)
            {
                const u32 key = NextKey();
                if (key != 0)
                    s_pendingKey = key;
            }
            return;
        }
        const u32 keys = shown ? ns->CaptureKeys() : 0;
        if (keys == 0)
        {
            EndCapture();
            return;
        }
        if (!s_capturing || keys != s_captureKeys)
        {
            InputCaptureBegin(keys & ~ScreenKey::CPad, (keys & ScreenKey::CPad) != 0, false);
            s_capturing = true;
            s_captureKeys = keys;
        }
        u32 key = NextKey();
        if (key == 0)
        {
            key = s_pendingKey;
            s_pendingKey = 0;
        }
        if (key == 0)
            return;
        s_lock.Lock();
        const ActionDef *def = ns->Key(key, log);
        s_lock.Unlock();
        if (def != nullptr && ActionStart(*def, sceneManager, log))
            s_running = true;
        else if (def != nullptr)
        {
            s_lock.Lock();
            ns->ActionDone(log);
            s_lock.Unlock();
        }
    }

    bool SubScreensCommand(const std::string &args, std::vector<std::string> &log)
    {
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(SubScreensStatus());
        }
        else if (args == "status")
            log.push_back(SubScreensStatus());
        else
            return false;
        return true;
    }

    std::string SubScreensStatus(void)
    {
        std::string out = std::string("screens: ") + (s_enabled ? "on" : "off");
        if (s_active < 0)
            return out + ", none up";
        s_lock.Lock();
        const std::string status = s_screens[s_active]->Status();
        s_lock.Unlock();
        return out + ", " + s_screens[s_active]->Name() + (s_shown ? " shown" : " hidden") + (s_yield ? " (panel on by hand)" : "")
               + (s_capturing ? ", input captured" : "") + "; " + status;
    }
}
