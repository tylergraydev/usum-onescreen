#pragma once

#include "Usum.hpp"

#include <cstdint>
#include <string>

namespace OneScreen
{
    namespace BattleWidescreen
    {
        constexpr uint32_t PerspectiveOffset = Usum::BattlePerspective;
        constexpr uint32_t DivideOffset = Usum::BattleDivide;
        constexpr uint32_t UnloadAddress = Usum::UnloadAddress;
        constexpr uint32_t UnloadWords[] = {
            0xE92D4070, 0xEE1D4F70, 0xE59F502C, 0xE3A0C000,
            0xE5A45080, 0xE2846004, 0xE5840014, 0xE59F001C,
            0xE886100E, 0xE5900000, 0xEF000032, 0xE2001102,
            0xE3510000, 0xA5940004, 0xE8BD8070, 0x000500C2, Usum::UnloadState
        };

        template<class Read>
        bool UnloadSignature(Read read)
        {
            for (unsigned i = 0; i < sizeof(UnloadWords) / sizeof(UnloadWords[0]); ++i)
            {
                uint32_t word = 0;
                if (!read(UnloadAddress + i * 4, word) || word != UnloadWords[i]) return false;
            }
            return true;
        }

        inline bool Address(uint32_t base, uint32_t &address)
        {
            if (base < Usum::ImageEnd || (base & 0xFFF) || base > UINT32_MAX - DivideOffset - 4)
                return false;
            address = base + PerspectiveOffset;
            return true;
        }

        template<class Read>
        bool Signature(uint32_t base, Read read)
        {
            uint32_t address = 0, word = 0;
            return Address(base, address)
                && read(address, word) && word == 0xE92D4030
                && read(address + 4, word) && word == 0xE1A05001
                && read(base + DivideOffset, word) && word == 0xEEC01AA9;
        }

        enum class Event { None, Installed, Removed, Mismatch, InstallFailed, RemoveFailed };

        // Caller serializes Update and BeforeUnload, including the actual unload.
        // Removal is NEVER deferred until an unmapped CRO is observed in Update.
        struct Lifetime
        {
            uint32_t active = 0, attempted = 0, retiring = 0;

            template<class Remove>
            bool BeforeUnload(uint32_t module, Remove remove)
            {
                if (module != active && module != attempted) return true;
                if (active && !remove()) return false;
                retiring = module;
                active = attempted = 0;
                return true;
            }

            template<class Read, class Install, class Remove>
            Event Update(uint32_t module, bool enabled, Read read, Install install, Remove remove)
            {
                bool removed = false;
                // A scene may still point at the retiring CRO during SDK cleanup.
                if (retiring && module != retiring) retiring = 0;
                if (module && module == retiring) return Event::None;
                if (active && (active != module || !enabled))
                {
                    if (!remove()) return Event::RemoveFailed;
                    active = 0;
                    removed = true;
                }
                if (!enabled || !module)
                {
                    attempted = 0;
                    return removed ? Event::Removed : Event::None;
                }
                if (active == module || attempted == module) return Event::None;
                attempted = module;
                if (!Signature(module, read)) return Event::Mismatch;
                uint32_t address = 0;
                Address(module, address);
                if (!install(address)) return Event::InstallFailed;
                active = module;
                return Event::Installed;
            }
        };
    }

    // Install the static pre-unload guard before permitting any CRO hook.
    std::string StartBattleWidescreen(uint32_t perspectiveCallback);
    std::string BattleWidescreenFrame(uint32_t sceneManager);
}
