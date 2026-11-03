#include "Screens/ThemeScreen.hpp"

#include <cstdio>
#include <new>

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "Party.hpp"
#include "RomfsText.hpp"
#include "UI/Device.hpp"
#include "UI/GameAssets.hpp"
#include "UI/Layer.hpp"
#include "UI/ThemeDemo.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // Current text language (0-9, 2 = English), as in Screens/Map.cpp.
    static const u32 LanguageByte = 0x671A10;
    // Party re-read while the demo shows, every this many frames.
    static const u32 ReadEvery = 30;

    // --- Shared with the OSD callback (game thread) ---
    static volatile bool s_shown = false;
    static UI::ThemeDemoData s_datas[2];
    static volatile u32 s_dataSlot = 0;
    static volatile u32 s_version = 0;      // data, page or selection changed
    static volatile s32 s_page = 0;
    static volatile s32 s_selected = 0;

    // --- Plugin thread ---
    static UI::GameAssets *s_assets = nullptr;  // kept for the plugin's life once loaded
    static bool s_loadRequested = false;
    static u32  s_frames = 0;
    static UI::ThemeDemoData s_data;

    // --- OSD callback ---

    static UI::Layer s_layer;
    static std::vector<u8> s_pixels;
    static u32 s_drawnVersion = ~0u;

    static bool ThemeOnScreen(const Screen &screen)
    {
        if (!screen.IsTop || !s_shown)
            return false;
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        bool redraw = s_version != s_drawnVersion;
        if (!s_layer.Matches(fb))
        {
            s_pixels.resize(UI::Layer::BytesFor(fb, UI::ThemeDemoArea));
            if (!s_layer.Setup(fb, UI::ThemeDemoArea, 0, s_pixels.data(), s_pixels.size()))
                return false;
            redraw = true;
        }
        if (redraw)
        {
            const u32 version = s_version;
            UI::Canvas canvas = s_layer.Begin();
            UI::DrawThemeDemo(canvas, s_datas[s_dataSlot], s_page, s_selected);
            s_drawnVersion = version;
        }
        s_layer.Present(fb);
        return true;
    }

    void StartThemeScreen(void)
    {
        OSD::Run(ThemeOnScreen);
    }

    // --- Plugin thread ---

    static void Publish(void)
    {
        const u32 next = s_dataSlot ^ 1;
        s_datas[next] = s_data;
        s_dataSlot = next;
        s_version = s_version + 1;
    }

    static void Load(std::vector<std::string> &log)
    {
        s_loadRequested = false;
        if (s_assets != nullptr)
            return;
        u8 lang = 0xFF;
        Process::Read8(LanguageByte, lang);
        static const u32 lowPath[3] = { 0, 0, 0 };
        const FS_Path archive = { PATH_EMPTY, 1, "" };
        const FS_Path file = { PATH_BINARY, sizeof(lowPath), lowPath };
        Handle handle = 0;
        const Result res = FSUSER_OpenFileDirectly(&handle, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
        if (R_FAILED(res))
        {
            log.push_back(Utils::Format("uitheme: can't open the RomFS (0x%08lX), fallback look", (u32)res));
            return;
        }
        const RomfsRead read = [handle](uint64_t offset, void *dst, u32 size) {
            u32 got = 0;
            return R_SUCCEEDED(FSFILE_Read(handle, &got, offset, dst, size)) && got == size;
        };
        UI::GameAssets *assets = new (std::nothrow) UI::GameAssets();
        if (assets == nullptr)
        {
            FSFILE_Close(handle);
            log.push_back("uitheme: no memory for the assets");
            return;
        }
        const u64 start = svcGetSystemTick();
        UI::LoadGameAssets(read, lang, *assets, log);
        const u64 ticks = svcGetSystemTick() - start;
        FSFILE_Close(handle);
        s_assets = assets;
        UI::PublishGameAssets(assets);
        log.push_back(Utils::Format("uitheme: assets loaded in %lu ms", (u32)(ticks / CPU_TICKS_PER_MSEC)));
    }

    // The demo's data: the party and the lead Pokémon's moves, read from the
    // game; the made-up sample where something can't be read.
    static UI::ThemeDemoData ReadDemoData(u32 sceneManager)
    {
        UI::ThemeDemoData d = UI::ThemeDemoSample();
        const PartyState party = ReadParty(sceneManager);
        if (!party.valid || party.count == 0)
            return d;
        d.partyCount = 0;
        for (u32 i = 0; i < party.count && i < 6; i++)
        {
            const Pk7 &mon = party.slots[i].mon;
            if (mon.form == Pk7Form::Invalid)
                continue;
            UI::ThemeDemoMon &out = d.party[d.partyCount++];
            char name[64];
            Utf16ToUtf8(mon.nickname, 13, name, sizeof(name));
            out.name = name[0] != 0 ? name : SpeciesName(mon.species);
            out.level = mon.level;
            out.hp = mon.hp;
            out.maxHp = mon.maxHp;
        }
        const Pk7 &lead = party.slots[0].mon;
        if (s_assets == nullptr || lead.form == Pk7Form::Invalid || s_assets->moves.empty())
            return d;
        d.moveCount = 0;
        for (u32 k = 0; k < 4; k++)
        {
            const u16 move = lead.moves[k];
            if (move == 0)
                continue;
            const UI::MoveInfo info = s_assets->Move(move);
            UI::ThemeDemoMove &out = d.moves[d.moveCount++];
            out.name = s_assets->MoveName(move);
            out.type = info.type;
            out.typeName = s_assets->TypeName(info.type);
            out.category = info.category;
            out.pp = lead.pp[k];
            out.maxPp = info.pp;  // base PP: PP Ups are not counted
        }
        return d;
    }

    void ThemeScreenUpdate(u32 sceneManager, std::vector<std::string> &log)
    {
        s_frames++;
        if (s_loadRequested)
        {
            Load(log);
            s_frames = 0;
        }
        if (s_shown && s_frames % ReadEvery == 0)
        {
            const UI::ThemeDemoData data = ReadDemoData(sceneManager);
            bool same = data.moveCount == s_data.moveCount && data.partyCount == s_data.partyCount;
            for (s32 i = 0; same && i < data.moveCount; i++)
                same = data.moves[i].name == s_data.moves[i].name && data.moves[i].pp == s_data.moves[i].pp;
            for (s32 i = 0; same && i < data.partyCount; i++)
                same = data.party[i].name == s_data.party[i].name && data.party[i].hp == s_data.party[i].hp;
            if (!same || s_version == 0)
            {
                s_data = data;
                Publish();
            }
        }
    }

    static std::string Status(void)
    {
        std::string s = Utils::Format("uitheme: %s, page %ld, selected %ld, ", s_shown ? "shown" : "off", s_page, s_selected);
        if (s_assets == nullptr)
            return s + "assets not loaded (fallback look)";
        s += std::string("game font ") + (s_assets->font.Ready() ? "on" : "off") + "\n";
        return s + s_assets->status;
    }

    bool ThemeScreenCommand(const std::string &args, std::vector<std::string> &log)
    {
        int n = 0;
        if (args == "on")
        {
            s_loadRequested = s_assets == nullptr;
            s_shown = true;
            s_frames = ReadEvery - 1;   // read the party on the next frame
            s_version = s_version + 1;
            log.push_back("uitheme: on");
        }
        else if (args == "off")
        {
            s_shown = false;
            log.push_back("uitheme: off");
        }
        else if (args == "load")
            s_loadRequested = s_assets == nullptr;
        else if (args == "status")
            log.push_back(Status());
        else if (std::sscanf(args.c_str(), "page %d", &n) == 1)
        {
            s_page = n;
            s_version = s_version + 1;
            log.push_back(Utils::Format("uitheme: page %d", n));
        }
        else if (std::sscanf(args.c_str(), "select %d", &n) == 1)
        {
            s_selected = n;
            s_version = s_version + 1;
            log.push_back(Utils::Format("uitheme: select %d", n));
        }
        else
            return false;
        return true;
    }
}
