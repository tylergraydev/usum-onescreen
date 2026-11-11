#include "BattleWidescreen.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <vector>

namespace B = OneScreen::BattleWidescreen;

struct Fake
{
    B::Lifetime state;
    std::map<uint32_t, uint32_t> memory;
    std::vector<uint32_t> installs, removes;
    uint32_t target = 0;
    bool installOK = true, removeOK = true;

    void Load(uint32_t base)
    {
        memory[base + B::PerspectiveOffset] = 0xE92D4030;
        memory[base + B::PerspectiveOffset + 4] = 0xE1A05001;
        memory[base + B::DivideOffset] = 0xEEC01AA9;
    }
    bool Read(uint32_t a, uint32_t &w)
    {
        auto it = memory.find(a);
        if (it == memory.end()) return false;
        w = it->second;
        return true;
    }
    bool Remove()
    {
        // A remove after unmap must fail the test, even when removal fails.
        assert(memory.count(target));
        removes.push_back(target);
        if (!removeOK) return false;
        memory[target] = 0xE92D4030;
        target = 0;
        return true;
    }
    bool Unload(uint32_t base)
    {
        if (!state.BeforeUnload(base, [&] { return Remove(); })) return false;
        memory.erase(base + B::PerspectiveOffset);
        memory.erase(base + B::PerspectiveOffset + 4);
        memory.erase(base + B::DivideOffset);
        return true;
    }
    B::Event Frame(uint32_t base, bool enabled = true)
    {
        return state.Update(base, enabled, [&](uint32_t a, uint32_t &w) { return Read(a, w); },
            [&](uint32_t a) {
                installs.push_back(a);
                if (!installOK) return false;
                target = a;
                memory[a] = 0xEA000000;
                return true;
            }, [&] { return Remove(); });
    }
};

int main()
{
    constexpr uint32_t a = 0x6DF000, b = 0x800000;
    uint32_t address = 0;
    assert(B::Address(a, address) && address == 0x7526D0);
    for (uint32_t invalid : {0u, 0x100000u, a + 4, 0xFFFFF000u})
        assert(!B::Address(invalid, address));

    Fake wrapper;
    for (unsigned i = 0; i < sizeof(B::UnloadWords) / sizeof(B::UnloadWords[0]); ++i)
        wrapper.memory[B::UnloadAddress + i * 4] = B::UnloadWords[i];
    auto readWrapper = [&](uint32_t at, uint32_t &word) { return wrapper.Read(at, word); };
    assert(B::UnloadSignature(readWrapper));
    for (unsigned i = 0; i < sizeof(B::UnloadWords) / sizeof(B::UnloadWords[0]); ++i)
    {
        const uint32_t at = B::UnloadAddress + i * 4;
        wrapper.memory[at] ^= 1;
        assert(!B::UnloadSignature(readWrapper));
        wrapper.memory.erase(at);
        assert(!B::UnloadSignature(readWrapper));
        wrapper.memory[at] = B::UnloadWords[i];
    }

    for (uint32_t offset : {B::PerspectiveOffset, B::PerspectiveOffset + 4, B::DivideOffset})
    {
        Fake f;
        f.Load(a);
        f.memory[a + offset] ^= 1;
        assert(f.Frame(a) == B::Event::Mismatch);
        assert(f.Frame(a) == B::Event::None);
        assert(f.installs.empty());
        f.Frame(0);
        f.memory.erase(a + offset);
        assert(f.Frame(a) == B::Event::Mismatch);
        assert(f.installs.empty());
    }

    Fake f;
    assert(f.Frame(0) == B::Event::None);
    f.Load(a);
    assert(f.Frame(a, false) == B::Event::None);
    assert(f.Frame(a) == B::Event::Installed);
    assert(f.Frame(a) == B::Event::None);
    assert(f.Unload(b)); // unrelated unload leaves Battle hooked
    assert(f.state.active == a && f.removes.empty());
    assert(f.Unload(a)); // remove while mapped, then unmap
    assert(f.state.active == 0 && f.removes.size() == 1);
    assert(f.Frame(a) == B::Event::None); // stale scene during SDK cleanup
    assert(f.Frame(0) == B::Event::None); // no writes into unmapped memory
    f.Load(b);
    assert(f.Frame(b) == B::Event::Installed);
    assert(f.installs.back() == b + B::PerspectiveOffset);
    assert(f.Unload(b));
    f.Frame(0);
    f.Load(b); // same-address reload gets a fresh hook
    assert(f.Frame(b) == B::Event::Installed);
    assert(f.Frame(b, false) == B::Event::Removed);
    assert(f.Frame(b) == B::Event::Installed);
    f.Load(a); // base changes while old CRO remains mapped
    assert(f.Frame(a) == B::Event::Installed);
    assert(f.removes.back() == b + B::PerspectiveOffset);

    Fake failed;
    failed.Load(a);
    failed.installOK = false;
    assert(failed.Frame(a) == B::Event::InstallFailed);
    assert(failed.state.active == 0);
    assert(failed.Frame(a) == B::Event::None);
    assert(failed.Unload(a));
    failed.Frame(0);
    failed.Load(a);
    failed.installOK = true;
    assert(failed.Frame(a) == B::Event::Installed);
    failed.removeOK = false;
    assert(!failed.Unload(a)); // veto unload while a hook remains active
    assert(failed.state.active == a && failed.memory.count(failed.target));
    assert(failed.Frame(0) == B::Event::RemoveFailed);
    failed.removeOK = true;
    assert(failed.Unload(a));
    assert(failed.Frame(0) == B::Event::None);

    Fake noGap;
    noGap.Load(a);
    assert(noGap.Frame(a) == B::Event::Installed);
    assert(noGap.Unload(a));
    noGap.Load(b);
    assert(noGap.Frame(b) == B::Event::Installed); // new base, no absent frame
    std::cout << "battle_widescreen_test: passed\n";
}
