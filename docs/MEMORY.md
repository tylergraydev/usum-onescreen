# Plugin memory

How the plugin's memory is laid out, how the native screens share one layer
buffer, how to measure the heap, and whether the plugin can have more.

## Where the memory goes (MemorySize 5MiB)

The 3GX loader (Azahar's `src/core/file_sys/plugin_3gx.cpp`, Luma3DS's
`sysmodules/rosalina/source/plugin/file_loader.c`) maps one block of
`MemorySize` bytes and splits it into:

| Part | Size | Source |
|---|---|---|
| executable: header + .text + .rodata + .data + .bss, + 0x1000, page-rounded | 1524 KB (this branch) | plugin_3gx.cpp:204-207 |
| bootloader page (Azahar only) | 4 KB | plugin_3gx.cpp:208-211 |
| plugin heap: the rest | 3592 KB | plugin_3gx.cpp:212 |

CTRPF then splits the plugin heap (`ctrulibExtension/system/allocateHeaps.cpp:38-55`,
same constants in the installed `libctrpf.a`, `__system_allocateHeaps`):

| Part | Size |
|---|---|
| CTRPF's own screen framebuffers (`ScreensFramebuffers`) | 676 KB (0xA9000) |
| newlib heap (`malloc` / `new`) | 2908 KB (0x2D7000) |
| hook page + shared page | 8 KB |

and takes 1 MiB of the newlib heap for its Search feature at boot
(`pluginInit.cpp:188-194`, `operator new(0x100000)`), so the plugin's own
code has about **1.9 MB** of heap, not 5 MiB. That is why the W7-2 crash came
at "2448 KB used": mallinfo's `uordblks` counts CTRPF's 1 MiB too.

### CTRPF's 1 MiB pool (can it go?)

No clean way. `InitHeap()` (`pluginInit.cpp:188-194`) runs unconditionally
during CTRPF's init (line 308), before `PatchProcess`, so no `FwkSettings`
field can stop it; `AllowSearchEngine = false` only hides the Search menu
entry (`PluginMenuHome.cpp:61`, `PluginMenuImpl.cpp:185`). The size is a
literal (`0x100000`). The pool (`CTRPluginFramework::Heap`, a MemPool set up on
first use, `Heap.cpp:29-43`) also backs `Directory` listings
(`Directory.cpp:206`, 100 entries at a time), `LineReader`/`LineWriter` (4 KB
buffers), `BMPImage` and the Gateway RAM dumper, not just Search
(`Search32.cpp:20-32` wants 0xB0000 of it). Shrinking it would mean freeing
`Heap::__ctrpf_heap` and replacing it from `PatchProcess` before the pool's
first use: internal symbols, not a documented API, so it is not done.
Rebuilding libctrpf with a smaller literal would work but forks the library.

Static data (.bss) comes out of the same block one for one:
 a 300 KB static
array is 300 KB less heap. The biggest left are the screenshot buffers in
`Sources/Shot.cpp` (`s_top` 281 KB + `s_bottom` 225 KB, used by the test
tools' `shot`).

Sources were read at 3gxtool 57e3160, Azahar e3c916e, Luma3DS a50bf16 and
CTRPluginFramework 96fd3d9.

## The shared screen buffer (UI/ScreenBuffer.hpp)

Every native screen used to allocate its own 250-340 KB layer the first time
it showed and keep it, or free it on hide. Both failed:

- kept: X menu + Bag + Party layers plus the W7-0 assets left no room
  ("NO MEMORY for the layer"), and once the game PANICked (svcBreak) with
  87 KB free;
- freed on hide: small allocations split the freed block, and a 246 KB block
  couldn't be allocated again with 327 KB free.

Now one buffer of 400x240x3 = 281 KB (the whole top screen in its BGR8
format, larger than any screen's area) is allocated first thing in `main()`,
before anything fragments the heap, and never freed. The screens take turns:

- `Borrow(owner, fresh)` / `GiveBack(owner)`, from the OSD callbacks only (the
  game thread, so two screens never race; no locks).
- First come, first served: whoever holds it keeps it until it gives it back;
  anyone else gets `nullptr` and asks again next frame (`Refused()` counts
  that). On a hand-over (X menu -> Party) the next screen shows one frame
  later, since the old one gives it back the frame it sees itself hidden.
- `fresh` is set on every new borrow: the owner sets its layer up again and
  redraws (another screen drew in the buffer meanwhile).
- A framebuffer needing more than 3 bytes a pixel (RGBA8) fails `Layer::Setup`
  and the screen stays hidden rather than overflowing.

Exceptions, both small and static: Rotom's "Ready" badge (196x26, 20 KB) has
its own buffer, because it shows on the field together with other screens
(over the map); Rotom's boxes borrow the shared one (the map steps aside for
them). PC boxes keeps its details badge in the tail of the shared buffer.

The battle menu and move learning had 184 KB and 150 KB static arrays; they
borrow the buffer too now, which gave that 334 KB back to the heap.

Files per screen: see the PR (each is a mechanical change in the screen's
OSD callback plus its show/hide log).

## Measuring

Each screen logs on show and hide:

    mem: party shown, heap 1870 KB used, 77 KB free, largest 988 KB

`used`/`free` are newlib's `mallinfo()` (`uordblks`, `fordblks`; `free` is
only what malloc already got from the heap, not the never-touched rest).
`largest` is the largest block `malloc` could hand out right now, which is
what decides whether the next big allocation works. It is read, not probed:
under `__malloc_lock` it walks newlib's free bins (dlmalloc 2.6.4,
`newlib/libc/stdlib/mallocr.c`; the bin layout was checked against
`__malloc_av_` in the ELF) and adds the top chunk plus the part of the heap
sbrk hasn't handed out yet, up to `fake_heap_end` (libsysbase's `_sbrk_r`
refuses anything past it; checked in the disassembly). A first version probed
with malloc/free: GCC removed the pair (the block was never used, and it
assumes malloc succeeds), so every probe "succeeded" and it printed 8191 KB;
and a real probe would briefly own the whole free block while another thread
(the asset Task, the game thread) might allocate. Under `ScreenBuffer::LowMark` (256 KB) the line ends in
`, LOW`. The boot log has the same numbers after the buffer is allocated
(`screen buffer: 281 KB; heap ...`).

`scenarios/memory-tour.txt` opens every native screen twice in one boot (PC
boxes, X menu, Party + summary, Pokédex, Bag, Options, Passport, Battle Video,
map, Rotom's prompt), then battles twice in a second boot, and fails on any
`LOW`, `NO MEMORY`, `bad_alloc` or `PANIC` plugin log line (`guard-reject`)
or an emulated crash (Azahar `<Critical>`, the log guard).

### Measured (2026-11-03, Azahar, instance p24, MemorySize 5MiB)

memory-tour PASS in 617 s, real save unchanged. Heap lines (KB):

| Point | used | largest free |
|---|---|---|
| boot, after the screen buffer | 1392 | 1514 |
| W7-0 assets loaded | +247 | |
| first Rotom prompt shown | 1678 | 1119 |
| after Rotom's texts are cached | 1866 | 931-988 |
| Pokédex shown (its data cached) | 1919-1939 | 853-862 |
| rest of part 1, both rounds (boxes, X menu, Party + summary, Bag, Options, Passport, Battle Video, map) | 1892-1912 | 912 |
| part 2 (second boot): both battles, X menu, Party, Bag | 1869-1885 | 988 |

The largest free block never went under 853 KB and did not drift down over
the second round; no `NO MEMORY`, `LOW`, bad_alloc or PANIC. Before this
change the same ground ran out ("NO MEMORY for the layer" with X menu + Bag
+ Party; PANIC at 87 KB free).

Not covered: the nickname keyboard (it needs a catch; native-nickname) and
move learning (native-move-learn). Both borrow the same buffer.

## Can the plugin have more than 5 MiB?

Yes, 10 MiB, by changing `MemorySize: 5MiB` to `MemorySize: 10MiB` in
`usum-onescreen.plgInfo`:

- 3gxtool takes `2MiB`, `5MiB`, `10MiB` (`sources/main.cpp:114-138`) and stores
  it in the 2-bit `memoryRegionSize` field of the 3GX header flags (bits 2-3,
  `includes/3gx.hpp:10,24`: 0 = 5 MiB, 1 = 2 MiB, 2 = 10 MiB).
- Azahar maps 5 / 2 / 10 / 5 MiB for those values (`plugin_3gx.cpp:193-197`).
  There is no cap and no setting for it (the only plugin settings are
  `plugin_loader_enabled` and `allow_plugin_loader`, `common/settings.h:518-519`).
  The block comes from the SYSTEM region (Old 3DS mode), or from the end of
  the APPLICATION region in New 3DS mode, where the game's reported
  `app_mem_alloc` shrinks by the block size (`plugin_3gx.cpp:215-235`).
- Luma3DS does the same on hardware (`file_loader.c:156-161`, `memoryblock.c:35-82`).
  UNVERIFIED: whether an Old 3DS running USUM has 10 MiB free in SYSTEM; if
  not, Luma fails the load ("A console reboot is needed to close extended
  memory games").
- With this executable: 10 MiB gives 8712 KB plugin heap, 8028 KB newlib heap,
  about 7 MB after CTRPF's 1 MiB (vs about 1.9 MB at 5 MiB).

Verified 2026-11-03 under Azahar (instance p24, New 3DS mode, plugin
loader on): the same ELF packed with `MemorySize: 10MiB` (3GX header byte
0x30 = 0x2B, memoryRegionSize 2; the 5 MiB build has 0x23, 0) booted to the
field, loaded the W7-0 assets and logged
`screen buffer: 281 KB; heap 1392 KB used, 7 KB free, largest 6634 KB`
against `largest 1514 KB` at 5 MiB: exactly 5120 KB more. Not switched in
this change: real hardware (Old 3DS SYSTEM region, Luma) is UNVERIFIED, and
the 5 MiB budget now holds (above).
