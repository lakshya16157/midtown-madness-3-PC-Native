Build from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/diagnostics/build_process_loopback.ps1
```

Start after the game PID exists, before the measured drive:

```powershell
build/audio-loopback/process_loopback.exe --pid GAME_PID --seconds 450 --output C:/absolute/new/capture/prefix
```

The output directory must exist. Existing output files are never overwritten.
Only the specified process and its children are included. The program has no
system-wide capture mode or fallback. It requires Windows build 20348 or newer.
The helper does not inject, suspend, or read game memory. It opens a process
handle to verify identity and stop when that process instance exits.

Wait for `prefix.ready.json` before driving. Capture stops on target exit, the
bounded duration, or Ctrl+C. PCM and packet records remain in preallocated
memory during capture; output is written after `IAudioClient::Stop`. A 450-second
capture reserves about 91 MB. Its own capture thread registers with Windows
MMCSS Audio and reverts on the same thread. No game thread settings are changed.

The completed files are `prefix.wav` (stereo s16le, 48 kHz),
`prefix.packets.csv` (source sample offset, frames, WASAPI flags, device position,
packet QPC position in 100 ns units, and receipt QPC ticks), and `prefix.json`
(PID/image, API results, clock, duration, flags, and completion reason). Empty
reserved files and a ready receipt are not proof of completed capture. Check
the final receipt's successful HRESULTs and `data_written` field.

After the game and helper stop, compare against the submitted PCM:

```powershell
tools/venv/Scripts/python.exe tools/diagnostics/compare_loopback.py --source host.pcm --loopback prefix.wav --packets prefix.packets.csv --receipt prefix.json --stress-report stress-report.json --output loopback-analysis.json
```

The XAudio2 backend adds five immutable 256-frame silent buffers immediately
before the first real submission. This supplies 26.667 ms of startup headroom
once; source decoding and real PCM order remain unchanged. The diagnostic
environment variable `RECOMP_XA2_STARTUP_BUFFERS` accepts `0` or `5` (default
`5`). Use the same setting across a performance comparison. The host dump and
device submitted-sample counter include these 1280 frames, so this loopback
comparison keeps the complete host dump unchanged. When comparing premix to
host, `analyze_audio.py` removes only the exact all-zero prefix declared by the
single `[XA2] startup_padding_frames=...` log record. Undeclared silence,
nonzero prefix bytes, wrong counts and midstream declarations fail that
comparison; they are never automatically trimmed.

The source may also be a lossless `.pcm.gz`. This offline command uses
NumPy/SciPy. It aligns an energetic waveform anchor, then compares 250 ms
blocks inside timestamped driving windows using correlation and fitted gain.
It reports silence, residual/misalignment candidates, and capture timestamp or
discontinuity warnings. A large clock/stream discontinuity can invalidate later
alignment; the tool reports uncertainty instead of silently passing it.

Process loopback measures the game's Windows render stream across endpoints.
It excludes unrelated applications, but is not a physical speaker/microphone
recording. Windows mixing, volume and conversion can change samples. Capture
faults and low-confidence alignment must be ruled out before attributing a
candidate to game corruption; successful capture alone is not a listening pass.

API references:

- [Microsoft application-loopback sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [Process activation parameters](https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_activation_params)
- [Capture packet data and timestamps](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)

`tests/test_loopback_analysis.py` checks gain/start-delay alignment, a 1.5-second
silence burst, a one-second distorted burst, explicit capture faults, silence
without an alignment seed, and incomplete packet metadata. It also detects
loud noise replacing quiet source PCM while keeping low-level dither and a
correlated volume increase out of that candidate category. The real Windows
fixture `tests/process_loopback_tone.cpp` supports separate target/unrelated
tones to verify process isolation without capturing another application's data.

For long source-health investigations, `RECOMP_AUDIO_PROFILE=1` retains the
existing default 600-report cap. `RECOMP_AUDIO_PROFILE_LIMIT` accepts a decimal
count from 1 through 1200; invalid values retain 600. Each report can include
one `[AUDIO_STATE]` snapshot, timestamped about 20 ms before the report closes:
all 256 active bits versus raw list reachability, paused/stream/envelope
classes, raw FE/DSP enable/reset state, bit-only VP mixbin signal metrics,
and integer pre-native-mix/final output energy. An unconfigured or unreadable
voice table is explicitly unavailable. Energy is an instantaneous snapshot,
not an integral across the report or a claim that game sounds are correct.
The extra scan and signal metrics are disabled when profiling is off.
Blocked trapped-IDLE metadata publication attempts are counted atomically
between snapshots; ordinary method effects still execute. The trapped decoder
pair stays stable until FECTL leaves TRAPPED, including across ISTS acknowledgement.
