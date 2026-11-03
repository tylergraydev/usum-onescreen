#include "UI/ScreenBuffer.hpp"

#include <cstdio>
#include <cstdlib>
#include <malloc.h>

namespace OneScreen
{
namespace UI
{
namespace ScreenBuffer
{
    static u8 *s_buffer = nullptr;
    static const char *volatile s_holder = nullptr;
    static volatile unsigned s_refused = 0;

    bool Init(void)
    {
        if (s_buffer == nullptr)
            s_buffer = (u8 *)std::malloc(Bytes);
        return s_buffer != nullptr;
    }

    u8 *Borrow(const char *owner, bool &fresh)
    {
        fresh = false;
        if (s_buffer == nullptr || owner == nullptr)
            return nullptr;
        if (s_holder == owner)
            return s_buffer;
        if (s_holder != nullptr)
        {
            s_refused = s_refused + 1;
            return nullptr;
        }
        // Always fresh, even if nobody borrowed it in between: the owner may
        // have drawn something else into its layer since (a redraw is cheap).
        s_holder = owner;
        fresh = true;
        return s_buffer;
    }

    void GiveBack(const char *owner)
    {
        if (owner != nullptr && s_holder == owner)
            s_holder = nullptr;
    }

    const char *Holder(void)
    {
        return s_holder;
    }

    unsigned Refused(void)
    {
        return s_refused;
    }

    size_t LargestFree(void)
    {
        // Binary search on what malloc gives; each probe is freed at once,
        // so nothing is left behind.
        size_t lo = 0, hi = 8u << 20;
        while (hi - lo > 1024)
        {
            const size_t mid = lo + (hi - lo) / 2;
            void *p = std::malloc(mid);
            if (p != nullptr)
            {
                std::free(p);
                lo = mid;
            }
            else
                hi = mid;
        }
        return lo;
    }

    std::string HeapStatus(void)
    {
        const struct mallinfo m = mallinfo();
        char text[96];
        std::snprintf(text, sizeof(text), "heap %u KB used, %u KB free, largest %u KB",
                      (unsigned)(m.uordblks / 1024), (unsigned)(m.fordblks / 1024),
                      (unsigned)(LargestFree() / 1024));
        return text;
    }
}
}
}
