#pragma once

#include <cstdint>

// USUM v1.2 engine internals. Main-image addresses and CRO-relative offsets.
// No CTRPF dependencies: patch data and host tests share these values.
namespace OneScreen
{
    namespace Usum
    {
        constexpr uint32_t GameManagerGlobal = 0x6A59C4; // GameSys::GameManager*
        constexpr uint32_t VtSaveData = 0x64F4A0;
        constexpr uint32_t VtMyItem = 0x64F3C4;
        constexpr uint32_t VtPokemonParam = 0x649374;
        constexpr uint32_t VtPartyScene = 0x647FCC;
        constexpr uint32_t VtStaticWork = 0x647F9C;
        constexpr uint32_t VtBagScene = 0x6488C8;
        constexpr uint32_t SpeciesMsgGlobal = 0x671088; // MsgData* (file 60)
        constexpr uint32_t MsgCacheTable = 0x6A7368; // 8 x {.., +8 u16 dat, +0xC MsgData*}
        constexpr uint32_t VtMsgData = 0x64B104; // gfl2::str::MsgData
        constexpr uint32_t EventMain = 0x404350;
        constexpr uint32_t AppCallStart = 0x37F54C;
        constexpr uint32_t BoxEventStart = 0x2DE91C;
        constexpr uint32_t AppCallVtable = 0x64BCD0;
        constexpr uint32_t BoxEventVtable = 0x647F58;
        constexpr uint32_t AppCallMain = 0x39268C;
        constexpr uint32_t BoxEventMain = 0x2DEC0C;
        constexpr uint32_t VtBoxLayout = 0x64F364; // Savedata::BOX
        constexpr uint32_t VtBoxPokemon = 0x64F044; // Savedata::BoxPokemon
        constexpr uint32_t BattleView = 0x10A528; // view object at scene +0x94
        constexpr uint32_t BattleMenuCommand = 0x10B598; // Fight / Pokémon / Bag / Run
        constexpr uint32_t BattleMenuTarget = 0x10B848; // SOS/Royal, research live verified
        constexpr uint32_t BattleMenuMoves = 0x10B724; // move list
        constexpr uint32_t BattleBusy = 0x10B43C; // under the menus: animations, messages
        constexpr uint32_t VtBattlePokeList = 0x648394;
        constexpr uint32_t BagFrame = 0x16E24;
        constexpr uint32_t BagChild = 0x16EB0;
        constexpr uint32_t BagWork = 0x16FF0;
        constexpr uint32_t BagPartyList = 0x16D98;
        constexpr uint32_t BattlePokemonPage = 0x10BCD0;
        constexpr uint32_t BattleQuestion = 0x10B3B0; // Battle CRO
        constexpr uint32_t BattlePerspective = 0x736D0;
        constexpr uint32_t BattleDivide = 0x73774;
        constexpr uint32_t UnloadAddress = 0x17376C;
        constexpr uint32_t UnloadState = 0x69EB28;
        constexpr uint32_t WazaView = 0x76D0; // WazaOshie CRO
        constexpr uint32_t KeyboardFrame = 0xBFE4; // the keyboard
        constexpr uint32_t KeyboardText = 0xBFC0; // text object, at frame +0x1C
        constexpr uint32_t ImageEnd = 0x6D6000;
        constexpr uint32_t TextAddress = 0x5D605C;
        constexpr uint32_t BattleMessageWait = 0x6358C;
        constexpr uint32_t BattleMessageWaitAlt = 0x63508;
        constexpr uint32_t BattleMessageCompare = 0x9BF28;
        constexpr uint32_t BattleHpStep = 0x2A27C;
        constexpr uint32_t BattleExpStep = 0x2A2D0;
        constexpr uint32_t FieldStateVtable = 0xD735C;
        constexpr uint32_t BagFieldFrame = 0x16F9C;
        constexpr uint32_t BagMenu = 0x6487C0;
        constexpr uint32_t LanguageByte = 0x671A10;
        constexpr uint32_t VtMapView = 0x64786C;
        constexpr uint32_t VtMapPane = 0x646B28;
        constexpr uint32_t ZukanContents = 0x34C14; // Zukan module offset of the data's vtable
        constexpr uint32_t VtZukanData = 0x64F550;
        constexpr uint32_t ZukanMagic = 0x2F120F17;
        constexpr uint32_t FormTable = 0x5D63DC;
        constexpr uint32_t ZukanTop = 0x34CD4;
        constexpr uint32_t ZukanList = 0x34DD8;
        constexpr uint32_t ZukanGrid = 0x34F68;
        constexpr uint32_t ZukanEntry = 0x34C24;
        constexpr uint32_t RotomApplet = 0x18284; // FieldMenu-relative
        constexpr uint32_t RotomTable = 0x164DC; // FieldMenu-relative: chat table, 0x9A x 16 bytes
        constexpr uint32_t PowerTable = 0x1645C; // FieldMenu-relative: Roto Loto powers, 11 x 8 bytes
        constexpr uint32_t VtMyStatus = 0x64F460; // Savedata::MyStatus (RTTI)
        constexpr uint32_t RidePagerFrame = 0x18420;
        constexpr uint32_t RidePagerList = 0x1848C;
        constexpr uint32_t VtConfigSave = 0x64F064; // Savedata::ConfigSave (RTTI)
        constexpr uint32_t VtMisc = 0x64F384; // Savedata::Misc (RTTI)
        constexpr uint32_t VtPlayTime = 0x64F480; // play time (hours u16 +4, minutes u8 +6)
        constexpr uint32_t VtMenuSave = 0x64F510;
        constexpr uint32_t PokeIconLookupFunc = 0x30CC1C;
        constexpr uint32_t VtMoveModelManager = 0x64C628;
        constexpr uint32_t VtPlayerModel = 0x64C538;
        constexpr uint32_t VtZoneLoader = 0x64BDD0;
        constexpr uint32_t FieldMenuMapFrame = 0x183A0; // FieldMenu-relative
        constexpr uint32_t PerspectiveAddress = 0x3631CC;
        constexpr uint32_t CalculateMtxAddress = 0x2A22EC;
        constexpr uint32_t OrthoTemplate = 0x5C3180;
        constexpr uint32_t OrthoTemplateAlt = 0x5E8468;
        constexpr uint32_t BoxCroLowBgDraw = 0x2F4D8; // App::Box::BoxLowBgDraw vtable
        constexpr uint32_t BoxCroIdle = 0x2F6F0;
        constexpr uint32_t BoxCroModal = 0x2F6B0;
        constexpr uint32_t BoxCroCarrying = 0x2FA80;
        constexpr uint32_t SaveDataBoxLayout = 0x3B98; // vtable 0x64F364 (N8Savedata3BOXE)
        constexpr uint32_t SaveDataBoxPokemon = 0x4184; // vtable 0x64F044 (N8Savedata10BoxPokemonE)
        constexpr uint32_t FieldMenuSaveFrame = 0x1814C;
        constexpr uint32_t FieldMenuReportDraw = 0x180B4;
        constexpr uint32_t FieldMenuSaveStateRead = 0x1CB0; // ldrb state, [r4,#E9]
        constexpr uint32_t FieldMenuSaveMainCompare = 0x1F28; // SaveMain: 1 still writing
        constexpr uint32_t FieldMenuSaveResultCompare = 0x1F44; // 0 success, otherwise error
        constexpr uint32_t FieldMenuReportDrawCtor = 0x222C; // ReportDraw constructor's vtable
        constexpr uint32_t FieldMenuReportDrawVtableEntry = 0x180E8;
        constexpr uint32_t FieldMenuReportDrawMain = 0x1C5C;
    }
}
