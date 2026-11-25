#include "Usum.hpp"
#include "UI/Assets.hpp"

#include <atomic>
#include <malloc.h>
#include <new>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

namespace OneScreen
{
namespace UI
{
    using namespace CTRPluginFramework;

    // Current text language (0-9, 2 = English), as in Screens/Map.cpp.
    static const u32 LanguageByte = Usum::LanguageByte;

    static std::atomic<int> s_state{ (int)AssetsState::NotStarted };
    static Task *s_task = nullptr;
    static GameAssets *s_assets = nullptr;      // kept for the plugin's life once loaded

    // Written by the Task, read by the plugin thread once the state is
    // Ready or Failed.
    static std::vector<std::string> s_taskLog;
    static u32 s_loadMs = 0;
    static u32 s_keptBytes = 0;                 // computed from the parts
    static s32 s_heapDelta = 0;                 // heap in use after - before

    static bool s_logged = false;               // plugin thread

    static u32 KeptBytes(const GameAssets &a)
    {
        size_t n = a.font.Bytes();
        for (const Image &image : a.icons)
            n += image.rgba.capacity();
        for (const Image &image : a.balls)
            n += sizeof(Image) + image.rgba.capacity();
        n += a.moves.capacity() * sizeof(MoveInfo);
        for (const std::string &s : a.typeNames)
            n += sizeof(std::string) + s.capacity();
        for (const std::string &s : a.moveDescriptions)
            n += sizeof(std::string) + s.capacity();
        for (const std::string &s : a.moveNames)
            n += sizeof(std::string) + s.capacity();
        return (u32)n;
    }

    static s32 LoadTask(void *)
    {
        const s32 heapBefore = mallinfo().uordblks;
        const u64 start = svcGetSystemTick();
        u8 lang = 0xFF;
        Process::Read8(LanguageByte, lang);
        static const u32 lowPath[3] = { 0, 0, 0 };
        const FS_Path archive = { PATH_EMPTY, 1, "" };
        const FS_Path file = { PATH_BINARY, sizeof(lowPath), lowPath };
        Handle handle = 0;
        const Result res = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
        if (R_FAILED(res))
        {
            s_taskLog.push_back(Utils::Format("assets: can't open the RomFS (0x%08lX), fallback look", (u32)res));
            s_state.store((int)AssetsState::Failed, std::memory_order_release);
            return 0;
        }
        GameAssets *assets = new (std::nothrow) GameAssets();
        if (assets == nullptr)
        {
            FSFILE_Close(handle);
            s_taskLog.push_back("assets: no memory, fallback look");
            s_state.store((int)AssetsState::Failed, std::memory_order_release);
            return 0;
        }
        const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
            u32 got = 0;
            return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
        };
        LoadGameAssets(read, lang, *assets, s_taskLog);
        FSFILE_Close(handle);
        s_loadMs = (u32)((svcGetSystemTick() - start) / CPU_TICKS_PER_MSEC);
        s_keptBytes = KeptBytes(*assets);
        s_heapDelta = (s32)mallinfo().uordblks - heapBefore;
        s_assets = assets;
        __sync_synchronize();   // the set is complete before any thread can see it
        PublishGameAssets(assets);
        s_state.store((int)AssetsState::Ready, std::memory_order_release);
        return 0;
    }

    AssetsState AssetsStatus(void)
    {
        return (AssetsState)s_state.load(std::memory_order_acquire);
    }

    const char *AssetsStateName(AssetsState state)
    {
        switch (state)
        {
            case AssetsState::NotStarted: return "not started";
            case AssetsState::Loading:    return "loading";
            case AssetsState::Ready:      return "ready";
            case AssetsState::Failed:     return "failed";
        }
        return "?";
    }

    const GameAssets *Assets(void)
    {
        return AssetsStatus() == AssetsState::Ready ? s_assets : nullptr;
    }

    bool AssetsReady(void)
    {
        return Assets() != nullptr;
    }

    const GameFont *GameFontOrNull(void)
    {
        const GameAssets *a = Assets();
        return a != nullptr && a->font.Ready() ? &a->font : nullptr;
    }

    bool HaveGameFont(void)
    {
        return GameFontOrNull() != nullptr;
    }

    const Image *AssetIcon(GameIcon icon)
    {
        const GameAssets *a = Assets();
        return a != nullptr ? a->Icon(icon) : nullptr;
    }

    const Image *AssetBall(u16 ballItem)
    {
        const GameAssets *a = Assets();
        return a != nullptr ? a->Ball(ballItem) : nullptr;
    }

    MoveInfo AssetMove(u16 move)
    {
        const GameAssets *a = Assets();
        return a != nullptr ? a->Move(move) : MoveInfo();
    }

    std::string AssetMoveDescription(u16 move)
    {
        const GameAssets *a = Assets();
        return a != nullptr && move < a->moveDescriptions.size() ? a->moveDescriptions[move] : std::string();
    }

    std::string AssetMoveName(u16 move, const std::string &fallback)
    {
        const GameAssets *a = Assets();
        const std::string name = a != nullptr ? a->MoveName(move) : "";
        return name.empty() ? fallback : name;
    }

    std::string AssetTypeName(u8 type, const std::string &fallback)
    {
        const GameAssets *a = Assets();
        const std::string name = a != nullptr ? a->TypeName(type) : "";
        return name.empty() ? fallback : name;
    }

    void RequestAssets(void)
    {
        if (AssetsStatus() != AssetsState::NotStarted)
            return;
        s_task = new (std::nothrow) Task(LoadTask, nullptr, -1);
        if (s_task == nullptr)
        {
            s_taskLog.push_back("assets: no memory for the load task, fallback look");
            s_state.store((int)AssetsState::Failed, std::memory_order_release);
            return;
        }
        s_state.store((int)AssetsState::Loading, std::memory_order_release);
        s_task->Start();
    }

    void AssetsUpdate(const std::string &scene, std::vector<std::string> &log)
    {
        const AssetsState state = AssetsStatus();
        if (state == AssetsState::NotStarted)
        {
            if (scene == "FieldRo")
            {
                log.push_back("assets: loading (field reached)");
                RequestAssets();
            }
            return;
        }
        if (s_logged || (state != AssetsState::Ready && state != AssetsState::Failed))
            return;
        s_logged = true;
        log.insert(log.end(), s_taskLog.begin(), s_taskLog.end());
        s_taskLog.clear();
        s_taskLog.shrink_to_fit();
        if (state == AssetsState::Ready)
            log.push_back(AssetsSummary());
    }

    std::string AssetsSummary(void)
    {
        const AssetsState state = AssetsStatus();
        std::string s = std::string("assets: ") + AssetsStateName(state);
        if (state != AssetsState::Ready)
            return s;
        return s + Utils::Format(", loaded in %lu ms, %lu KB kept (heap +%ld KB), game font %s", s_loadMs,
                                 s_keptBytes / 1024, s_heapDelta / 1024, HaveGameFont() ? "on" : "off");
    }
}
}
