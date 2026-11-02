# Soft reset (L+R+Start) crashes Azahar with any 3GX plugin

P1-8, checked 2026-11-02 against `main` at `23180e6`, Azahar 2126.2 (Windows, Vulkan), isolated instance `p18` (`tools/instance.py`; a copy of the save; the real save's mtime and SHA-256 were unchanged). Evidence is local only, in `analysis/private/p18-evidence/` of the `claude/p1-8-soft-reset-2c7176` worktree.

**Result: not our plugin.** Azahar 2126.2 crashes (access violation, exit code `0xC0000005`) when a title restarts itself while *any* CTRPF 3GX plugin is loaded. This includes a minimal plugin with no hooks, OSD callback or file polling. Without the plugin loader, the same soft reset works. Nothing in this repo can stop it except stopping the game from seeing L+R+Start (see the end of this doc).

## Runs

Input without the plugin comes from an Azahar input movie (`tools/movie.py`, played with `tools/instance.py start NAME --movie FILE`, which is Azahar's `-p`).

| Run | Plugin | Input | Where | Result |
|---|---|---|---|---|
| 1 | ours | `cmd.py hold l/r/start` | field (`pokecenter`) | Azahar exits about 0.5 s after `RebootToTitle` |
| 2 | ours | `cmd.py hold l/r/start` | title | same |
| 3 | loader off | movie: 25 s idle, L+R+Start 5 s | title | **survives**: three resets (keys still held), the game boots again each time |
| 4 | ours | same movie | title | crashes (so the plugin's input injection isn't the cause) |
| 5 | minimal CTRPF plugin (empty `PluginMenu`, writes one log line) | same movie | title | **crashes** |
| 6 | minimal plugin, under a debugger | same movie | title | access violation in `azahar.exe`, see below |
| 7 | loader off | movie: Start ×10, A ×8 (to the field), L+R+Start 1 s | field (`FieldRo` loaded at 37 s) | **survives**; after the reset Azahar loads `00040000001B5000` again and the game starts up |

Run 4 also shows that CTRPF opens `ir:rst`: Azahar then reads an IrRst record per pad update, which this simple movie doesn't write ("Expected to read type 4" errors). The held L+R+Start still got through.

## What Azahar does on a soft reset

The game calls `APT:PrepareToDoApplicationJump(title FFFFFFFFFFFFFFFF)` and `DoApplicationJump` (jump to itself). The ROM is a `.cci`, not installed, so `NS::RebootToTitle` logs `Failed to find title ... resetting current title instead` and calls `System::Reset`. Same lines in every run:

```
Service.APT <Info> ... PrepareToDoApplicationJump:606: called title_id=FFFFFFFFFFFFFFFF, media_type=0X0, flags=0X000002
Service.APT <Info> ... DoApplicationJump:622: called param_size=00000000, hmac_size=00000000
Service.APT <Critical> ... NS::RebootToTitle:85: Failed to find title '.../title/00040000/001b5000/content/00000000.app' to jump to, resetting current title instead.
Kernel <Info> ... Process::~Process:692: Cleaning up process 11
```

`System::Reset` (`src/core/core.cpp`, 2126.2 and current master) is a shutdown and reload. It saves the plugin loader's `PluginLoaderContext` before `Shutdown()` and sets it on the new `plg:ldr` service in `Load()`, "as we don't currently support proper app jumping". The context still says `plugin_loaded = true` and holds `memory_changed_handle`. That handle is from the old process's handle table, and CTRPF got it through `svcControlProcess(PROCESSOP_GET_ON_MEMORY_CHANGE_EVENT)`. `PLG_LDR::OnProcessExit`, which clears both, never runs on this path: there is no `Plugin unloaded successfully.` line.

While the new process's memory is being mapped, `VMManager::UpdatePageTableForVMA` calls `PLG_LDR::OnMemoryChanged`. That sees `plugin_loaded && memory_changed_handle`, takes `kernel.GetCurrentProcess()->handle_table`, and calls `Get<Event>(stale handle)` on a handle table that doesn't exist yet. The plugin case log ends while the renderer is being recreated, cut mid-line (unflushed). The no-plugin case goes on to `Loader ... Program ID: 00040000001B5000`.

Debugger (run 6, a small Win32 debug loop; no symbols, so addresses were matched to source by disassembling with `dumpbin`):

```
second-chance 0xc0000005 at azahar.exe+0x572B61  params ['0x0', '0x4b0']   (read of address 0x4B0)
  azahar.exe+0x572B61  HandleTable::GetGeneric: checks 0xFFFF8001 (CurrentProcess), index = handle >> 15,
                       generation at +0x10000; faults on cmp qword ptr [rcx+rdx*8] with a null table
  azahar.exe+0x41358C  return into PLG_LDR::OnMemoryChanged: cmp byte [this+0x82] (plugin_loaded),
                       cmp dword [this+0x24C] (memory_changed_handle), GetCurrentProcess, Get<Event>
  azahar.exe+0x537ACB  return into the caller that looks up the plg:ldr service (VMManager)
exit code 0xc0000005
```

`+0x82` and `+0x24C` match `PluginLoaderContext` at `+0x80` in `PLG_LDR`. `is_enabled`, `allow_game_change` and `plugin_loaded` are the first three bytes. `memory_changed_handle` follows `user_load_parameters` (path[256], config[32]), `plg_event` and `plg_reply`.

Upstream: azahar-emu/azahar#2569 ("3GX plugins are not applied if the running title restarts", 2125.1.3, closed 2026-09-22) is the same reset path, reported as "plugin not reloaded", not as a crash. The `System::Reset` code is unchanged on master as of 2026-11-02. The fix belongs in Azahar: after restoring the context, clear `plugin_loaded`, `plugin_process_id`, `memory_changed_handle`, `memory_region` and `plg_event`/`plg_reply`. Then the plugin would also load again for the new process, which #2569 asks for.

## Save states

Not tested. Azahar 2126.2 can't save or load a state headlessly: there's no command-line option, and the RPC server gives an empty reply to SaveState (`tools/startpoint.py`, H3). Upstream added SaveState/LoadState RPC in 2127.0-alpha4. Reading the code (unverified in practice): a state load deserializes the kernel (handle tables included) together with `PluginLoaderContext`, and doesn't go through `System::Reset`, so this bug shouldn't apply. Worth checking once the project moves to an Azahar with RPC save states.

## What the plugin could do

The only thing the plugin can change is whether the game sees the combo. The pad hook (`Sources/Buttons.cpp`) could hide Start while L and R are held, so the game never soft resets and Azahar never takes this path. The cost is losing soft reset in the game: players can use Azahar's own restart, which wasn't tested here. Not done in this PR, because it's a behaviour choice; the alternative is reporting the bug upstream.

## Reproduce

```bash
py -3 -I tools/movie.py title-reset.ctm 25:- 5:l+r+start 300:-
py -3 -I tools/startpoint.py restore p18 title
py -3 -I tools/instance.py start p18 --movie title-reset.ctm --no-plugin   # survives
py -3 -I tools/instance.py stop p18
py -3 -I tools/startpoint.py restore p18 title
py -3 -I tools/instance.py start p18 --movie title-reset.ctm               # exits at the reset
```
