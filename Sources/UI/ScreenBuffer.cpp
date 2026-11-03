#include "UI/ScreenBuffer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>
#include <unistd.h>

namespace OneScreen
{
namespace UI
{
namespace ScreenBuffer
{
    static u8 *s_buffer = nullptr;
    static const char *volatile s_holder = nullptr;
    static volatile unsigned s_refused = 0;
    // Screens a newer one took the buffer from: they wait until it is free.
    static const char *s_displaced[8] = {};

    static bool Displaced(const char *owner)
    {
        for (const char *d : s_displaced)
            if (d == owner)
                return true;
        return false;
    }

    static void SetDisplaced(const char *owner, bool displaced)
    {
        for (const char *&d : s_displaced)
            if (d == owner)
                d = nullptr;
        if (!displaced)
            return;
        for (const char *&d : s_displaced)
            if (d == nullptr)
            {
                d = owner;
                return;
            }
    }

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
        if (s_holder != nullptr && Displaced(owner))
        {
            s_refused = s_refused + 1;
            return nullptr;
        }
        // Free, or a newer screen opened over the holder: the newer one takes
        // it, and the holder waits until it is free again.
        if (s_holder != nullptr)
            SetDisplaced(s_holder, true);
        SetDisplaced(owner, false);
        // Always fresh, even if nobody borrowed it in between: the owner may
        // have drawn something else into its layer since (a redraw is cheap).
        s_holder = owner;
        fresh = true;
        return s_buffer;
    }

    void GiveBack(const char *owner)
    {
        if (owner == nullptr)
            return;
        if (s_holder == owner)
            s_holder = nullptr;
        SetDisplaced(owner, false);
    }


    const char *Holder(void)
    {
        return s_holder;
    }

    unsigned Refused(void)
    {
        return s_refused;
    }

#ifdef __3DS__
    // newlib's malloc (dlmalloc 2.6.4, newlib/libc/stdlib/mallocr.c): 128
    // bins of free chunks, bin 0's fd is the top chunk. Read only, under
    // malloc's lock, so it never takes memory another thread is about to ask
    // for. The heap ends at fake_heap_end (libsysbase's sbrk refuses more;
    // CTRPF sets it, ctrulibExtension/system/allocateHeaps.cpp).
    struct Chunk
    {
        size_t prevSize, size;
        Chunk *fd, *bk;
    };
    extern "C" Chunk *__malloc_av_[];
    extern "C" char *fake_heap_end;

    size_t LargestFree(void)
    {
        const size_t sizeMask = ~(size_t)3;     // PREV_INUSE, IS_MMAPPED
        const auto bin = [](int i) { return (Chunk *)((char *)&__malloc_av_[2 * i + 2] - 2 * sizeof(size_t)); };
        __malloc_lock(_REENT);
        size_t largest = 0;
        for (int i = 1; i < 128; i++)
            for (Chunk *p = bin(i)->fd; p != bin(i); p = p->fd)
                largest = std::max(largest, p->size & sizeMask);
        // The top chunk and what sbrk hasn't handed out yet behind it.
        const Chunk *top = bin(0)->fd;
        const size_t topSize = top == bin(0) ? 0 : top->size & sizeMask;
        const char *topEnd = (const char *)top + topSize;
        if (top != bin(0) && topEnd <= fake_heap_end)
            largest = std::max(largest, topSize + (size_t)(fake_heap_end - topEnd));
        else if (top == bin(0) && fake_heap_end != nullptr)
            largest = std::max(largest, (size_t)(fake_heap_end - (char *)sbrk(0)));
        __malloc_unlock(_REENT);
        // A chunk holds its size word: that much less for the caller.
        return largest > sizeof(size_t) ? largest - sizeof(size_t) : 0;
    }
#else
    // Host tests: a malloc probe. Called through these: GCC drops a
    // malloc/free pair whose block is never used (and assumes the malloc
    // succeeded).
    static void *(*volatile s_malloc)(size_t) = std::malloc;
    static void (*volatile s_free)(void *) = std::free;

    size_t LargestFree(void)
    {
        // Binary search on what malloc gives; each probe is freed at once.
        size_t lo = 0, hi = 8u << 20;
        while (hi - lo > 1024)
        {
            const size_t mid = lo + (hi - lo) / 2;
            void *p = s_malloc(mid);
            if (p != nullptr)
            {
                s_free(p);

                lo = mid;
            }
            else
                hi = mid;
        }
        return lo;
    }
#endif


    static std::string Status(size_t largest)
    {
        const struct mallinfo m = mallinfo();
        char text[96];
        std::snprintf(text, sizeof(text), "heap %u KB used, %u KB free, largest %u KB",
                      (unsigned)(m.uordblks / 1024), (unsigned)(m.fordblks / 1024), (unsigned)(largest / 1024));
        return text;
    }

    std::string HeapStatus(void)
    {
        return Status(LargestFree());
    }

    std::string HeapLog(const char *what)
    {
        const size_t largest = LargestFree();
        return std::string("mem: ") + what + ", " + Status(largest) + (largest < LowMark ? ", LOW" : "");
    }


}
}
}
