# Patchy
Open audio infrastructure for Windows, inspired by the breadth and simplicity of
Apple's Core Audio. **Early engineering prototype**, not yet a system-wide virtual
audio device or a complete first-milestone release.

The native C++20 CLI inventories endpoints, watches changes, exports redacted
diagnostics, bridges a shared output's loopback audio to another output, and monitors
a physical input through a selected output. Explicit channel maps select, duplicate
or silence channels. The engine adapts sample rate and modest independent clock
drift using bounded buffers.

**Ableton cannot select Patchy as an ASIO device yet.** That needs a host-facing
ASIO component in addition to this engine. See [DAW integration](docs/DAW_INTEGRATION.md).

## Build on Windows 11 x64
Install Visual Studio 2022 or newer with Desktop development with C++, a Windows
SDK and CMake 3.24+. No WDK or custom driver is needed.

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\patchy.exe --help
.\build\Release\patchy.exe list
```

If multiple VS versions are installed, select the generator explicitly, e.g.
`-G "Visual Studio 18 2026"` with a CMake version that supports it, or
`-G "Visual Studio 17 2022"`. Local results are in VALIDATION.md.

## Use
```powershell
.\build\Release\patchy.exe watch --seconds 30
.\build\Release\patchy.exe report > patchy-report.txt
.\build\Release\patchy.exe route --source default --to 'PASTE OUTPUT ENDPOINT ID' --seconds 30
```

For `route`, both source and destination are **output** endpoints. Copy IDs from
`list` and quote them. The source default resolves once; it does not follow changes.
Ctrl+C stops a route. Omit `--seconds` to continue until stopped. `--queue-ms 30`
is a target; actual buffering may be larger. The tool does not change endpoint
volume, format, defaults, driver state or application output preferences.

Original playback stays audible. Exact self-routing is refused. External feedback
through other software/hardware remains possible. Supported initial formats:
1--8 channels, float32 or PCM16/24/32, 8--192 kHz and sample-rate ratios 0.5--2.
Layouts must match unless a channel map is supplied. Unknown multichannel layouts
are rejected. Error messages explain unsupported combinations.

### Monitor an input and assign channels
Use headphones to avoid acoustic feedback when monitoring a microphone.
`monitor` accepts an **input** source and an **output** destination:

```powershell
.\build\Release\patchy.exe monitor --source 'INPUT ENDPOINT ID' --to 'HEADPHONE OUTPUT ID' --map 1,1 --seconds 30
```

`--source default` selects the current default console input for `monitor`.
`--map` supplies one entry per destination channel, in that endpoint's channel
order. Values are 1-based source channel numbers; 0 writes silence. `1,1` duplicates
input channel 1 into stereo; `2,1` swaps stereo; `1,0` sends input 1 left and silence
right. Omit the map for unchanged, matching layouts. Mapping does not mix channels
or change gain. Both `route` and `monitor` support it. Windows microphone privacy
policy applies; access denied is reported without changing that policy.

The bridge reports negotiated buffers/periods, queue depth, correction ppm,
captured/rendered frames, silence, underruns, overflows, discontinuities, timestamp
errors and retries. Silence while idle/priming is not automatically a hardware
dropout. Queue size is not measured total latency. No waveform is saved.

Inactive endpoints remain in inventory; their live mix format is unavailable.
Mix format is not an exhaustive hardware format list. `report` omits identifying
names/IDs/paths/serials/apps/audio. `list` and `watch` deliberately display IDs
and friendly names and should be reviewed before sharing.

Exit codes: 0 success or normal stop, 1 runtime failure, 2 invalid arguments or no
successful stream setup. Unavailable selected devices retry until cancelled;
they do not silently switch to another output.

## Portable processing tests
Non-Windows builds include the processing library and tests, not the Windows CLI.
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
GitHub Actions defines a Windows build/test job and a Linux ASan/UBSan job.
A workflow definition does not imply a hosted run has occurred.

## Project map
- [Vision](VISION.md), [architecture](ARCHITECTURE.md), [Core Audio parity](COREAUDIO_PARITY.md)
- [Roadmap](ROADMAP.md), [limitations](LIMITATIONS.md), [validation](VALIDATION.md)
- `include/patchy`: public format/processing/device/route interfaces
- `src/core`: portable bounded buffers, channel assignment, conversion and clock adaptation
- `src/windows`: MMDevice, notifications, WASAPI and recovery
- `src/cli`: controls and local diagnostics
- `tests`: independent processing tests
- [License](LICENSE), [third-party policy](THIRD_PARTY.md)

Next major target: a genuine signed virtual input/output driver, followed by
aggregate/multi-output devices. The current program is a user-space bridge.

## Validation tools
`tools/validate.ps1 -Configuration Release` runs configure/build/CTest with failure
checking; use `Debug` for the Debug configuration. See
[the opt-in hardware test guide](docs/HARDWARE_TESTS.md) for the quiet signal probe
and manual reconnect, timing and 302USB acceptance matrix.

A conservative timestamp sanity guard stops grossly incompatible clock rates.
Correction is limited to +/-2,000 ppm; two completed windows above 5,000 ppm
relative timestamp-rate error terminate the route. Zero timestamp-rate error during
warmup is not a clock accuracy measurement. A known incompatible Waves/NVIDIA
combination and all local results are documented in VALIDATION.md.
