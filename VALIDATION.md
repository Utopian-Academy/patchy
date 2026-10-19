# Validation record
Initial evidence: 2026-10-18. Further input/mapping validation: 2026-10-19.
Hosted CI results will be recorded separately after publication.

## Environment
Windows x64, build 26200 reported by CMake; Visual Studio 18 2026 Community,
MSVC 19.50.35724.0, Windows SDK 10.0.26100.0, CMake 4.3.0.
The initial working directory was a home folder with no Git repository. A new
local repository was created in the `patchy` directory. No remote existed at that stage.
No custom driver, security change, device enable/disable, default change, format
change, volume change or audio recording was used.

## Completed
- Native Release and Debug builds succeeded. Final recheck results below.
- Four CTest entries: portable processing tests, CLI help, invalid CLI arguments,
  and prepared-loop ordinary C++ allocation audit.
- Processing tests cover PCM16/24/32 clipping/roundtrip, valid-bit alignment,
  float nonfinite/silent buffers, malformed formats, channel masks, self-route
  rejection, ring wrap/overflow, drift convergence at +/-500 ppm, 44.1/48/96 kHz
  conversion passband, downsample alias rejection, channel isolation and
  underrun/reset behavior. Clock tests cover QPC units, rollback, invalid
  timestamps and gross rate mismatch measurement.
- Prepared push/pull/encode/decode/reset loop: zero ordinary C++ allocations.
  This does not instrument Windows internals or all possible driver paths.
- Inventory enumerated 77 endpoints, including active, disabled, not-present and
  unplugged states, both render and capture. Formats and periods were available
  for active devices. Volume/mute diagnostics were added after a real failed test.
- Same canonical endpoint (default -> default) refused before streaming, exit 2.
- Existing absent USB endpoint retried three times in a three-second bounded run,
  did not change destinations, and stopped with no-start exit 2.
- Watch registered and completed a timed run; actual physical arrival/removal was
  not induced. Redacted report checked for names, IDs, known device labels and paths.

## Live routes and signal evidence
The opt-in signal test emits a 0.005 peak (-46 dBFS) 997 Hz tone, then measures
destination loopback correlation. It stores only numerical measurements, not audio.
It checks tone against a baseline and a 1559 Hz neighbor. This is a digital path
smoke test, not an analog recording, latency measurement, THD test or listening test.
Existing driver/APO/volume processing affects observed amplitude.

| Case | Evidence | Interpretation |
|---|---|---|
| Realtek default output -> NVIDIA display, 48 -> 48 kHz, initial eight-second run | 375,360 captured / 375,840 rendered frames; 2,400 priming-silent frames; zero bridge underruns/overflows/retries; one capture discontinuity | Streams worked; source and destination were later found muted, so this alone is not signal proof |
| Realtek speakers -> muted NVIDIA display, first signal test | 997 Hz peak 3.61e-7, below test threshold | Failed; do not hide this as a pass. Destination was at zero volume/muted |
| Realtek speakers -> already unmuted NVIDIA/MACROSILICON, final eight-second signal run | 383,520 captured / 384,960 rendered frames; 264,480 observer measurement frames; peak 0.0169983; neighbor 1.89e-6; zero bridge underruns/overflows; four source discontinuity flags and one observer discontinuity; final relative timestamp estimate -11 ppm | Tone detected and queue-health checks passed; discontinuity flags remain visible. No claim of clean analog or long-run behavior |
| Waves SoundGrid -> NVIDIA, 44.1 -> 48 kHz, initial six-second signal run | Tone detected; eight bridge underruns at default target | Signal path exists but route reliability failed |
| NVIDIA -> Waves SoundGrid, 48 -> 44.1 kHz, initial six-second run | Tone detected; zero bridge underruns/overflows; two observer discontinuities | Short signal evidence only, before final clock guard; not a physical 44.1 kHz hardware qualification |
| Waves -> NVIDIA with 100 ms target | Roughly 41.3k source frames/s despite a 44.1 kHz mix; three underruns in six seconds | More buffering does not solve a sustained rate mismatch. Cause is not established |
| Same Waves -> NVIDIA with final timestamp guard | Two bad windows, about -63,197 ppm relative timestamp rate; stopped around six seconds with HRESULT 0x80040201 and explicit configuration/clock-range error; signal test health failed as intended | Unsupported timing is rejected. No driver/firmware defect diagnosis is claimed |

The timestamp guard uses capture packet frame/QPC pairs and render IAudioClock
position/frequency/QPC pairs. It requires two completed two-second windows over
5,000 ppm difference; normal occupancy correction remains bounded at +/-2,000 ppm.
Invalid/discontinuous timestamps reset the relevant fit. It is a conservative
gross-mismatch guard, not a precise synchronization guarantee. Depending on window
alignment it can take around six seconds to reject a route.

## Still unverified / not delivered
- Two-hour or longer physical runs and measured analog latency, phase, noise,
  distortion, full-band SRC quality and audible artifacts.
- Physical 44.1 kHz input/output interfaces with independent clocks; the Waves
  endpoint is an existing software/vendor endpoint, not proof of such hardware.
- Actual USB unplug/replug, format changes, disable/enable, audio service restart,
  sleep/resume, hotplug event sequences and endpoint identity replacement.
- Physical 302USB, Bluetooth profiles, exhaustive multichannel interfaces and
  exclusive-mode contention.
- IAudioClient3 fallback on a device that rejects low-latency initialization.
- Hosted GitHub Actions, Linux sanitizer execution, kernel code, signing or HLK.
- Virtual devices, aggregate devices and the broader roadmap.

M1 remains in progress. The next major subsystem is a genuine signed virtual
input/output driver, after closing the remaining foundation acceptance tests.

## Final recheck
After the clock guard and diagnostic fixes, both configurations rebuilt successfully.
Release CTest: 4/4 passed (0.31 s); Debug CTest: 4/4 passed (0.72 s).
The core executable contains six processing test groups; the separate allocation
entry audits the prepared DSP loop. Git whitespace validation passed. Source and
local build artifacts remained local; no remote CI/publish had occurred at that stage.

## Input monitoring and channel mapping: 2026-10-19

- Added explicit capture-endpoint monitoring, source/destination direction checks,
  prepared channel selection/duplication/silence and an input packet watchdog.
- Release and Debug native builds succeeded. Five CTest entries passed, including
  channel-map bounds and invalid layouts, mono duplication, channel swaps, explicit
  silence, eight-channel assignment and a mapped mono-to-stereo SRC pipeline.
  CLI tests now verify usage exit codes and diagnostics for malformed maps and
  oversized numeric arguments. The allocation audit includes channel mapping.
- The first sandboxed webcam-input test returned E_ACCESSDENIED during Initialize,
  with zero frames. The same 12-second test outside the execution sandbox succeeded:
  default Logi C310 input -> Realtek headphones, 48 -> 48 kHz, `--map 0,0`.
  Final counters: 564,000 captured / 565,440 rendered, 2,880 priming-silent frames,
  zero bridge underruns/overflows/retries, one source discontinuity, zero timestamp
  errors, final relative timestamp-rate estimate -19 ppm. Actual queue target 2,112
  frames, capture/render buffers 1,056 frames, render period 480 frames. This proves
  stream/queue operation only: all output channels were intentionally silenced.
  No microphone samples were saved or audibly monitored; no privacy policy changed.
- Repeated the eight-second Realtek-speaker -> NVIDIA/MACROSILICON 48 kHz loopback
  signal test after mapping integration. Passed tone detection and queue-health
  checks: 383,040 captured / 384,480 rendered, 264,000 measured observer frames,
  997 Hz peak 0.0170597, 1559 Hz neighbor 9.7834e-7, zero bridge underruns/overflows,
  nine source discontinuity flags and one observer discontinuity. The final relative
  timestamp-rate estimate was -12 ppm. These flags are not a glitch-free claim.
- Physical mono/multichannel input mapping, microphone analog fidelity, input
  unplug/reconnect and input-watchdog fault injection remain unqualified on hardware.
  No ASIO frontend, DAW integration or kernel driver is implemented.
