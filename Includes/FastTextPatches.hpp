#pragma once

#include "Usum.hpp"

#include "Settings.hpp"
#include <array>
#include <cstddef>

// Pure patch data and preflight, shared with host tests. All values are u32 words.
namespace OneScreen
{
    namespace FastTextPatches
    {
        constexpr uint32_t TextAddress = Usum::TextAddress;
        constexpr std::array<uint32_t, 24> TextStock = {
            1,0,0,0, 1,1,2,255, 2,1,0,0, 1,1,1,255, 2,2,4,255, 2,2,2,255
        };
        constexpr std::array<uint32_t, 24> TextInstant = {
            0,0,0,0, 255,255,255,255, 0,0,0,0,
            255,255,255,255, 255,255,255,255, 255,255,255,255
        };
        constexpr std::array<uint32_t, 5> BattleOffsets = {
            Usum::BattleMessageWait, Usum::BattleMessageWaitAlt, Usum::BattleMessageCompare, Usum::BattleHpStep, Usum::BattleExpStep
        };
        constexpr std::array<uint32_t, 5> BattleStock = {
            0xE3A02018, 0xE3A02018, 0xE3500018, 0xB2800001, 0xE2800002
        };
        inline unsigned MessageFrames(BattleSpeed speed)
        {
            return speed == BattleSpeed::Fast ? 8 : speed == BattleSpeed::Instant ? 1 : 24;
        }
        inline std::array<uint32_t, 5> BattleWords(BattleSpeed messages, BattleSpeed bars)
        {
            const unsigned frames = MessageFrames(messages);
            return { 0xE3A02000u | frames, 0xE3A02000u | frames, 0xE3500000u | frames,
                bars == BattleSpeed::Fast ? 0xB2800004u : bars == BattleSpeed::Instant ? 0xB1A00001u : BattleStock[3],
                bars == BattleSpeed::Fast ? 0xE2800008u : bars == BattleSpeed::Instant ? 0xE2800064u : BattleStock[4] };
        }

        // Read ALL words before writing any. Expected is stock at each new load,
        // then only values successfully written by this plugin (including partial failure).
        template <std::size_t N, class Reader>
        bool Preflight(const std::array<uint32_t, N> &offsets,
                       const std::array<uint32_t, N> &expected, Reader read, uint32_t &badOffset)
        {
            for (std::size_t i = 0; i < N; ++i)
            {
                uint32_t current = 0;
                if (!read(offsets[i], current) || current != expected[i])
                {
                    badOffset = offsets[i];
                    return false;
                }
            }
            return true;
        }
    }
}
