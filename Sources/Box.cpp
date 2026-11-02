#include "Box.hpp"

#include <3ds.h>
#include <CTRPluginFramework.hpp>

#include "ActionQueue.hpp"
#include "Party.hpp"
#include "Scene.hpp"

namespace OneScreen
{
    using namespace CTRPluginFramework;

    // USUM v1.2. Found and checked as described in docs/RE6-boxes.md.
    static const u32 GameManagerGlobal = 0x6A59C4;  // GameSys::GameManager*
    static const u32 ManagerGameData   = 0x24;      // -> GameSys::GameData
    static const u32 GameDataSaveData  = 0x04;      // -> Savedata::SaveData
    static const u32 VtSaveData        = 0x64F4A0;
    static const u32 VtBoxLayout       = 0x64F364;  // Savedata::BOX
    static const u32 VtBoxPokemon      = 0x64F044;  // Savedata::BoxPokemon
    static const u32 VtPokemonParam    = 0x649374;
    static const u32 ParamCore = 0x04, ParamCalc = 0x08;

    // App::Box::BoxLowBgDraw, one of the objects in the scene manager's
    // first eight words while the Box screen is up.
    static const u32 ManagerSlots   = 8;
    static const u32 DrawListener   = 0x8C;     // current input listener (its vtable gives the mode)
    static const u32 DrawParam      = 0xC0;     // -> the Box app's parameter block
    static const u32 DrawWorkRef    = 0xC4;     // -> {work*, ...}
    static const u32 DrawCursor     = 0xDC;     // u32 cursor index
    // Parameter block: +0 Savedata::BOX*, +4 Savedata::BoxPokemon*, +0x60 tray.
    static const u32 ParamLayout = 0x00, ParamPokemon = 0x04, ParamTray = 0x60;
    // Work: +0 the parameter block, +0x1C 6 PokemonParam* (the screen's party).
    static const u32 WorkParam = 0x00, WorkParty = 0x1C;

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

    static bool ReadBytes(u32 address, u8 *out, u32 size)
    {
        if (!Readable(address, size))
            return false;
        for (u32 i = 0; i < size; i += 4)
        {
            const u32 v = Read32(address + i);
            for (u32 k = 0; k < 4 && i + k < size; k++)
                out[i + k] = (u8)(v >> (k * 8));
        }
        return true;
    }

    BoxStorage ReadBoxStorage(void)
    {
        BoxStorage state;
        const u32 manager = Read32(GameManagerGlobal);
        const u32 data = Readable(manager, ManagerGameData + 4) ? Read32(manager + ManagerGameData) : 0;
        const u32 save = Readable(data, 8) ? Read32(data + GameDataSaveData) : 0;
        if (!Readable(save, SaveDataBoxPokemon + BoxPokemonData + BoxCount * BoxSlots * Pk7CoreSize)
            || Read32(save) != VtSaveData)
            return state;
        const u32 layout = save + SaveDataBoxLayout, pokemon = save + SaveDataBoxPokemon;
        if (Read32(layout) != VtBoxLayout || Read32(pokemon) != VtBoxPokemon)
            return state;
        state.layout = layout;
        state.pokemon = pokemon;
        state.unlocked = BoxUnlockedCount(Read8(layout + BoxLayoutUnlocked));
        state.valid = true;
        return state;
    }

    // The Box CRO's header, cached by the scene object's vtable (finding a
    // module walks back page by page).
    static u32 BoxModule(u32 sceneVtable)
    {
        static u32 s_vtable = 0, s_module = 0;
        if (sceneVtable != s_vtable)
        {
            s_vtable = sceneVtable;
            const u32 module = CroModuleOf(sceneVtable);
            s_module = module != 0 && CroModuleName(module) == "Box" ? module : 0;
        }
        return s_module;
    }

    BoxView ReadBoxView(u32 sceneManager)
    {
        BoxView view;
        const u32 manager = Read32(sceneManager);
        if (!Readable(manager, ManagerSlots * 4))
            return view;
        const u32 scene = Read32(manager);
        const u32 module = Readable(scene, 4) ? BoxModule(Read32(scene)) : 0;
        if (module == 0)
            return view;

        u32 draw = 0;
        for (u32 i = 0; i < ManagerSlots && draw == 0; i++)
        {
            const u32 object = Read32(manager + i * 4);
            if (Readable(object, DrawCursor + 4) && Read32(object) == module + BoxCroLowBgDraw)
                draw = object;
        }
        if (draw == 0)
            return view;

        const u32 param = Read32(draw + DrawParam);
        if (!Readable(param, ParamTray + 4))
            return view;
        const BoxStorage storage = ReadBoxStorage();
        if (!storage.valid || Read32(param + ParamLayout) != storage.layout
            || Read32(param + ParamPokemon) != storage.pokemon)
            return view;
        const u32 tray = Read32(param + ParamTray);
        if (tray >= BoxCount)
            return view;

        const u32 listener = Read32(draw + DrawListener);
        view.mode = Readable(listener, 4) ? BoxModeFromListener(Read32(listener) - module) : BoxMode::Unknown;
        view.draw = draw;
        view.cursor = Read32(draw + DrawCursor);
        view.tray = tray;
        view.valid = true;

        const u32 ref = Read32(draw + DrawWorkRef);
        const u32 work = Readable(ref, 4) ? Read32(ref) : 0;
        if (Readable(work, WorkParty + BoxPartySlots * 4) && Read32(work + WorkParam) == param)
        {
            view.partyValid = true;
            for (u32 i = 0; i < BoxPartySlots; i++)
            {
                const u32 p = Read32(work + WorkParty + i * 4);
                if (!Readable(p, 0x10) || Read32(p) != VtPokemonParam)
                    view.partyValid = false;
                view.party[i] = p;
            }
        }
        return view;
    }

    std::string BoxName(const BoxStorage &storage, u32 tray)
    {
        u16 units[BoxNameUnits] = {};
        if (storage.valid && tray < BoxCount)
            for (u32 i = 0; i < BoxNameUnits; i++)
                units[i] = Read16(storage.layout + BoxLayoutNames + tray * BoxNameUnits * 2 + i * 2);
        return BoxNameText(units, tray);
    }

    Pk7 ReadBoxMon(const BoxStorage &storage, u32 tray, u32 slot)
    {
        u8 core[Pk7CoreSize];
        if (!storage.valid || tray >= BoxCount || slot >= BoxSlots
            || !ReadBytes(storage.pokemon + BoxPokemonData + BoxSlotOffset(tray, slot), core, Pk7CoreSize))
            return Pk7();
        return DecodePk7(core, nullptr);
    }

    Pk7 ReadParam(u32 param)
    {
        u8 core[Pk7CoreSize], calc[Pk7CalcSize];
        if (!Readable(param, 0x10) || Read32(param) != VtPokemonParam
            || !ReadBytes(Read32(param + ParamCore), core, Pk7CoreSize))
            return Pk7();
        const bool haveCalc = ReadBytes(Read32(param + ParamCalc), calc, Pk7CalcSize);
        return DecodePk7(core, haveCalc ? calc : nullptr);
    }

    u32 BoxWorldFlags(u32 sceneManager)
    {
        switch (ReadBoxView(sceneManager).mode)
        {
        case BoxMode::Idle:     return WorldBoxIdle;
        case BoxMode::Modal:    return WorldBoxModal;
        case BoxMode::Carrying: return WorldBoxCarrying;
        default:                return 0;
        }
    }

    static std::string MonText(const Pk7 &mon)
    {
        if (mon.form == Pk7Form::Invalid)
            return mon.encryptionConstant == 0 && mon.species == 0 ? "-" : "unreadable";
        if (mon.species == 0)
            return "-";
        const std::string species = SpeciesName(mon.species);
        std::string out = (species.empty() ? Utils::Format("#%u", mon.species) : species)
                          + Utils::Format("(%u)", mon.species);
        const std::string label = BoxMonLabel(mon);
        if (label != species)
            out += " \"" + label + "\"";
        if (mon.calcValid)
            out += Utils::Format(" L%u", mon.level);
        if (mon.heldItem)
            out += " @" + ItemName(mon.heldItem);
        return out;
    }

    std::string DescribeBox(u32 sceneManager, int box)
    {
        const BoxStorage storage = ReadBoxStorage();
        if (!storage.valid)
            return "box: not found (no save loaded?)";
        const BoxView view = ReadBoxView(sceneManager);
        const u32 tray = box >= 1 && box <= (int)BoxCount ? (u32)box - 1 : view.valid ? view.tray : 0;

        std::string out = Utils::Format("box: %lu \"%s\", %lu of %lu unlocked (BoxPokemon %08lX)", tray + 1,
                                        BoxName(storage, tray).c_str(), storage.unlocked, BoxCount, storage.pokemon);
        u32 used = 0;
        for (u32 slot = 0; slot < BoxSlots; slot++)
        {
            const Pk7 mon = ReadBoxMon(storage, tray, slot);
            if (mon.form == Pk7Form::Invalid || mon.species == 0)
                continue;
            used++;
            out += Utils::Format("\n  %2lu %s", slot, MonText(mon).c_str());
        }
        out += Utils::Format("\n  %lu of %lu slots used", used, BoxSlots);
        if (!view.valid)
            return out + "\n  box screen: not open";
        out += Utils::Format("\n  box screen: box %lu, mode %s, cursor %lu, draw %08lX", view.tray + 1,
                             BoxModeName(view.mode), view.cursor, view.draw);
        if (view.partyValid)
        {
            out += "\n  box screen party:";
            for (u32 i = 0; i < BoxPartySlots; i++)
                out += " " + MonText(ReadParam(view.party[i]));
        }
        else
            out += "\n  box screen party: not found";
        return out;
    }
}
