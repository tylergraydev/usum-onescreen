#pragma once
#include <cstdint>
namespace OneScreen {
// Unknown rules must never be presented as a verified wild encounter.
enum class BattleKind : uint8_t { Unknown, Wild, Trainer, Royal };
// Battle+0x92C84: type 1 (or format 2) routes to the trainer Run refusal.
// Format 2 as Royal is inferred from its four independent clients; live check pending.
inline BattleKind DecodeBattleKind(uint8_t type, uint8_t style) {
    if (type == 255 || style > 3) return BattleKind::Unknown;
    if (style == 2) return BattleKind::Royal;
    if (type == 0) return BattleKind::Wild;
    if (type == 1 || type >= 4) return BattleKind::Trainer;
    return BattleKind::Unknown;
}
struct BattleTargetRef { uint8_t client, record; };
// Client lists include fainted history (and, on our side, benched Pokemon).
template<class Mon> inline bool LiveBattleRecord(const Mon &mon) {
    return mon.address && mon.species >= 1 && mon.species <= 807
        && mon.hp > 0 && mon.hp <= mon.maxHp;
}
// Follow identity through SOS reorder; never retain a fainted selection.
template<class Mon> inline uint8_t LiveFoeRecord(const Mon (&mons)[6], uint32_t preferred) {
    uint8_t first = 0xFF;
    for (uint8_t i = 0; i < 6; i++) {
        if (!LiveBattleRecord(mons[i])) continue;
        if (mons[i].address == preferred) return i;
        if (first == 0xFF) first = i;
    }
    return first;
}
inline bool TargetOccupied(BattleKind kind, uint8_t format, uint8_t plate) {
    // SOS has no own ally; single trainer battles also have a bench at r1.
    return plate != 3 || kind == BattleKind::Royal
        || (kind == BattleKind::Trainer && format == 1);
}
inline BattleTargetRef TargetRecord(BattleKind kind, uint8_t plate) {
    if (kind == BattleKind::Royal) {
        constexpr uint8_t clients[] = {3, 2, 0, 1};
        return {clients[plate < 4 ? plate : 2], 0};
    }
    constexpr uint8_t clients[] = {1, 1, 0, 0};
    constexpr uint8_t records[] = {1, 0, 0, 1};
    const uint8_t safe = plate < 4 ? plate : 2;
    return {clients[safe], records[safe]};
}
// Conservative known spread attacks; unknown moves keep ordinary target choice.
inline bool IsSpreadMove(unsigned move) {
    switch (move) {
    case 13: case 39: case 43: case 45: case 51: case 57: case 59: case 75:
    case 81: case 89: case 129: case 153: case 157: case 196: case 222:
    case 230: case 239: case 257: case 284: case 304: case 323: case 330:
    case 435: case 436: case 482: case 523: case 539: case 545: case 572:
    case 586: case 591: case 605: case 614: case 617: case 618: case 619:
    case 691: case 720: return true;
    default: return false;
    }
}
inline bool BattleCanRun(BattleKind kind) { return kind != BattleKind::Trainer && kind != BattleKind::Royal; }
inline bool BattleHasBag(BattleKind kind) { return kind != BattleKind::Royal; }
inline unsigned CaughtByte(unsigned species) { return (species - 1) >> 3; }
inline unsigned CaughtMask(unsigned species) { return 1u << ((species - 1) & 7); }
inline bool CaughtBit(unsigned species, uint8_t byte) {
    return species >= 1 && species <= 807 && (byte & CaughtMask(species)) != 0;
}
}
