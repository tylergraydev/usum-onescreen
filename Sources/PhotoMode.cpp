#include "PhotoMode.hpp"

namespace OneScreen
{
    const char *PhotoRefusal(const PhotoSafety &s)
    {
        if (s.saving) return "saving in progress";
        if (s.action) return "a plugin action is running";
        if (s.pcPending) return "a PC request is pending or open";
        if (s.alreadyPaused) return "another owner has paused the game";
        if (!s.supportedScene) return "only available in the field or battle";
        if (s.fieldBusy) return "the field is busy (menu, save, event or transition)";
        if (s.gpuTest) return "turn gpuquad test off first";
        return "";
    }

    bool PhotoEnter(PhotoState &state, const PhotoSafety &safety, uint64_t nowMs)
    {
        if (state.active || *PhotoRefusal(safety)) return false;
        state.active = true;
        state.enteredMs = nowMs;
        return true;
    }

    bool PhotoExit(PhotoState &state)
    {
        if (!state.active) return false;
        state.active = false;
        return true;
    }

    bool PhotoHintVisible(const PhotoState &state, uint64_t nowMs)
    {
        return state.active && nowMs - state.enteredMs < 2000;
    }
}

#ifdef __3DS__
#include <CTRPluginFramework.hpp>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include "Actions.hpp"
#include "Buttons.hpp"
#include "InputCapture.hpp"
#include "PcAnywhere.hpp"
#include "PcAnywhereRules.hpp"
#include "QuickSave.hpp"
#include "Settings.hpp"
#include "UI/GpuQuad.hpp"

// CTRPF's installed libctrpf (2026-05-22) exports these. Public Pause always
// acquires faded menu buffers in that build, even though its wrapper passes
// false. Reacquire from the saved game buffers without fading; never patch
// framework code or use guessed game addresses. See docs/PHOTO-MODE.md.
namespace CTRPluginFramework
{
    class ScreenImpl { public: static int AcquireFromGsp(bool); };
    class OSDImpl { public: static void UpdateScreens(void); };
}

namespace OneScreen
{
    using namespace CTRPluginFramework;
    static PhotoState s_photo;
    static std::atomic<bool> s_requested{false};
    static bool s_blockMenu = false;
    static std::atomic<bool> s_active{false};

    static uint64_t NowMs() { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000); }
    void PhotoModeRequest(bool on) { s_requested = on; }
    bool PhotoModeActive(void) { return s_active.load(); }
    bool PhotoModeCanOpenMenu(void)
    {
        if (Controller::GetKeysDown() == 0) s_blockMenu = false;
        return !s_active.load() && !s_blockMenu;
    }
    std::string PhotoModeStatus(void)
    {
        return std::string("photo: ") + (s_active.load() ? "on" : s_requested ? "pending" : "off");
    }

    static PhotoSafety Safety(uint32_t manager)
    {
        const auto w = ReadPcWorld(manager);
        PhotoSafety s;
        s.saving = QuickSaveRunning();
        s.action = ActionRunning();
        s.pcPending = w.pending;
        s.supportedScene = w.scene == "FieldRo" || w.scene == "Battle";
        s.fieldBusy = w.scene == "FieldRo" && (w.fieldState != 0 || w.eventRunning || w.festivalPlaza);
        s.alreadyPaused = Process::IsPaused();
        s.gpuTest = UI::GpuQuadCommand("status").find(", test on,") != std::string::npos;
        return s;
    }

    bool PhotoModeUpdate(uint32_t manager, void (*pollCommands)())
    {
        static u32 hits = 0;
        const u32 current = ButtonsPhotoComboHits();
        if (current != hits) { hits = current; s_requested = true; }
        if (!s_requested) return false;

        const PhotoSafety safety = Safety(manager);
        const char *why = PhotoRefusal(safety);
        if (*why)
        {
            s_requested = false;
            OSD::Notify(std::string("Photo mode: refused - ") + why);
            return true;
        }

        const u32 capture = InputCaptureRequest();
        InputCaptureBegin(~0u, true, true);
        const bool lightweight = OSD::LightweightMode;
        // Drain callbacks before hiding their GPU publications. Pause must
        // never run with the OSD lock held (the game needs it to stop).
        OSD::Lock();
        OSD::LightweightMode = true;
        OSD::Unlock();
        const u32 reads = ButtonsPhotoComboReads();
        // Let fresh game frames replace old composited images. This is entry
        // preparation only, with full capture and all plugin updates stopped.
        // Repeat hides because the GPU API's publication lock is nonblocking.
        for (int frame = 0; frame < 8; ++frame)
        {
            for (u32 slot = 0; slot < 4; ++slot) UI::HideGpuQuad(slot);
            svcSleepThread(16000000);
        }
        const bool primed = ButtonsPhotoComboReads() - reads >= 2;
        Process::Pause();
        // Recheck with game threads stopped: native events may have advanced
        // during entry preparation. Ignore only our own pause ownership.
        PhotoSafety pausedSafety = Safety(manager);
        pausedSafety.alreadyPaused = false;
        const int acquired = ScreenImpl::AcquireFromGsp(false);
        OSDImpl::UpdateScreens();
        if (!primed || acquired != 0 || *PhotoRefusal(pausedSafety))
        {
            s_requested = false;
            InputCaptureBegin(capture, (capture & CaptureStickBits) != 0, (capture & CaptureTouchBit) != 0);
            OSD::LightweightMode = lightweight;
            Process::Play();
            OSD::Notify("Photo mode: refused - frame/input unavailable or state changed");
            return true;
        }

        PhotoEnter(s_photo, safety, NowMs());
        s_active.store(true);
        // Snapshot only CTRPF-owned paused buffers, never the game's memory.
        const Screen &top = OSD::GetTopScreen();
        const Screen &bottom = OSD::GetBottomScreen();
        const size_t topBytes = top.Stride * 400, bottomBytes = bottom.Stride * 320;
        std::unique_ptr<u8[]> cleanTop(new (std::nothrow) u8[topBytes]);
        std::unique_ptr<u8[]> cleanRight(top.Is3DEnabled ? new (std::nothrow) u8[topBytes] : nullptr);
        std::unique_ptr<u8[]> cleanBottom(new (std::nothrow) u8[bottomBytes]);
        if (!cleanTop || !cleanBottom || (top.Is3DEnabled && !cleanRight))
        {
            s_requested = false;
            PhotoExit(s_photo);
            s_active.store(false);
            InputCaptureBegin(capture, (capture & CaptureStickBits) != 0, (capture & CaptureTouchBit) != 0);
            OSD::LightweightMode = lightweight;
            Process::Play();
            OSD::Notify("Photo mode: not enough memory for the frozen frame");
            return true;
        }
        std::memcpy(cleanTop.get(), (void *)top.LeftFramebuffer, topBytes);
        if (top.Is3DEnabled)
            std::memcpy(cleanRight.get(), (void *)top.RightFramebuffer, topBytes);
        std::memcpy(cleanBottom.get(), (void *)bottom.LeftFramebuffer, bottomBytes);
        const u32 combo = GetSettings().photoKey;
        PcComboState exitCombo;
        // Consume the entry chord. A new release/repress is required to exit.
        const u32 exitKey = combo ? combo : (u32)Key::B;
        PcComboRead(exitCombo, exitKey, Controller::GetKeysDown());
        const std::string hint = "Photo mode \xE2\x80\x94 " + KeysToString(exitKey) + " to exit";

        while (s_requested)
        {
            Controller::Update(); // direct HID, independent of game pad reads
            if (PcComboRead(exitCombo, exitKey, Controller::GetKeysDown())) s_requested = false;
            pollCommands(); // RunCommand rejects mutations while active
            const Screen &t = OSD::GetTopScreen();
            const Screen &b = OSD::GetBottomScreen();
            std::memcpy((void *)t.LeftFramebuffer, cleanTop.get(), topBytes);
            if (t.Is3DEnabled && t.RightFramebuffer != t.LeftFramebuffer)
                std::memcpy((void *)t.RightFramebuffer, cleanRight.get(), topBytes);
            std::memcpy((void *)b.LeftFramebuffer, cleanBottom.get(), bottomBytes);
            if (PhotoHintVisible(s_photo, NowMs())) t.DrawSysfont(hint, 8, 8);
            OSD::SwapBuffers();
            svcSleepThread(16000000);
        }
        // Both buffers lose the hint, including when exit is within two seconds.
        for (int i = 0; i < 2; ++i)
        {
            const Screen &t = OSD::GetTopScreen();
            std::memcpy((void *)t.LeftFramebuffer, cleanTop.get(), topBytes);
            if (t.Is3DEnabled && t.RightFramebuffer != t.LeftFramebuffer)
                std::memcpy((void *)t.RightFramebuffer, cleanRight.get(), topBytes);
            OSD::SwapBuffers();
        }
        s_blockMenu = true;
        PhotoExit(s_photo);
        s_active.store(false);
        // Capture was observed by the game's reader during entry preparation.
        // Removing it leaves every held key/stick/touch suppressed until up.
        InputCaptureBegin(capture, (capture & CaptureStickBits) != 0, (capture & CaptureTouchBit) != 0);
        OSD::LightweightMode = lightweight;
        Process::Play();
        hits = ButtonsPhotoComboHits();
        return true;
    }
}
#endif
