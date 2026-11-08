#include "Screens/RotomPrompt.hpp"

#include <cstring>
#include <new>

#include <CTRPluginFramework.hpp>

#include "Actions.hpp"
#include "AutoPanel.hpp"
#include "Buttons.hpp"
#include "Panel.hpp"
#include "Scene.hpp"
#include "Screens/Map.hpp"
#include "Screens/RotomPromptView.hpp"
#include "UI/Device.hpp"
#include "UI/Assets.hpp"
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"
#include "UI/ScreenBuffer.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2. Found and checked as described in docs/ROTOM-FIELD.md.
    // Rotom's applet: a FieldMenu CRO object on the scene manager's stack.
    static const u32 ManagerSlots = 8;
    static const u32 VtRotomApplet = 0x18284;       // FieldMenu-relative
    static const u32 RotomTable = 0x164DC;          // FieldMenu-relative: chat table, 0x9A x 16 bytes
    static const u32 PowerTable = 0x1645C;          // FieldMenu-relative: Roto Loto powers, 11 x 8 bytes
    static const u32 PowerEntries = 11, PowerEntryBytes = 8, PowerDescription = 4;
    static const u16 FirstQuestion = 961;           // entry 0's question, checked once per module
    static const u32 AppletMyStatus = 0xA0;         // Savedata::MyStatus
    static const u32 AppletMsgData = 0xC4;          // message file 89, full load
    static const u32 AppletAnswers = 0xC8;          // 2 answers up, 1 picked, 0 none
    static const u32 AppletAnimation = 0xF0;        // 0x5C: Rotom wants to talk
    static const u32 AppletEntry = 0xF8;            // chat table entry, 0 none
    static const u32 AppletLoto = 0x100;            // u8 Roto Loto state
    static const u32 AppletPower = 0x104;           // Roto Loto power table entry
    static const u32 ReadyAnimation = 0x5C;
    static const u32 VtMyStatus = 0x64F460;
    static const u32 MyStatusName = 0x40;           // 13 UTF-16 units
    static const u32 VtMsgData = 0x64B104;
    static const u32 RotomLines = 1488;
    // Roto Loto's lines in file 89 (docs/ROTOM-FIELD.md).
    static const u16 LotoIntroLine = 1470, LotoResultLine = 1469;

    // Keys the prompt takes from the game while a box shows. The circle pad
    // and B stay with the game, so the player can keep walking (and run).
    static const u32 TalkKey = Key::DPadUp;
    static const char *const TalkKeyName = "Up";
    static const u32 ChoiceKeys = Key::A | Key::DPadUp | Key::DPadDown;
    static const u32 LotoKeys = Key::A;
    // Rotom's reply shows this long after an answer.
    static const u32 ReplyFrames = 300;
    // After an answer's (or a Roto Loto) tap, wait this long for the applet to move on.
    static const u32 AnswerWait = 60;
    // Look for the applet this often while it isn't found.
    static const u32 ScanEvery = 15;

    // --- Shared with the OSD callback (game thread) ---
    static volatile bool s_shown = false;
    static RotomView     s_views[2];
    static volatile u32  s_viewSlot = 0;
    static volatile u32  s_viewVersion = 0;
    // The boxes borrow UI::ScreenBuffer. The badge has its own small buffer:
    // it shows on the field next to other screens (over the map).
    static const char *const Owner = "rotom";
    static u8            s_badgePixels[RotomBadge.w * RotomBadge.h * 4];

    // --- Plugin thread ---
    static bool s_enabled = true;
    static u32  s_frames = 0;
    static u32  s_applet = 0;               // the applet object, 0 if not found
    static u32  s_module = 0;               // its FieldMenu module
    static bool s_tableOk = false;          // the chat table checked out for s_module
    static u32  s_lastScan = 0;
    static RotomReading s_reading;
    static RotomView s_view;                // what is shown (published to the OSD)
    static int  s_entry = -1;               // the chat entry the Choice is for
    static RotomEntry s_entryLines;
    static int  s_picked = -1;              // the answer being / last tapped
    static u32  s_busyUntil = 0;            // answer / Roto Loto tap: wait for the applet until here
    static u32  s_lotoTapped = 0;           // the Roto Loto state the last tap was made in
    static u32  s_replyUntil = 0;
    static std::string s_replyText;
    static bool s_talkPending = false;      // the talk tap ran; waiting for what Rotom does
    static bool s_forcedOff = false;
    static bool s_yield = false;            // the player turned the panel on: stay hidden until the box goes
    static bool s_capturing = false;
    static u32  s_captureMask = 0;
    static bool s_reopenMap = false;        // the map was open when a box came: open it again after
    static std::string s_lastLog;           // the last "rotom: <view>" line, so each view is logged once

    // --- OSD callback ---

    static UI::Layer s_layer;
    static u32 s_drawnVersion = ~0u;
    static UI::Rect s_drawnArea = { 0, 0, 0, 0 };
    static bool s_drawnGameFont = false;    // the game font was loaded when last drawn

    static bool RotomOnScreen(const Screen &screen)
    {
        if (!screen.IsTop)
            return false;
        const RotomView view = s_views[s_viewSlot];
        const bool badge = view.mode == RotomMode::Ready;
        if (!s_shown || badge)
            UI::ScreenBuffer::GiveBack(Owner);
        if (!s_shown)
            return false;
        u8 *pixels = s_badgePixels;
        size_t bytes = sizeof(s_badgePixels);
        if (!badge)
        {
            bool fresh = false;
            pixels = UI::ScreenBuffer::Borrow(Owner, fresh);
            bytes = UI::ScreenBuffer::Bytes;
            if (pixels == nullptr)
                return false;
            if (fresh)
                s_drawnArea = UI::Rect{ 0, 0, 0, 0 };
        }
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        const u32 version = s_viewVersion;
        const UI::Rect area = RotomArea(view);
        if (area.Empty())
            return false;
        // Also once the game's font and icons have loaded.
        const bool gameFont = UI::Theme::UsingGameFont();
        bool redraw = version != s_drawnVersion || gameFont != s_drawnGameFont;
        // The badge and the boxes have different areas, so a switch between
        // them always sets up again (onto the other buffer).
        if (!s_layer.Matches(fb) || area.x != s_drawnArea.x || area.y != s_drawnArea.y || area.w != s_drawnArea.w
            || area.h != s_drawnArea.h)
        {
            const int radius = badge ? area.h / 2 : RotomRadius;
            if (!s_layer.Setup(fb, area, radius, pixels, bytes))
                return false;
            s_drawnArea = area;
            redraw = true;
        }
        if (redraw)
        {
            UI::Canvas canvas = s_layer.Begin();
            DrawRotomPrompt(canvas, view);
            s_drawnVersion = version;
            s_drawnGameFont = gameFont;
        }
        return s_layer.Present(fb);
    }

    void StartRotomPrompt(void)
    {
        OSD::Run(RotomOnScreen);
    }

    // --- Plugin thread: reading the applet ---

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

    static bool Readable(u32 address, u32 size)
    {
        return address != 0 && Process::CheckAddress(address, MEMPERM_READ)
            && Process::CheckAddress(address + size - 1, MEMPERM_READ);
    }

    static void FindApplet(u32 sceneManager, std::vector<std::string> &log)
    {
        s_applet = 0;
        const u32 manager = Read32(sceneManager);
        for (u32 i = 0; manager != 0 && i < ManagerSlots; i++)
        {
            const u32 object = Read32(manager + i * 4);
            if (object == 0)
                break;
            const u32 vtable = Read32(object);
            const u32 module = CroModuleOf(vtable);
            if (module == 0 || vtable != module + VtRotomApplet || CroModuleName(module) != "FieldMenu")
                continue;
            s_applet = object;
            if (module != s_module)
            {
                s_module = module;
                s_tableOk = Read16(module + RotomTable) == FirstQuestion;
                log.push_back(Utils::Format("rotom: applet %08lX, FieldMenu at %08lX, chat table %s", object,
                                            module, s_tableOk ? "ok" : "NOT FOUND"));
            }
            return;
        }
    }

    static RotomReading Read(u32 sceneManager, std::vector<std::string> &log)
    {
        RotomReading r;
        const bool stillThere = s_applet != 0 && s_module != 0 && Read32(s_applet) == s_module + VtRotomApplet;
        if (!stillThere && (s_applet != 0 || s_frames - s_lastScan >= ScanEvery))
        {
            s_lastScan = s_frames;
            FindApplet(sceneManager, log);
        }
        if (s_applet == 0 || !Readable(s_applet, AppletPower + 4))
            return r;
        r.applet = true;
        r.entry = s_tableOk ? RotomEntryIndex(s_module + RotomTable, Read32(s_applet + AppletEntry)) : -1;
        r.answers = Read32(s_applet + AppletAnswers);
        r.ready = Read32(s_applet + AppletAnimation) == ReadyAnimation;
        r.loto = Read8(s_applet + AppletLoto);
        const u32 power = Read32(s_applet + AppletPower);
        const u32 powers = s_module + PowerTable;
        if (power >= powers && (power - powers) % PowerEntryBytes == 0 && (power - powers) / PowerEntryBytes < PowerEntries)
            r.lotoPower = (int)((power - powers) / PowerEntryBytes);
        return r;
    }

    // Line `line` of Rotom's message file, as text; "" if it can't be read.
    static std::string Line(u32 line)
    {
        const u32 msg = Read32(s_applet + AppletMsgData);
        if (!Readable(msg, 0x30) || Read32(msg) != VtMsgData || Read8(msg + 0x08) != 1)
            return "";
        const u32 header = Read32(msg + 0x14), section = Read32(msg + 0x18);
        if (!Readable(header, 4) || !Readable(section, 4) || Read16(header + 2) != RotomLines || line >= RotomLines)
            return "";
        const u32 size = Read32(section);
        if (4 + line * 8 + 8 > size)
            return "";
        const u32 offset = Read32(section + 4 + line * 8);
        const u32 length = Read16(section + 4 + line * 8 + 4);
        if (length == 0 || length > 512 || offset + length * 2 > size || !Readable(section + offset, length * 2))
            return "";
        std::vector<u16> encrypted(length);
        for (u32 i = 0; i < length; i++)
            encrypted[i] = Read16(section + offset + i * 2);

        std::string name;
        const u32 status = Read32(s_applet + AppletMyStatus);
        if (Readable(status, MyStatusName + 26) && Read32(status) == VtMyStatus)
        {
            u16 units[14] = {};
            for (u32 i = 0; i < 13; i++)
                units[i] = Read16(status + MyStatusName + i * 2);
            char utf8[3 * 13 + 1];
            Utf16ToUtf8(units, 13, utf8, sizeof(utf8));
            name = utf8;
        }
        return RotomLineText(encrypted.data(), length, line, name);
    }

    // --- Plugin thread: actions ---

    // One tap on Rotom's screen each. Built once: the action layer keeps a
    // pointer to the running one.
    enum TapId { TapAnswer1, TapAnswer2, TapLotoNext, TapLotoStop, TapTalk, TapCount };

    static const ActionDef &TapAction(TapId id)
    {
        static const struct { const char *name, *button; } taps[TapCount] = {
            { "rotom-answer-1", "rotom_chat.answer_1" },
            { "rotom-answer-2", "rotom_chat.answer_2" },
            { "rotom-loto-next", "rotom_loto.text_box" },
            { "rotom-loto-stop", "rotom_loto.reel" },
            { "rotom-talk", "field.rotom_eye_left" },
        };
        static std::vector<ActionDef> defs;
        if (defs.empty())
            for (const auto &tap : taps)
                defs.push_back(ActionDef{ tap.name, "taps Rotom's screen", InScene(SceneField),
                                          { StepTap(tap.button), StepWait(ActionGapFrames) } });
        return defs[id];
    }

    static bool Tap(TapId id, u32 sceneManager, std::vector<std::string> &log)
    {
        const ActionDef &def = TapAction(id);
        const std::string problem = CheckAction(def);
        if (!problem.empty())
        {
            log.push_back(std::string("rotom: ") + def.name + " not available: " + problem);
            return false;
        }
        return ActionStart(def, sceneManager, log);
    }

    static void Answer(const RotomView &view, int i, u32 sceneManager, std::vector<std::string> &log)
    {
        log.push_back(Utils::Format("rotom: answer %d \"%s\"", i + 1, view.answer[i]));
        if (!Tap(i == 0 ? TapAnswer1 : TapAnswer2, sceneManager, log))
            return;
        s_picked = i;
        s_busyUntil = s_frames + AnswerWait;
    }

    static void LotoNext(const RotomView &view, u32 sceneManager, std::vector<std::string> &log)
    {
        const bool spinning = view.loto == RotoLotoStep::Spinning;
        log.push_back(spinning ? "rotom: roto loto stop" : "rotom: roto loto next");
        if (!Tap(spinning ? TapLotoStop : TapLotoNext, sceneManager, log))
            return;
        s_lotoTapped = s_reading.loto;
        s_busyUntil = s_frames + AnswerWait;
    }

    static void Talk(const char *how, u32 sceneManager, std::vector<std::string> &log)
    {
        log.push_back(std::string("rotom: talk (") + how + ")");
        if (Tap(TapTalk, sceneManager, log))
            s_talkPending = true;
    }

    // --- Plugin thread: the view ---

    static void Publish(void)
    {
        const u32 next = s_viewSlot ^ 1;
        s_views[next] = s_view;
        s_viewSlot = next;
        s_viewVersion = s_viewVersion + 1;
    }

    // The text of the box, read and wrapped only when what it shows changes.
    static RotomView s_text;
    static int s_textKey = -1;

    static const RotomView &Text(int key, RotomMode mode, const std::string &text)
    {
        if (key != s_textKey || s_text.mode != mode)
        {
            s_text = RotomView();
            s_text.mode = mode;
            RotomSetText(s_text, text);
            s_textKey = key;
        }
        return s_text;
    }

    static void CopyText(RotomView &to, const RotomView &from)
    {
        to.lines = from.lines;
        std::memcpy(to.text, from.text, sizeof(to.text));
    }

    static RotomView MakeView(bool inField, bool menu)
    {
        RotomView view;
        std::snprintf(view.key, sizeof(view.key), "%s", TalkKeyName);
        const RotomReading &r = s_reading;
        if (!s_enabled || !inField || menu || !r.applet)
            return view;

        if (r.entry >= 0 && r.answers == 2)
        {
            view.mode = RotomMode::Choice;
            if (r.entry != s_textKey || s_text.mode != RotomMode::Choice)
            {
                u16 words[8];
                for (u32 i = 0; i < 8; i++)
                    words[i] = Read16(s_module + RotomTable + r.entry * RotomEntryBytes + i * 2);
                RotomEntryFromWords(words, s_entryLines);
                Text(r.entry, RotomMode::Choice, Line(s_entryLines.question));
                std::snprintf(s_text.answer[0], sizeof(s_text.answer[0]), "%s", Line(s_entryLines.answer[0]).c_str());
                std::snprintf(s_text.answer[1], sizeof(s_text.answer[1]), "%s", Line(s_entryLines.answer[1]).c_str());
            }
            CopyText(view, s_text);
            std::memcpy(view.answer, s_text.answer, sizeof(view.answer));
            view.cursor = r.entry == s_entry ? s_view.cursor : 0;
            view.busy = s_frames < s_busyUntil;
            return view;
        }

        const RotoLotoStep step = RotoLotoStepFrom(r.loto);
        if (step != RotoLotoStep::None)
        {
            view.mode = RotomMode::Loto;
            view.loto = step;
            view.busy = s_frames < s_busyUntil && r.loto == s_lotoTapped;
            const int key = 1000 + (int)step * 16 + (r.lotoPower + 1);
            if (key != s_textKey || s_text.mode != RotomMode::Loto)
            {
                std::string text;
                if (step == RotoLotoStep::Intro)
                    text = Line(LotoIntroLine);
                else if (step == RotoLotoStep::Result)
                    text = Line(LotoResultLine);
                else if (step == RotoLotoStep::Power && r.lotoPower >= 0)
                    text = Line(Read16(s_module + PowerTable + r.lotoPower * PowerEntryBytes + PowerDescription));
                else if (step == RotoLotoStep::Spinning)
                    text = "The reel is spinning!";
                Text(key, RotomMode::Loto, text);
            }
            CopyText(view, s_text);
            return view;
        }

        if (s_frames < s_replyUntil && !s_replyText.empty())
        {
            view.mode = RotomMode::Reply;
            CopyText(view, Text(2000 + (int)(s_replyUntil % 1000), RotomMode::Reply, s_replyText));
            return view;
        }

        if (r.ready && !MapIsOpen() && !ActionRunning())
            view.mode = RotomMode::Ready;
        return view;
    }

    void RotomPromptUpdate(u32 sceneManager, const std::string &scene, int fieldState, std::vector<std::string> &log)
    {
        s_frames++;
        const bool inField = scene == SceneField;
        const bool menu = inField && fieldState == FieldStateMenu;
        const RotomReading before = s_reading;
        s_reading = inField && s_enabled ? Read(sceneManager, log) : RotomReading();
        const RotomReading &r = s_reading;

        // The answer was taken: Rotom replies (if the chat has a reply).
        if (before.entry >= 0 && before.answers == 2 && !(r.entry == before.entry && r.answers == 2))
        {
            if (s_picked >= 0 && before.entry == s_entry)
            {
                log.push_back(Utils::Format("rotom: answered %d", s_picked + 1));
                const u16 reply = s_entryLines.reply[s_picked];
                s_replyText = reply != RotomNoLine ? Line(reply) : "";
                s_replyUntil = s_replyText.empty() ? 0 : s_frames + ReplyFrames;
            }
            else
                log.push_back("rotom: the chat ended without the native prompt");
            s_picked = -1;
            s_busyUntil = 0;
        }
        if (s_busyUntil != 0 && s_frames >= s_busyUntil && !ActionRunning())
        {
            if (r.entry >= 0 && r.answers == 2 && s_picked >= 0)
                log.push_back(Utils::Format("rotom: answer %d not taken yet, A again", s_picked + 1));
            s_busyUntil = 0;
        }
        if (r.loto != before.loto)
        {
            log.push_back(Utils::Format("rotom: roto loto state %u", r.loto));
            s_busyUntil = 0;
        }
        if (s_talkPending && !ActionRunning())
        {
            s_talkPending = false;
            if (!r.ready || r.entry >= 0 || r.loto != 0)
                log.push_back("rotom: talk started");
        }

        RotomView view = MakeView(inField, menu);
        if (view.mode == RotomMode::Choice)
            s_entry = r.entry;
        else if (r.entry < 0)
            s_entry = -1;

        // A box (not the badge) takes the screen: the map steps aside and
        // comes back after; the panel is held off unless the player turns it on.
        const bool box = view.mode == RotomMode::Choice || view.mode == RotomMode::Loto || view.mode == RotomMode::Reply;
        if (box && MapIsOpen())
        {
            MapClose("Rotom", log);
            s_reopenMap = true;
        }
        if (!box && s_reopenMap)
        {
            s_reopenMap = false;
            if (inField && !menu)
                MapRequestOpen("Rotom");
        }
        if (!box)
            s_yield = false;
        bool holdOff = box && !s_yield;
        if (holdOff && s_forcedOff && GetPanelMode() != PanelMode::Off)
        {
            s_yield = true;
            holdOff = false;
            log.push_back("rotom: panel turned on by hand, prompt hidden until it ends");
        }
        if (holdOff)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("rotom: prompt up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            s_forcedOff = false;
            if (!menu)
                AutoPanelSetEnabled(AutoPanelEnabled());
        }
        if (s_yield)
            view.mode = RotomMode::None;

        // Keys.
        const u32 mask = view.mode == RotomMode::Choice ? ChoiceKeys : view.mode == RotomMode::Loto ? LotoKeys : 0;
        if (mask != 0 && !ActionRunning())
        {
            u32 pressed = CaptureRealPressed();
            if (Controller::IsKeysPressed(TalkKey))
                pressed |= TalkKey;
            switch (RotomKeys(view, pressed, TalkKey))
            {
            case RotomKey::Up:     view.cursor = 0; break;
            case RotomKey::Down:   view.cursor = 1; break;
            case RotomKey::Answer: Answer(view, view.cursor, sceneManager, log); view.busy = s_busyUntil != 0; break;
            case RotomKey::Next:   LotoNext(view, sceneManager, log); view.busy = s_busyUntil != 0; break;
            default: break;
            }
        }
        else if (view.mode == RotomMode::Ready && !ActionRunning())
        {
            u32 pressed = CaptureRealPressed();
            if (Controller::IsKeysPressed(TalkKey))
                pressed |= TalkKey;
            if (RotomKeys(view, pressed, TalkKey) == RotomKey::Talk)
                Talk(TalkKeyName, sceneManager, log);
        }

        // Input capture: only while a box takes keys. The action layer
        // captures everything while it runs and gives ours back after.
        if (mask != 0 && !ActionRunning())
        {
            if (!s_capturing || s_captureMask != mask)
            {
                InputCaptureBegin(mask, false, false);
                s_capturing = true;
                s_captureMask = mask;
            }
        }
        else if (s_capturing && mask == 0 && !ActionRunning())
        {
            if (InputCaptureRequest() == s_captureMask)
                InputCaptureEnd();
            s_capturing = false;
        }

        // Log each new view once (not cursor moves, which log on their own).
        if (view.cursor != s_view.cursor && view.mode == RotomMode::Choice && s_view.mode == RotomMode::Choice)
            log.push_back(Utils::Format("rotom: cursor %d", view.cursor + 1));
        RotomView logged = view;
        logged.cursor = 0;
        logged.busy = false;
        const std::string line = DescribeRotomView(logged);
        if (line != s_lastLog)
        {
            s_lastLog = line;
            log.push_back("rotom: " + line);
        }

        if (view != s_view || s_viewVersion == 0)
        {
            s_view = view;
            Publish();
        }
        const bool shown = view.mode != RotomMode::None;
        if (shown && !s_shown)
            UI::RequestAssets();   // the game's font and icons; normally loaded at the first field already
        if (shown != s_shown)
        {
            log.push_back(shown ? "rotom: drawing" : "rotom: hidden");
            log.push_back(UI::ScreenBuffer::HeapLog(shown ? "rotom shown" : "rotom hidden"));
        }
        s_shown = shown;

    }

    bool RotomPromptCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        unsigned n = 0;
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(RotomPromptStatus());
        }
        else if (args == "status")
            log.push_back(RotomPromptStatus());
        else if (args == "read")
        {
            std::vector<std::string> scan;
            const RotomReading r = Read(sceneManager, scan);
            for (const std::string &l : scan)
                log.push_back(l);
            log.push_back(Utils::Format("rotom: applet %08lX found %d entry %d answers %lu ready %d loto %lu power %d",
                                        s_applet, r.applet, r.entry, r.answers, r.ready, r.loto, r.lotoPower));
        }
        else if (std::sscanf(args.c_str(), "select %u", &n) == 1 && (n == 1 || n == 2))
        {
            if (s_view.mode != RotomMode::Choice)
                log.push_back("rotom: select refused: no question is up");
            else
            {
                s_view.cursor = (int)n - 1;
                Publish();
                log.push_back(Utils::Format("rotom: cursor %u", n));
            }
        }
        else if (args == "answer" || (std::sscanf(args.c_str(), "answer %u", &n) == 1 && (n == 1 || n == 2)))
        {
            if (s_view.mode != RotomMode::Choice || s_view.busy || ActionRunning())
                log.push_back("rotom: answer refused: no question waiting for an answer");
            else
                Answer(s_view, args == "answer" ? s_view.cursor : (int)n - 1, sceneManager, log);
        }
        else if (args == "next")
        {
            if (s_view.mode != RotomMode::Loto || s_view.busy || ActionRunning())
                log.push_back("rotom: next refused: no Roto Loto step waiting");
            else
                LotoNext(s_view, sceneManager, log);
        }
        else if (args == "talk")
        {
            if (s_view.mode != RotomMode::Ready || ActionRunning())
                log.push_back("rotom: talk refused: Rotom doesn't want to talk now");
            else
                Talk("command", sceneManager, log);
        }
        else
            return false;
        return true;
    }

    std::string RotomPromptStatus(void)
    {
        return Utils::Format("rotom: %s, %s%s%s%s | applet %08lX entry %d answers %lu ready %d loto %lu", s_enabled ? "on" : "off",
                             DescribeRotomView(s_view).c_str(), s_shown ? ", shown" : "",
                             s_capturing ? ", input captured" : "", s_forcedOff ? ", holding panel off" : "", s_applet,
                             s_reading.entry, s_reading.answers, s_reading.ready, s_reading.loto);
    }
}
