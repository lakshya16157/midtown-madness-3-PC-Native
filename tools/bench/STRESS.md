Run repeated driving tests in one isolated game process with `stress.py`.
It uses the native `MM3Teleport-<PID>` pipe and the existing live pad file;
it never calls `WriteProcessMemory`, changes original saves, or rewrites game assets.
The build must implement the native `stress` commands. Preparation fails clearly
when the build cannot supply the requested evidence.

For autonomous native-menu acquisition, use:

```powershell
pwsh tools\bench\stress.ps1 -Save <save-template> -Plan tools\bench\stress-plan.example.json -Tag <unique-name>
```

This launcher selects and reads back the plan's city (Paris `0` or Washington DC `1`), Cruise, dusk, maximum traffic and
pedestrians, and cops level four through the native menu. It starts one isolated
process, executes every plan window, then stops only that process and removes
its save copy. The plan's expected native scene must match the acquired scene;
unreached or mismatched scenes fail. The default graphics workload is 3072×1920,
4× MSAA, a 60 FPS cap and VSync off.
`stress-washington.example.json` requests twelve nearby active police cars in
Washington DC at the first authored Cruise road spawn from
`Scripts/Washington/Cruise/00Logic.lua` in the installed game archive. It uses
three repeated driving windows and requires native collision-effect activity.
`-RenderScale 2.25` changes the initial internal height to 1080 lines.
Optional `-MsaaSamples 1|2|4|8`, `-DrawDistance 0.5..4` and
`-ViewDistance 0.5..4` override the profile's graphics workload. Omitted view
distance follows the selected object distance. The effective settings are
recorded with each run. For example, `-MsaaSamples 2 -DrawDistance 1 -ViewDistance 1`
retains the original world distances with lower anti-aliasing cost.
Presentation defaults are `-Aspect 16:10 -WindowWidth 1280 -WindowHeight 800`
in a window. Optional `-Aspect` accepts `4:3`, `16:9`, `16:10` or `auto`;
`auto` follows the actual client aspect. `-Fullscreen` requests borderless
fullscreen at the panel's actual desktop resolution; window width and height
are window preferences and do not change the desktop resolution.
Requested presentation options and effective environment settings
are recorded in `run.json`; use renderer logs/captures to verify actual sizes.
The experimental `RECOMP_READBACK_IDLE_WAIT` flag passes through explicitly
and defaults to `0`. `RECOMP_AUDIO_DSP_HANDOFF` passes through and defaults to
`1`, matching normal MM3 profiles; an explicit `0` retains the legacy DSP yield
path for comparisons. `RECOMP_AUDIO_HIRES_WAIT` is a separate audio deadline option.
`-HitchTrace` explicitly enables detailed hitch timing; omitting it unsets that
trace. `-QuietProfiling` disables renderer and DSP summaries. Frame, bounded
audio/collision and producer-gap evidence remain enabled; `run.json` records
the exact coverage. At shutdown, the harness tries a normal window close for
five seconds, then stops its owned process if needed. `shutdown.json` records
the method; forced exits can lose a buffered diagnostic tail.
For the user's observed 3840 by 2400 panel with automatic aspect and 1080 internal lines:

```powershell
pwsh tools\bench\stress.ps1 -Save <save-template> -Plan <validated-plan.json> -Tag <unique-name> -RenderScale 2.25 -Aspect auto -WindowWidth 3840 -WindowHeight 2400 -Fullscreen
```

`-StationarySeconds 30` adds a no-input control at the plan's fixed pose before
spawning police and driving. Its exact frame/CPU window and native position
samples are saved as `stationary_control` in `stress-report.json`. Check the
recorded displacement before treating it as stationary: live traffic can push
the car. This control is separate from the driving/collision pass count, and
does not freeze traffic or guarantee identical geometry between launches.

Optional plan `render_scales: [4, 2.25, 2]` measures each scale crossed with
every `poll_us` condition in the same process, using an acknowledged native
renderer change outside measurement. Omit `render_scales` to leave rendering
scale unchanged. All conditions reverse order on alternate repeats.

For an existing operator-guided controlled session:

1. Start `run.ps1 -ControlledScene -Capture` with an isolated save template and
   explicit `-X`, `-Z`, `-Heading`. Acquire and capture the intended Cruise scene
   through `control.jsonl`, then use the existing `confirm` command.
2. Copy `stress-plan.example.json` into the run directory as
   `stress-plan-request.json`. Set the exact expected **native** scene values and
   fixed coordinates/heading for the scene you actually acquired.
3. Append this complete JSON line to the session's `control.jsonl`:

   ```json
   {"command":"stress","plan":"stress-plan-request.json"}
   ```

4. Read `control-results.jsonl` and `stress-report.json`. A completed command can
   contain failed evidence checks; `verified: false` is not a passing test.
   `finish` then stops the owned game and lets the existing runner restore its
   environment and clean its isolated save.

For a separately managed isolated session with a compatible `run.json`:

```powershell
tools\venv\Scripts\python.exe tools\bench\stress.py <run-directory> --plan <plan.json>
```

The direct controller leaves that process running for its owner to close.
It refuses shared save/config/pad/frame paths, a changed executable identity,
preexisting measurement windows, and an existing stress report.

Each window resets the car through the native ground-reset routine to the same
explicit X/Z/heading. Conditions reverse order on alternate repeats. The default
input takes 27.7 seconds including the input parser's 200 ms releases, leaving
over two seconds inside a 30-second measurement. Both left and right turns must
be announced as applied in runtime logs, the car must change heading and move,
and the required collision counter must increase. The example uses `effects`,
the native collision-kind effect counter. `total` counts contact-producing calls,
which can include ground contact; neither counter classifies a wall impact.
Unavailable `wall`/`vehicle` classifications cannot be substituted by either.
Counts that the native build
cannot classify remain unavailable. Neither the planned input nor a menu choice
is accepted as proof of a collision, traffic count, or spawned police car.

Set `police_count` to a desired native **total**, not an additive spawn count.
Count-zero baselines request a clear in a fresh isolated session. Positive-count
scenarios preserve the existing native pool, reject any requested shrink, and
verify that all prior IDs survive each native total request. Active police are
reused and relocated between windows, avoiding active actor destruction during
the run. The report records prior, reused and newly allocated IDs.
`police_mode: "pool"` explicitly tests allocated police actors, including dormant
actors. It is labeled **police pool load** and does not establish an active chase.
`police_mode: "active"` requires verified native `active_count` and `active_ids`;
the controller waits at most five seconds after the native set-total request.
It also requires `nearby_count`/`nearby_ids` and per-ID body position evidence:
nonzero native execution mode, a validated body, finite position, and distance
within 150 m horizontally and 50 m vertically of the copied player position.
The controller independently recomputes these distances with a 1 mm tolerance
for serialized guest coordinates. It rejects pool-only evidence. The prepared
`stress-police-active.example.json` requests twelve nearby active police cars,
three repeats and rendering scales 4, 3 and 2 at 250 µs polling. Failed native
activation/count/body proof stops that scenario before a measurement window.
After a positive spawn proof, the controller repeats the native player pose
reset, records `post_spawn_reset`, and revalidates fresh player and police body
snapshots before capture. Activation can otherwise move the parked player before
the measured driving pattern starts. The launcher records
`RECOMP_AUDIO_HIRES_WAIT` explicitly, inheriting its value or using `0`.
Optional `fence_modes: [0, 1]` adds the GPU completion mechanism to the same-process
condition sweep; the controller waits for the renderer's asynchronous applied
acknowledgement. Omit it to preserve the current mechanism. Full Cartesian
condition order reverses on alternate repeats. The launcher also records
`RECOMP_READBACK_FENCE` explicitly, defaulting to `0`.
Optional `audio_wait_modes: [0, 1]` adds producer condition-wait/native-timer
conditions, with an applied acknowledgement before warmup. Initialize the native
timer handles using `RECOMP_AUDIO_HIRES_WAIT=1` for that comparison; each window
records the effective selected wait. Omitted mode fields preserve old names.
Optional `native_apu_modes: [0, 1]` compares the original exception-dispatched
APU register access with native access to the same backend in one process.
`stress native_apu 0|1` changes the path before warmup; `stress native_apu`
queries it without changing audio state. Enabling requires a live audio backend
and no legacy `RECOMP_APU_TRACE`. The controller checks the effective mode at
both window boundaries, records it in `native_apu`, `native_apu_checks` and
`condition_settings`, and adds `-nativeapu0` or `-nativeapu1` to window names.
Startup `metadata.settings.MM3_NATIVE_APU_MMIO` remains the original environment
value. Omit the plan field to leave the access path and window names unchanged.
`metadata.settings.RECOMP_XA2_STARTUP_BUFFERS` records the XAudio2 startup
padding comparison setting (`5` by default, or `0`). The default adds 1280
silent stereo frames once before the first real submission, giving 26.667 ms
of headroom. It does not reset at each stress window. Submitted-PCM captures
include that declared prefix; premix-to-host analysis verifies it explicitly.
The launcher follows MM3's default `RECOMP_AUDIO_DSP_HANDOFF=1`, preserving an
explicit `0` for comparisons. This changes DSP lock handoff at existing execution
quanta; it does not change DSP instructions, output samples or the frame cadence.
Fatal `R6025`, pure-virtual, `KeBugCheck`, `FATAL`, native `[CRASH]` and failed
native indirect-call diagnostics stop further
measurement windows and mark runtime health separately from FPS evidence.
`MM3_ROUTE_DIAGNOSTICS=1` adds bounded navigation-request lifetime records and
is recorded explicitly in `run.json`; it defaults to `0` for normal tests.
A positive `minimum_traffic_cars`
similarly requires an actual native count. Its default zero allows frame/collision
tests while traffic classification is unavailable, and the report records that
limitation. Use `minimum_collisions: 0` only for a deliberate noncollision baseline;
unavailable counters still remain unverified in that baseline.

`windows.json` contains exact QPC measurement windows and process CPU samples.
`stress-report.json` adds native state snapshots, pose resets, requested/observed
police counts, movement and collision deltas, applied input, independent guest-flip
and Present interval summaries, overlapping hitch records, and QPC-tagged audio
records. CSV persistence is checked before analysis. Audio records without QPC
timestamps cannot be assigned to a window; missing audio records do not establish
healthy output. Startup, resetting, spawning, and warmup are outside measurement.

The same process still has evolving traffic, shader caches and world objects.
Repeated fixed poses improve comparability; they do not make collisions or traffic
deterministic. Logged overlaps are correlation and nested renderer stages are not
independent costs. Preserve the native state and input evidence when comparing
conditions instead of interpreting a lower average as proof of smooth driving.

For a diagnostic pass, `-SampleThreads` starts the bounded native x64 PC sampler
after race acquisition. It briefly suspends each sampled thread, reads its PC,
and resumes it before recording the result. The run requests shutdown through a
stop file before closing the game. Resolve the retained samples against the
matching executable and PDB before rebuilding:

```powershell
python tools/diagnostics/sample_threads.py resolve --output <run>/thread-samples --windows-json <run>/windows.json
```

These are PC samples, not call stacks or exact CPU-time attribution. Sampling
changes scheduling; use a separate pass without `-SampleThreads` to validate FPS.
The optional `RECOMP_SLOW_FRAME_TRACE`, `RECOMP_AUDIO_DSP_TRACE`, and
`RECOMP_AUDIO_DSP_TRACE_CPU` flags are recorded in the run settings and default
to `0`. Disable them for normal performance validation.
