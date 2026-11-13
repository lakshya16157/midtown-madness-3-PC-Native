# Vulkan renderer: why it is slower than DirectX 11, and the fix plan

Written 2026-11-12 so that any model (or person) can continue this work. Read it top to bottom before
changing anything. The status line of each fix says whether it is done.

## Rules for whoever continues

- **Do not launch the game or run benchmarks unless the user asks in that message.** The user tests the
  game themselves and asked for testing to stop on 2026-11-12. Build, run offline tests, hand over.
- Never set `RECOMP_READBACK_HOLD=0` as a default on Windows: it makes window lights flash.
- Build the Vulkan exe from the repo root (PowerShell):
  `. .\scripts\enter-env.ps1; cmake --build build-vulkan --config RelWithDebInfo --target mm3_recomp`
  then, only when no `mm3_recomp.exe` is running, copy
  `build-vulkan\RelWithDebInfo\mm3_recomp.exe` to `build\RelWithDebInfo\mm3_recomp_vulkan.exe`
  (the launcher's "Vulkan renderer" option starts that copy).
- The DirectX 11 exe is `build\RelWithDebInfo\mm3_recomp.exe` (target `mm3_recomp` in `build`). Any new
  `nv2a_d3d_*` function the kernel calls must exist in **three** places: `nv2a_sdlgpu.c` (Vulkan),
  `nv2a_d3d11.c` (twice: the real one and the stub block after the last `#else`), and the two test stub
  files `tools/xboxrecomp/tests/nv2a_exec_stubs.c` and
  `tools/xboxrecomp/tests/kernel_dispatch/nv2a_texaddr_test.c`.
- Use the Edit tool for C edits that contain backslashes; shell heredocs eat them.
- Nothing here is committed or released. Releases need the user's explicit go-ahead.

## Files

| File | What it is |
|---|---|
| `tools/xboxrecomp/src/video/nv2a_sdlgpu.c` | The Vulkan renderer (SDL3 GPU). Almost all fixes go here. |
| `tools/xboxrecomp/src/video/nv2a_d3d11.c` | The DirectX 11 renderer, for comparison. |
| `tools/xboxrecomp/src/kernel/nv2a_pb_exec.c` | Executes the game's GPU commands and calls the renderer. |
| `tools/xboxrecomp/src/kernel/xbox_memory_layout.c` | `nv2a_ack_thread`: the thread that runs the executor (the "renderer thread"). |
| `src/compat/sdlgpu_d3d11_only.c` | Empty stand-ins for DX11-only functions in the Vulkan build. |
| `Bench DX11 (frame timings).cmd`, `Bench Vulkan (frame timings).cmd` | Launchers the **user** runs; they write `logs\bench-dx11.csv` / `logs\bench-vulkan.csv`. |
| `tools/bench/compare_frames.py` | Compares those two csv files (fps, p50/p99/p99.9, worst frame). |

## How the Vulkan renderer works (needed to understand the fixes)

- Two threads matter. The **game thread** writes GPU commands into the push buffer. The **renderer
  thread** (`nv2a_ack_thread`) reads them (`nv2a_pb_scan_segment`) and calls `nv2a_d3d_draw_ub` etc.
- `nv2a_sdlgpu.c` does not call SDL per draw. Each draw is **recorded** as ops in a batch (`s_b`,
  `rec_uniform`, `rec_samplers`, `draw_op`). `flush_batch()` replays the batch into one SDL render
  pass. A batch ends when the render target changes, a copy pass is needed, or `submit()` runs.
- `submit(fence)` sends the SDL command buffer `s_cmd` to the GPU and starts a new one
  (`s_cmd_serial++`). Vertex and index data live in two rings (`s_vtb`/`s_vbuf` 32 MB,
  `s_itb`/`s_ibuf` 16 MB, positions `s_vpos`/`s_ipos`); texture uploads use `s_utb` (`s_upos`).
- At each flip (`nv2a_d3d_flip_present`): `writeback_surfaces()` records downloads of the render
  targets and calls `submit(&s_wb_fence)`; then `present_surface()` draws the picture to the window
  and submits again. The game is held at its flip until `nv2a_d3d_writeback_busy()` says the
  downloads are in guest memory (the "read-back hold", `RECOMP_READBACK_HOLD`, default 1 on Windows).

## What was measured (2026-11-12, user's PC: RTX 2000 Ada laptop, 1728x1080 internal, render scale 2.25)

| | DirectX 11 | Vulkan |
|---|---|---|
| Benchmark race, cap 120 | 117 fps | 88.8 fps (65-85 in dense scenes) |
| User's race, cap 60: idle time left per frame | 6.7 ms | 3.8 ms |
| Game thread waiting at the flip (`[MM3_GPU_WAITS] notify`) | ~12 ms per second | 290-355 ms per second |
| Draw stage cost | 2.1 us per draw | 3.4 us per draw |

At Steam Deck settings (render scale 1.5) both ran ~118 fps on 2026-11-11, so the gap grows with the
pixel count.

## The causes and fixes, biggest first

### Fix 1. Send work to the GPU during the frame, not only at the flip — STATUS: DONE in code 2026-11-12, built, seen working in the user race 2026-11-12

**Cause.** The whole frame was recorded into one command buffer and submitted only at the flip
(`nv2a_d3d_kick` only does `s_epoch++`). The GPU started the frame when the CPU had finished it, so the
game waited 4-5 ms per frame at its flip. In a 12 ms dense frame the game worked ~7.7 ms and both
threads sat idle ~28% of the frame. DirectX 11's driver sends work to the GPU while the frame is built.

**Fix.** In `nv2a_sdlgpu.c` add `submit_early()` and call it every N draws:

```c
static uint32_t s_early_draws;     /* draws recorded since the last submit */
static int s_early_n = -1;         /* RECOMP_SDLGPU_SUBMIT_DRAWS: draws per early submit, 0 = off, default 128 */
static int64_t s_early_last;       /* when the last one was */

static void submit_early(void)
{
    uint32_t v = s_vpos, i = s_ipos, u = s_upos;
    if (!s_cmd) return;
    copy_end();
    submit(NULL);
    /* The rings go on where they were (submit() restarts them): restarting would cycle a 32 MB and a
     * 16 MB buffer per early submit. Marking them as mapped for the new command buffer keeps
     * ring_reserve / upload_texture_extent from cycling too. */
    s_vpos = v; s_ipos = i; s_upos = u;
    s_b.v0 = s_b.v1 = v; s_b.i0 = s_b.i1 = i;
    s_vcmd = s_icmd = s_ucmd = s_cmd_serial;
    s_early_draws = 0;
    s_early_last = qpc();
}
```

Call sites: (a) in `nv2a_d3d_draw_ub`, right after the existing "room for this draw's data" check
(`if (need > VB_BYTES - s_vpos || ...) submit(NULL);`): `if (s_early_n > 0 && s_early_draws >= s_early_n) submit_early();`
and count `s_early_draws++` next to `s_prof_draws++`; (b) in `nv2a_d3d_tick` after `fbw_sdl_pump()`:
submit when at least 24 draws are waiting and 0.5 ms passed, so a tail of draws does not sit unsent
while the game computes. `submit()` itself must set `s_early_draws = 0`.

**Why it is safe.** `submit(NULL)` in the middle of a frame already exists (the ring-full case at the
top of `nv2a_d3d_draw_ub`). All per-command-buffer caches key on `s_cmd_serial` (`s_last_uni_cmd`,
`pushed_cmd`), and sampler de-duplication resets in `flush_batch`.

**If it shows wrong pictures:** set `RECOMP_SDLGPU_SUBMIT_DRAWS=0` to switch it off and compare.

**Expected.** Dense scenes from 70-85 fps to roughly the DirectX 11 level. Not measured yet.

### Fix 2. Sun-flare frame reads no longer wait for the GPU — STATUS: DONE in code 2026-11-12 (latched read), built, user-confirmed 2026-11-12 ("lighting looks much better")

**Cause.** The game copies a few 8x8 blocks of the frame into its 128x128 sun-visibility picture with
2D blits (`nv2a_pb_exec.c`, `subch == 2`, method 0x308). `nv2a_d3d_surface_read` did that by
submitting the frame so far and waiting for the GPU: 4.8-5.5 ms per read, 0.4-1.4 reads per frame
(log line `surface reads N, X ms (total)`).

**First attempt (2026-11-12 morning, WRONG, replaced).** Deliver the pixels to guest memory when the
flip's download fence is done. Wrong because the game rebuilds the 128x128 picture every frame
(base copy, then the blocks, then the flare draw): pixels that arrive later are wiped by the next
frame's base copy before the flare is drawn, so the flare would use a picture without its blocks.

**Correct design ("latched read").** `nv2a_d3d_surface_read_async(phys, x, y, w, h, dst, dst_pitch)`
keeps one slot per read ordinal within the frame. Read number k of this frame **writes into `dst`
immediately the result of read number k of the last finished frame**, and queues a download of the
current pixels for the next frame. It returns 1 when it wrote `dst`, 0 when it has no usable result
(first frame, different size or surface, block moved more than 32 pixels, result older than 3 frames);
on 0 the caller falls back to the blocking `nv2a_d3d_surface_read`. One frame of lag in sun
visibility; order of writes is exactly the game's. Each slot owns one reusable transfer buffer
(this is also Fix 4). `RECOMP_SDLGPU_SYNC_READ=1` switches the latched read off.

### Fix 3. Per-draw CPU cost — STATUS: not done

**Cause.** On the renderer thread the NVIDIA Vulkan driver costs 1.33 ms per frame against 0.77 ms for
DirectX 11 (whose driver has its own worker thread), and the SDL layer adds 0.56 ms.

**Fix (large).** Replay batches on a second thread: `flush_batch()` would hand the finished batch
(ops, arena, vertex/index ranges) to a worker that makes the SDL calls and submits. Needs care with
every place that needs the command buffer in order (copy passes, downloads, present). Do it only with a
way to test; it matters most on slow CPUs (Steam Deck) once Fix 1 is in.

### Fix 4. GPU buffers created and destroyed every frame — STATUS: done with Fix 2

The first async read created and released an SDL transfer buffer per read (seen as D3DKMT
CreateAllocation / DestroyAllocation, ~0.4 ms per frame). The latched read keeps one buffer per slot.

### Fix 5. The renderer thread never sleeps when idle — STATUS: DONE in code 2026-11-12 16:14 (read-back waits only), not yet run

`nv2a_ack_thread` ends each pass with `if (!nv2a_ack_idle_wait(regs)) Sleep(0);`. In the Vulkan build
`nv2a_d3d_readback_idle_wait` is an empty stand-in in `src/compat/sdlgpu_d3d11_only.c` (returns 0), so
the thread spins and pumps SDL events every pass. DirectX 11 sleeps on a GPU event with a 250 us timer
(`nv2a_d3d_readback_idle_wait` in `nv2a_d3d11.c`). A Vulkan version would be
`if (!s_wb_pending && !s_ar_sub) return 0; SDL_DelayNS(250000); return 1;` in `nv2a_sdlgpu.c` (and remove
the stand-in). Trade-off: saves a CPU core (helps the Steam Deck), but can add up to ~0.5 ms to each
frame on a fast PC. Measure before making it the default.

### Fix 6. The window picture is drawn twice — STATUS: not done, on purpose (2026-11-12: ~0.3 ms GPU, the GPU is not the limit; a mistake gives a black window and it cannot be checked without a run)

`present_surface` draws the frame into a window-size texture `s_kept` (`composite`), then
`present_kept_image` copies `s_kept` to the swapchain. At a 3840x2400 window that is 2 x 9.2 megapixels
per frame. Drawing straight into the swapchain image and keeping `s_kept` only for repeated frames
(`nv2a_d3d_repeat_frame`, `nv2a_d3d_interp_frame`, screenshots, F9 history) would halve it.

### Fix 7. 7.6% of draws use the slow generic pixel shader — STATUS: partly done 2026-11-12 (26 states added, 274 -> 297; logging on in the bench launcher)

Log line `[SDLGPU_PROF] pixel shaders: N draws specialised, M generic`. The specialised list is
`src/shaders/mm3_ps_states.txt`, prebuilt by `tools/shader_prebuild/sdlgpu_spec_prebuild.py`. Adding
the missing states needs a capture from a game run (`tools/diagnostics/merge_shader_cache.py`).

### Fix 8. First-time pipeline stalls — STATUS: cache fixed 2026-11-12; seed list + cache kept across shader-list changes DONE 2026-11-12 16:14, not yet run

`pipeline()` now records every pipeline it builds on the drawing thread in
`%LOCALAPPDATA%\XboxRecomp\ShaderCache\sdlgpu-pipelines.bin`; the next start builds them in the
background (`[SDLGPU] pipeline warm-up: N of N pipelines`). A pipeline never seen before still stalls
once (625 ms seen). The stall log line now names it (`vs/ps/spec/color/prim`). Shipping a ready list
with the installer would remove the first-run stalls.

## How the user checks the result

1. Run `Bench DX11 (frame timings).cmd`, do one race, close the game.
2. Run `Bench Vulkan (frame timings).cmd`, same race and car, close the game.
3. `python tools/bench/compare_frames.py logs/bench-dx11.csv logs/bench-vulkan.csv`
4. In the Vulkan log (`logs\play-*.log`, newest): `[MM3_GPU_WAITS] notify a/b ms` should drop from
   ~300 ms per second towards the DirectX 11 values; `surface reads` should stay near 0;
   no `[SDLGPU_STALL]` lines above ~50 ms after the first run.
5. Look at the sun glare (must still appear and fade behind buildings) and at night window lights.

## Status log

- 2026-11-12: Fix 8 cache part done and seen working in a user log.
- 2026-11-12 (later): Fix 1 (`submit_early`, `early_draws_limit`, call sites in `nv2a_d3d_draw_ub` and
  `nv2a_d3d_tick`), Fix 2 + Fix 4 (latched read: `nv2a_d3d_surface_read_async`, `async_reads_complete`,
  `async_reads_flip` in `nv2a_sdlgpu.c`; caller in `nv2a_pb_exec.c`; DX11 and test stubs return 0) are
  written. `build-vulkan` exe builds and links; the DX11 libraries (`xbox_video`, `xbox_kernel` in `build`)
  compile with the new stub. The Vulkan exe was copied to `build\RelWithDebInfo\mm3_recomp_vulkan.exe`.
  **Not run at all**: the user tests. No offline renderer test exists in `build-vulkan` (0 ctest tests there).
  New log line once a second with `RECOMP_D3D_PROFILE=1`:
  `[SDLGPU_PROF] early submits N, sun reads served without a wait M (total)`.
  Switches for comparing: `RECOMP_SDLGPU_SUBMIT_DRAWS=0` (Fix 1 off), `RECOMP_SDLGPU_SYNC_READ=1` (Fix 2 off).
- Still open, in order of value: Fix 3 (second thread), Fix 5 (idle sleep, measure first), Fix 6, Fix 7,
  Fix 8 boot list. If the user reports a wrong picture, first ask them to retry with the two switches
  above, one at a time, to learn which fix causes it.
- 2026-11-12 13:xx, user's first race with Fix 1+2 (logs play-20261112-114457-692-88699337.log, 60 fps cap,
  render scale 2.25): idle time per frame (pace) 3.65 ms (old Vulkan) -> 6.58 ms (DX11 6.72); draw stage
  4.18 -> 3.22 ms; 21636-58790 early submits; 1244 sun/light reads served without a wait. User felt the
  lights' glow a bit delayed: the latched read served frame N-2's result, because last frame's downloads
  were only collected at the next flip. Fixed: `nv2a_d3d_surface_read_async` now polls `s_wb_fence`
  (`SDL_QueryGPUFence`, non-blocking) and calls `writeback_finish(0)` first, so the lag is one frame.
  If it still feels delayed: `RECOMP_SDLGPU_SYNC_READ=1` gives exact (blocking) reads; the cost is
  ~3 ms per read. Still ~800 reads per run fall back to the blocking read (no matching slot: first frame
  or a block that moved more than 32 px); worth checking which reads miss.
- First-run pipeline stalls seen again: spec 157/158/144 (ps 3), 534-579 ms each; now in the cache file.
- 2026-11-12 player logs (Steam Deck, SteamOS, RADV, 2.7 beta 1; Downloads\direct-20261112-1037/1040/1042.log):
  2 of 3 runs crashed on quit, thread 1002 (the renderer/ack thread) inside the GPU driver right after
  "[MM3_EXIT] close requested". Cause: Linux `ExitProcess` (platform/win32_compat.c) is `exit()`, which runs
  atexit handlers and unloads the driver while the renderer thread still draws (Windows ExitProcess
  stops other threads first). Fix: `xbox_Nv2aStopRenderer(2000)` (xbox_memory_layout.c) called in
  `mm3_close_game` (src/main.c) on non-Windows before ExitProcess. Compiles on Windows and in WSL;
  not run on a Deck. The game ran at exactly 40 fps because of the player's saved "Maximum FPS"
  (pace 11-18 ms spare per frame), not a slowdown.
- 2026-11-12 15:00 user race on the new Vulkan build (logs/play-20261112-145458-452-04782afb.log, 60 cap):
  user: "lighting looks much better" (lag fix confirmed). Race: 60.0 fps every second outside loading,
  draw 3.1 ms, pace (idle) 7.3 ms/frame, cpu-sync 0.65, notify waits ~0-3 ms/s (was 290-355 ms/s).
  Early submits ~370/s. Sun reads served without a wait: 1135. Still 738 blocking reads (2265 ms, ~3 ms each),
  all in one ~35 s stretch (14:55:45-14:56:19), ~25/s = ~1.2 ms/frame there: NEXT item = find why those
  miss the latched slot (log the read's phys/x/y/w/h on fallback). One new pipeline stall (spec 115, 126 ms,
  now cached). bench-vulkan.csv vs bench-dx11.csv: p50 16.67 both, p99 17.67 vs 17.60 (equal);
  the >100 ms frames in both are loading screens.
- 2026-11-12 16:14 "fix everything you can" (exe build-vulkan 16:14:44, copied to build/RelWithDebInfo/mm3_recomp_vulkan.exe; not run):
  1. Sun reads (Fix 2): `nv2a_d3d_surface_read_async` now takes the NEAREST valid result of the same size and
     surface from ANY slot (within 32 px, at most 3 frames old), not only slot k. Slot k alone missed whenever a
     lamp came into or left view (all later reads of the frame shifted by one slot). AR_MAX 8 -> 32. The PROF line
     now counts blocking fallbacks by reason: `blocking: slots full / no match / moved / old / surface`. If
     "moved" dominates next time, the sun moves > 32 px per frame in fast turns (raise the limit or accept it).
  2. Fix 5: `nv2a_d3d_readback_idle_wait` now lives in nv2a_sdlgpu.c (the stand-in in
     src/compat/sdlgpu_d3d11_only.c is removed): when the title waits for the flip's read-back and the fence is
     not done, `SDL_DelayNS(100000)` instead of spinning. Same idea as DX11 (fence event + 250 us). Off with
     `RECOMP_SDLGPU_IDLE_WAIT=0`. Check: pace/notify in the PROF lines should not get worse.
  3. Fix 8: (a) `pipe_header_ok` accepts a cache file made with a different shader-state list (only magic,
     key size and depth format must match). Safe because a key's `spec` is an index that pipeline_create builds
     from this build's list (an index past the end falls back to the generic shader). So adding states no
     longer throws the cache away. (b) Seed list: `sdlgpu-pipelines-seed.bin` next to the exe is warmed up before
     the user's own file (keys de-duplicated). Source: `src/shaders/sdlgpu-pipelines-seed.bin` (a copy of the
     dev's %LOCALAPPDATA%/XboxRecomp/ShaderCache/sdlgpu-pipelines.bin). installer/build.py copies it into the
     payload, installer.py installs it with the game (Windows and Linux) and removes it on uninstall. It only
     helps GPUs with the same depth format (NVIDIA/Intel D24S8; the Deck's RADV is D32S8, so its list is
     ignored there). Refresh it before each release: copy the dev's cache file after a long Vulkan session.
  4. Fix 7: 26 states from the latest DX11 manifest (%LOCALAPPDATA%/XboxRecomp/ShaderCache/eb96af6d....psm,
     2026-11-11) that were missing are appended to src/shaders/mm3_ps_states.txt (appended at the end so
     existing spec indexes stay the same). The build compiled 297 specialised shaders, 0 failed. `Bench Vulkan
     (frame timings).cmd` now sets `RECOMP_SDLGPU_SPEC=log`: the log prints `[SDLGPU_SPEC] generic <54 hex words>`
     for states still generic. Append those lines' words to mm3_ps_states.txt (the same format) and rebuild.
  Not done: Fix 3 (second-thread replay; large, needs test runs), Fix 6 (see above). The DX11 exe was not
  rebuilt (it also prebuilds mm3_ps_states.txt for Wine/Proton; rebuild before a release).
- 2026-11-12 16:52 "load all shaders so the game doesn't stutter": launcher option "Load all shaders before
  playing (Vulkan)" = MM3_PRELOAD_SHADERS (default ON, Visual + audio mods tab, column 1) -> env
  RECOMP_SDLGPU_PRELOAD=1 (set in mm3_launcher_config_apply; an explicit env value is kept unless ticked).
  Renderer (nv2a_sdlgpu.c `pipe_async_init`): the warm-up list (seed file + user cache) is built by the
  background thread PLUS up to 3 helper threads (`warm_helper`, shared atomic index `s_warm_next`), and init
  waits for them (max 90 s). Log: `[SDLGPU] shaders preloaded: N of M pipelines built in X ms`.
  Off: the old single low-priority background thread. DX11 unaffected. Files: src/mm3_launcher_config.[ch],
  src/mm3_launcher.c (column + help text; the UI test limits the help text length, ~250 chars is fine),
  installer/mm3_settings.py (Linux settings), tests/mm3_launcher_config_contract.c, README, DEVELOPERS.md.
  Launcher tests pass (config contract, UI, settings, controls). Not run in game.
  Limit: it can only build pipelines it has seen (seed + the user's cache). A never-seen combination still
  builds once on first use. The seed is for NVIDIA/Intel (D24S8); the Deck needs its own seed
  (ask a Deck player for their %XDG_DATA_HOME%/xboxrecomp/sdlgpu-pipelines.bin).
- 2026-11-12 17:31 after 2.7 (reports: window lights flicker + "previous light stays when I look up" in Vulkan on Windows;
  Steam Deck issue #27: FPS locked at 40, wrong textures on Eiffel Tower / Louvre). Not yet run in game:
  1. nv2a_sdlgpu.c `depth_find_texture` (port of nv2a_d3d11.c's 10-28 fix): the Y16 depth view the light pass reads is the
     most recent depth buffer at that address that COVERS the texture, not just the most recently bound one (MM3's 40x30
     brightness probe binds a small depth buffer at the scene's address right before the light pass). Log line
     `[SDLGPU_LIGHT] depth view at ... reads the WxH depth buffer, not the wxh one` shows when the old lookup would have
     been wrong. `RECOMP_D3D_DEPTH_LAST_BOUND=1` restores the old lookup for comparison. F9 saves frame history
     (logs/f9-history-N-*.bmp) if the ghost is still there.
  2. Linux: texture write watch (guest_write_watch.c) now OFF by default on non-Windows (RECOMP_TEXTURE_WATCH=1 enables):
     suspected cause of stale/wrong textures on landmarks (old poster textures where lattice belongs). Unproven; the watch
     was new in 2.7 beta 1 and only tested on Windows.
  3. FPS 40: the photo in issue #27 shows Gamescope Display 80 Hz, FPS 40 = the Deck's own frame limit 40 (QAM
     Performance > Frame limit puts the panel at 80 Hz); the game's limit is not the cause. New log line every ~5 s:
     `[SDLGPU_PROF] display refresh X Hz, game frame limit N, vsync V`.
  Needs a 2.7.1 release (Windows Setup + Linux tarball) after the user confirms; Linux binary not rebuilt yet.
- 2026-11-13 2.7.2 (Deck first-run stalls, issue #27 follow-up report: 11-39 stalls of 0.6-1.0 s per first run, built on
  the drawing thread). nv2a_sdlgpu.c: (1) `s_build_ms` running estimate of one pipeline build (note_build_ms from the
  background worker, the warm-up and the sync path; starts at 100): pipeline_draw builds a missing pipeline on the
  drawing thread only without a stand-in or when spent + estimate fits the 30 ms frame budget, so slow drivers use the
  generic stand-in + background build. (2) pipe_header_ok no longer compares the depth format (PipeKey has no GPU
  state), so the shipped seed list works on the Deck/AMD. (3) warm_loop also builds each key's generic twin (spec 0).
  (4) preload wait cap 90 s -> 40 s. Seed refreshed (190 keys). Not run on a Deck. Left: the rare 31-40 ms blocking
  sun read on a busy GPU.
