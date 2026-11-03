#include "Screens/Pokedex.hpp"

#include <cstring>
#include <new>

#include <CTRPluginFramework.hpp>

#include "AutoPanel.hpp"
#include "Panel.hpp"
#include "Party.hpp"
#include "Scene.hpp"
#include "Screens/PokedexList.hpp"
#include "UI/Assets.hpp"
#include "UI/Device.hpp"
#include "UI/Layer.hpp"
#include "UI/Theme.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2. Found and checked as described in docs/RE7-pokedex.md.
    static const char *ZukanModule = "Zukan";
    static const u32 ManagerFrame     = 0x08;       // scene manager +8: the Zukan app's current frame
    static const u32 FrameContents    = 0xA4;       // frame -> the app's shared data
    static const u32 FrameTopCursor   = 0x134;      // top level: u32 row (0-4 dexes, 5 Evaluation)
    static const u32 FrameTopState    = 0x19D;      // top level: u8, 7 = menu up
    static const u32 VtContents       = 0x34C14;    // Zukan module offset of the data's vtable
    static const u32 ContentsDex      = 0x11;       // u8: dex being shown (DexKind)
    static const u32 ContentsCount    = 0xA0;       // u16: records (403)
    static const u32 ContentsRecords  = 0xA4;       // -> records, RecordSize each
    static const u32 ContentsAlolaRows = 0xAC;      // u32: rows of the Alola list (to the highest seen)
    static const u32 ContentsCursor   = 0xB4;       // u32: index in the current list / grid
    static const u32 RecordSize = 0x1F0;            // u16 species, u16 number[5]
    static const u32 MaxRecords = 807;

    // Save data (docs/RE3-party-bag.md for the roots).
    static const u32 GameManagerGlobal = 0x6A59C4;
    static const u32 ManagerGameData = 0x24, GameDataSaveData = 0x04;
    static const u32 VtSaveData = 0x64F4A0;
    static const u32 SaveZukan = 0x23DC;            // Savedata::ZukanData
    static const u32 VtZukanData = 0x64F550, ZukanMagic = 0x2F120F17;
    static const u32 SaveEventFlags = 0x15D8 + 0x7D4;   // EventWork +0x7D4: flag bits (flags < 0x1340)
    static const u32 FlagIslandDex = 0x101F;        // + island - 1: that island's Pokédex is open
    static const u32 FlagEvaluation = 0xFE7;        // the Evaluation row is there
    // The game's form table (read by 0x4C7664 / 0x4C7FD0 through 0x4C8068).
    static const u32 FormTable = 0x5D63DC;
    static const int FormTableCount = 0x7B;

    // Frames the native screen must have nothing to show before the panel
    // hold ends (a list opening or closing passes through other states).
    static const u32 ReleaseFrames = 20;
    // On entering the Pokédex, how long to hold the panel off before its
    // first screen shows (the app fills its records first).
    static const u32 ArriveFrames = 90;

    // --- Shared with the OSD callback (game thread) ---
    static Mutex s_drawLock;
    static DexView s_drawView;
    static volatile bool s_visible = false;
    static bool s_dirty = false;
    static u8 *s_pixels = nullptr;      // allocated the first time the screen shows
    static const size_t PixelBytes = PokedexMaxW * PokedexMaxH * 4;
    static UI::Layer s_layer;
    static UI::Rect s_layerArea = {};

    // --- Plugin thread ---
    static bool s_enabled = true;
    static bool s_forcedOff = false;    // we hold the panel off (AutoPanel manual override)
    static bool s_yield = false;        // the player turned the panel on: hidden until the Pokédex closes
    static u32  s_frames = 0;
    static u32  s_holdUntil = 0;        // frame the panel hold ends, unless something shows

    // What was read from the game.
    struct DexData
    {
        bool valid = false;
        u32 contents = 0;
        std::vector<DexRecord> records;
        u32 alolaRows = 0;
        std::vector<DexRow> rows[DexKinds];
        DexCounts counts[DexKinds];
        bool open[DexKinds] = {};
        bool evaluation = false;
    };
    static DexData s_data;
    static std::vector<std::string> s_names;    // species name cache (seen ones), by species

    static DexView s_view;              // what is shown now
    static std::string s_viewKey;       // its log line; a change is logged and redrawn
    static std::string s_lastWhy;

    static u8 Read8(u32 address)
    {
        u8 v = 0;
        Process::Read8(address, v);
        return v;
    }

    static u16 Read16(u32 address)
    {
        u16 v = 0;
        Process::Read16(address, v);
        return v;
    }

    static u32 Read32(u32 address)
    {
        u32 v = 0;
        Process::Read32(address, v);
        return v;
    }

    static bool Readable(u32 address, u32 size)
    {
        return address != 0 && Process::CheckAddress(address, MEMPERM_READ)
               && Process::CheckAddress(address + size - 1, MEMPERM_READ);
    }

    // --- OSD callback ---

    static bool PokedexOnScreen(const Screen &screen)
    {
        if (!screen.IsTop || !s_visible || s_pixels == nullptr)
            return false;
        UiInitFont();
        const UI::Surface fb = SurfaceFromScreen(screen);
        // Never wait for the plugin thread here (this is the game's frame):
        // while it hands over a new view, the old one is shown. (TryLock
        // returns true on failure.)
        if (!s_drawLock.TryLock())
        {
            const UI::Rect area = PokedexArea(s_drawView.screen);
            if (!s_layer.Matches(fb) || area.x != s_layerArea.x || area.y != s_layerArea.y
                || area.w != s_layerArea.w || area.h != s_layerArea.h)
            {
                s_layerArea = s_layer.Setup(fb, area, PokedexRadius, s_pixels, PixelBytes) ? area : UI::Rect{};
                s_dirty = true;
            }
            if (s_dirty && s_layerArea.w > 0)
            {
                UI::Canvas canvas = s_layer.Begin();
                DrawPokedex(canvas, s_drawView);
                s_dirty = false;
            }
            s_drawLock.Unlock();
        }
        if (s_layerArea.w > 0)
            s_layer.Present(fb);
        return true;
    }

    void StartPokedex(void)
    {
        OSD::Run(PokedexOnScreen);
    }

    // --- Reading the game ---

    static u32 SaveData(void)
    {
        const u32 manager = Read32(GameManagerGlobal);
        const u32 data = Readable(manager, ManagerGameData + 4) ? Read32(manager + ManagerGameData) : 0;
        const u32 save = Readable(data, 8) ? Read32(data + GameDataSaveData) : 0;
        return Readable(save, SaveZukan + DexZukanBytes) && Read32(save) == VtSaveData ? save : 0;
    }

    static bool EventFlag(u32 save, u32 flag)
    {
        return (Read8(save + SaveEventFlags + (flag >> 3)) >> (flag & 7)) & 1;
    }

    static const std::string &CachedName(u16 species)
    {
        if (s_names.size() <= MaxRecords)
            s_names.assign(MaxRecords + 1, std::string());
        std::string &name = s_names[species <= MaxRecords ? species : 0];
        if (name.empty() && species != 0 && species <= MaxRecords)
            name = SpeciesName(species);
        return name;
    }

    // The app's records and the save's flags; false (and why) if anything
    // doesn't check out.
    static bool LoadData(u32 contents, DexData &out, std::string &why)
    {
        out = DexData();
        const u32 count = Read16(contents + ContentsCount);
        const u32 records = Read32(contents + ContentsRecords);
        if (count == 0 || count > MaxRecords || !Readable(records, count * RecordSize))
        {
            why = Utils::Format("records %08lX x%lu not readable", records, count);
            return false;
        }
        out.records.resize(count);
        for (u32 i = 0; i < count; i++)
        {
            const u16 *r = (const u16 *)(records + i * RecordSize);
            DexRecord &rec = out.records[i];
            rec.species = r[0];
            for (int d = 0; d < DexKinds; d++)
                rec.number[d] = r[1 + d];
            // The app fills its records over the first frames after it opens.
            if (rec.species == 0)
            {
                why = "records not ready";
                return false;
            }
            if (rec.species > MaxRecords || rec.number[0] != i + 1)
            {
                why = Utils::Format("record %lu doesn't check out (species %u, Alola %u)", i, rec.species,
                                    rec.number[0]);
                return false;
            }
        }
        out.alolaRows = Read32(contents + ContentsAlolaRows);
        if (out.alolaRows == 0 || out.alolaRows > count)
            out.alolaRows = count;

        const u32 save = SaveData();
        const u32 zukan = save ? save + SaveZukan : 0;
        if (zukan == 0 || Read32(zukan) != VtZukanData || Read32(zukan + 4) != ZukanMagic
            || !Readable(FormTable, FormTableCount * 4))
        {
            why = "save dex flags not found";
            return false;
        }
        u8 flags[DexZukanBytes];
        std::memcpy(flags, (const void *)zukan, sizeof(flags));
        DexFormEntry forms[FormTableCount];
        std::memcpy(forms, (const void *)FormTable, sizeof(forms));
        if (forms[0].species == 0 || forms[0].forms < 2)
        {
            why = "form table doesn't check out";
            return false;
        }

        for (int d = 0; d < DexKinds; d++)
        {
            std::vector<DexRow> &rows = out.rows[d];
            rows = DexRows(out.records, (DexKind)d, d == 0 ? out.alolaRows : 0);
            for (DexRow &row : rows)
            {
                row.state = DexStateOf(flags, row.species, forms, FormTableCount);
                if (row.state != DexState::Unseen)
                    row.name = CachedName(row.species);
            }
            // Counts over the whole dex, not only the rows listed.
            std::vector<DexRow> all = d == 0 ? DexRows(out.records, DexKind::Alola, 0) : rows;
            for (DexRow &row : all)
                row.state = DexStateOf(flags, row.species, forms, FormTableCount);
            out.counts[d] = DexCount(all);
            out.open[d] = d == 0 || EventFlag(save, FlagIslandDex + d - 1);
        }
        out.evaluation = EventFlag(save, FlagEvaluation);
        out.contents = contents;
        out.valid = true;
        return true;
    }

    static std::string DataSummary(const DexData &data)
    {
        std::string out = Utils::Format("pokedex: data %u records, %s %u caught %u seen (%u rows)",
                                        (unsigned)data.records.size(), DexId(DexKind::Alola), data.counts[0].caught,
                                        data.counts[0].seen, (unsigned)data.rows[0].size());
        for (int d = 1; d < DexKinds; d++)
            out += Utils::Format(", %s %s %u/%u", DexId((DexKind)d), data.open[d] ? "open" : "locked",
                                 data.counts[d].caught, data.counts[d].seen);
        return out + (data.evaluation ? ", evaluation" : "");
    }

    // The Zukan app's frame and data. Screen None (and why) if it isn't up.
    struct DexFrame
    {
        DexScreen screen = DexScreen::None;
        u32 frame = 0, contents = 0, module = 0;
    };

    static DexFrame ReadFrame(u32 sceneManager, std::string &why)
    {
        DexFrame f;
        const u32 manager = Read32(sceneManager);
        f.frame = Readable(manager, ManagerFrame + 4) ? Read32(manager + ManagerFrame) : 0;
        const u32 vtable = Readable(f.frame, FrameTopState + 1) ? Read32(f.frame) : 0;
        static u32 s_vtable = 0, s_module = 0;
        if (vtable != s_vtable)
        {
            const u32 module = vtable ? CroModuleOf(vtable) : 0;
            s_module = module && CroModuleName(module) == ZukanModule ? module : 0;
            s_vtable = vtable;
        }
        f.module = s_module;
        if (f.module == 0)
        {
            why = "no Zukan frame";
            return f;
        }
        f.contents = Read32(f.frame + FrameContents);
        if (!Readable(f.contents, ContentsCursor + 4) || Read32(f.contents) != f.module + VtContents)
        {
            why = "frame data doesn't check out";
            f.contents = 0;
            return f;
        }
        f.screen = DexScreenFromVtable(vtable - f.module);
        return f;
    }

    // --- The view ---

    // Builds the view for what is up now; false (and why) if there is
    // nothing to show.
    static bool BuildView(const DexFrame &f, DexView &view, std::string &key, std::string &why)
    {
        view = DexView();
        view.screen = f.screen;
        for (int d = 0; d < DexKinds; d++)
        {
            view.open[d] = s_data.open[d];
            view.counts[d] = s_data.counts[d];
        }
        view.evaluation = s_data.evaluation;

        if (f.screen == DexScreen::Top)
        {
            const u8 state = Read8(f.frame + FrameTopState);
            if (state != DexTopMenuState)
            {
                why = Utils::Format("top level busy (state %u)", state);
                return false;
            }
            view.topCursor = (int)Read32(f.frame + FrameTopCursor);
            if (view.topCursor < 0 || view.topCursor >= DexTopRows)
            {
                why = Utils::Format("top cursor %d", view.topCursor);
                return false;
            }
            key = Utils::Format("pokedex: top cursor %s", view.topCursor == DexTopEvaluation
                                ? "evaluation" : DexId((DexKind)view.topCursor));
            return true;
        }
        if (f.screen != DexScreen::List && f.screen != DexScreen::Grid && f.screen != DexScreen::Entry)
        {
            why = Utils::Format("%s screen", DexScreenName(f.screen));
            return false;
        }

        const u8 dex = Read8(f.contents + ContentsDex);
        if (dex >= DexKinds || (f.screen == DexScreen::List && dex != 0) || (f.screen == DexScreen::Grid && dex == 0))
        {
            why = Utils::Format("dex %u in a %s", dex, DexScreenName(f.screen));
            return false;
        }
        view.dex = (DexKind)dex;
        const std::vector<DexRow> &rows = s_data.rows[dex];
        const int cursor = (int)Read32(f.contents + ContentsCursor);
        if (rows.empty() || cursor < 0 || cursor >= (int)rows.size())
        {
            why = Utils::Format("cursor %d of %u rows", cursor, (unsigned)rows.size());
            return false;
        }
        const int visible = f.screen == DexScreen::List ? DexListRows : f.screen == DexScreen::Grid ? DexGridRows : 1;
        view.window = MakeDexWindow(rows, cursor, visible);
        key = Utils::Format("pokedex: %s %s %d/%u %s", DexScreenName(f.screen), DexId(view.dex), cursor,
                            (unsigned)rows.size(), DescribeDexRow(rows[cursor]).c_str());
        return true;
    }

    static void Publish(bool visible)
    {
        s_drawLock.Lock();
        if (visible)
        {
            s_drawView = s_view;
            s_dirty = true;
        }
        s_visible = visible;
        s_drawLock.Unlock();
    }

    // --- Panel ---

    static void HoldPanel(bool hold, std::vector<std::string> &log)
    {
        if (hold && s_forcedOff && GetPanelMode() != PanelMode::Off)
        {
            // Turned on while we hold it off: the player's doing (L+R).
            s_yield = true;
            hold = false;
            log.push_back("pokedex: panel turned on by hand, native Pokedex hidden until it closes");
        }
        if (hold)
        {
            if (GetPanelMode() != PanelMode::Off)
            {
                SetPanelMode(PanelMode::Off);
                log.push_back("pokedex: native Pokedex up -> " + PanelStatus());
            }
            AutoPanelManualOverride();
            s_forcedOff = true;
        }
        else if (s_forcedOff)
        {
            // Clears the override: AutoPanel picks the mode for what is up.
            s_forcedOff = false;
            AutoPanelSetEnabled(AutoPanelEnabled());
        }
    }

    void PokedexUpdate(u32 sceneManager, const std::string &scene, std::vector<std::string> &log)
    {
        // Transitions (empty scene) don't count as leaving.
        static bool s_inDex = false;
        const bool inDex = scene == ZukanModule || (scene.empty() && s_inDex);
        if (!inDex)
        {
            if (s_inDex)
                log.push_back("pokedex: closed");
            s_yield = false;
            if (s_data.valid)
                s_data = DexData();
        }
        s_frames++;
        if (inDex && !s_inDex)
            s_holdUntil = s_frames + ArriveFrames;
        s_inDex = inDex;

        std::string why;
        DexFrame f;
        if (!s_enabled)
            why = "off";
        else if (!inDex)
            why = "not in the Pokedex";
        else if (s_yield)
            why = "panel on by hand";
        else
            f = ReadFrame(sceneManager, why);

        // Records and flags: once per visit, and again whenever the screen
        // changes (a catch or a new sighting is registered between visits).
        static DexScreen s_lastScreen = DexScreen::None;
        if (f.contents != 0 && (!s_data.valid || s_data.contents != f.contents || f.screen != s_lastScreen))
        {
            DexData data;
            const std::string before = s_data.valid ? DataSummary(s_data) : "";
            if (LoadData(f.contents, data, why))
            {
                s_data = data;
                const std::string summary = DataSummary(s_data);
                if (summary != before)
                    log.push_back(summary);
            }
            else
            {
                s_data = DexData();
                f.contents = 0;
            }
        }
        s_lastScreen = f.screen;

        DexView view;
        std::string key;
        const bool show = f.contents != 0 && s_data.valid && BuildView(f, view, key, why);

        if (show && s_holdUntil < s_frames + ReleaseFrames)
            s_holdUntil = s_frames + ReleaseFrames;
        HoldPanel(s_enabled && inDex && !s_yield && s_frames < s_holdUntil, log);

        if (show && s_pixels == nullptr)
        {
            s_pixels = new (std::nothrow) u8[PixelBytes];
            if (s_pixels == nullptr)
                log.push_back("pokedex: NO MEMORY for the layer");
        }

        if (show)
        {
            if (!s_visible || key != s_viewKey)
            {
                if (!s_visible)
                {
                    // The game's font and icons (normally loaded at the first
                    // field already).
                    UI::RequestAssets();
                    log.push_back(Utils::Format("pokedex: shown %s", DexScreenName(view.screen)));
                }
                log.push_back(key);
                s_view = view;
                s_viewKey = key;
                Publish(true);
            }
            // Redraw once the game's font has loaded (UI::Theme draws with
            // it from then on).
            static bool s_gameFont = false;
            if (UI::Theme::UsingGameFont() != s_gameFont)
            {
                s_gameFont = !s_gameFont;
                Publish(true);
            }
            s_lastWhy.clear();
        }
        else
        {
            if (s_visible)
            {
                Publish(false);
                s_viewKey.clear();
            }
            if (inDex && why != s_lastWhy)
                log.push_back("pokedex: hidden (" + why + ")");
            s_lastWhy = inDex ? why : "";
        }
    }

    bool PokedexCommand(const std::string &args, u32 sceneManager, std::vector<std::string> &log)
    {
        (void)sceneManager;
        if (args == "on" || args == "off")
        {
            s_enabled = args == "on";
            log.push_back(PokedexStatus());
        }
        else if (args == "status")
            log.push_back(PokedexStatus());
        else if (args == "rows")
        {
            if (!s_data.valid)
                log.push_back("pokedex: no data (not in the Pokedex?)");
            else
                for (int d = 0; d < DexKinds; d++)
                {
                    std::string line = Utils::Format("pokedex: %s %u rows:", DexId((DexKind)d),
                                                     (unsigned)s_data.rows[d].size());
                    for (const DexRow &row : s_data.rows[d])
                        if (row.state != DexState::Unseen)
                            line += " " + DescribeDexRow(row) + ",";
                    log.push_back(line);
                }
        }
        else
            return false;
        return true;
    }

    bool PokedexEnabled(void)
    {
        return s_enabled;
    }

    std::string PokedexStatus(void)
    {
        return Utils::Format("pokedex: %s, %s%s%s%s", s_enabled ? "on" : "off",
                             s_visible ? ("shown " + std::string(DexScreenName(s_view.screen))).c_str() : "hidden",
                             s_data.valid ? ", data loaded" : "", s_yield ? ", panel on by hand" : "",
                             s_forcedOff ? ", holding panel off" : "");
    }
}
