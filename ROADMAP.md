# Roadmap
Milestones close on evidence, not the existence of source files.

## M0: foundation
Five research-backed design documents, explicit license/dependency policy, modular
CMake project, reproducible build, CI definition and honest validation record.

## M1: user-space vertical slice (in progress)
Acceptance:
- Inventory all exposed input/output endpoint states with identity, format, channels,
  masks, defaults and available period data; preserve per-device errors.
- Detect arrival/removal/state/default/property changes without blocking callbacks.
- Bridge shared output loopback to a different physical output; reject self-routing.
- Monitor existing capture endpoints through explicit outputs. Validate direction,
  OS access failures and capture stalls. Select/duplicate/silence channels with
  explicit maps; prove mono/stereo and multichannel routing without crosstalk.
- Demonstrate 44.1/48 kHz both ways, positive/negative drift, bounded queue, no callback
  allocations, silence/restart, explicit rejection of unsupported layouts/formats.
- Publish actual buffer/queue data and counters. Measure latency by independent
  correlation, not by interpreting requested buffer sizes as end-to-end latency.
- Recover after USB replug, disable/enable, format change, sleep/resume and service
  interruption while retaining selected endpoints and bounded cancellation.
- Tests cover formats, PCM edges, channel separation, buffer wrap, alias rejection,
  drift, underrun/overflow recovery, CLI errors and feedback protection.
- Two-hour physical routing runs at 44.1/48 kHz, with hardware/driver/OS/load records,
  independent capture and audible verification. Test x64 Release and Debug.

See VALIDATION.md for completed checks and open acceptance cases.

## M2: genuine virtual input/output (next major target)
Ordinary unrelated apps see persistent named stereo input/output endpoints and can
exchange audio. Monitoring/recording work concurrently. Engine death yields silence
without kernel stalls. Installation/uninstallation use supported signing with no
security bypass. Review kernel concurrency, PnP/power, IOCTL/memory security,
fuzzing, static analysis, Verifier/HLK and licensing before release. Confirm the
minimal driver architecture and signed test/distribution path before importing SysVAD.

## DAW integration track: explicit ASIO frontend
After input/output transport is stable, build a host-facing ASIO frontend so
Windows DAWs can select Patchy. This is distinct from opening vendor ASIO hardware
and from exposing virtual Windows endpoints. Resolve SDK licensing first. Acceptance:
Ableton Live selects ASIO/Patchy, records and plays simultaneously, reports correct
channels/rate/latencies, survives start/stop/reconfiguration and handles engine loss
without a host crash or deadlock. Validate callbacks and long runs under load.
See [DAW integration](docs/DAW_INTEGRATION.md). No DAW support is claimed today.

## M3: aggregate and multi-output devices
Two physical inputs and two outputs with saved channel slots, selectable master,
per-member drift/latency correction, missing-member policy and virtual exposure.
Eight-hour bounded-drift/hotplug runs and measured alignment. Document shared versus
exclusive multichannel limits and unsynchronized hardware constraints.

## M4: ordinary-user reliability
Atomic versioned configuration, per-user optional background agent, compact settings,
optional tray, explicit startup policy, persistent errors and private report export.
No telemetry/account/default-device changes. Test 302USB from PnP through real duplex
signal flow. Publish device/firmware/driver-specific evidence.

## M5: professional breadth
Exclusive backend, weighted channel matrices, multi-client graph, process loopback,
optional license-reviewed vendor-ASIO backend, improved SRC quality metrics, isolated plugins and latency
compensation. Expand built-in/USB/HDMI/Bluetooth/vendor-interface testing.

## M6: ecosystem
Stable versioned client/node APIs, MIDI timing, file/conversion services, examples,
compatibility testing, reproducible releases and a maintained public hardware matrix.
