# DAW integration: what must exist before Ableton can select Patchy

The current CLI is an application using WASAPI. It can capture an existing input
and render to an existing output, or bridge output loopback. It does not register
an ASIO driver, install a Windows endpoint or supply a DAW callback API.

On Windows, our intended Ableton workflow is **Driver Type: ASIO; Audio Device:
Patchy**. This is a design target, not an available option. Core Audio is Apple's
platform API; Patchy cannot add that API to the Windows version of Ableton.
[Ableton's ASIO guidance](https://help.ableton.com/hc/en-us/articles/360003149240-Using-an-ASIO-audio-driver-on-Windows).

## Separate integration boundaries

| Component | Responsibility | Status |
|---|---|---|
| WASAPI hardware backend | Open Windows capture/render endpoints and adapt independent clocks | Shared input monitoring and loopback bridge implemented |
| Host-facing ASIO frontend | Present Patchy channels, callback buffers, timestamps and latencies to an ASIO host | Not implemented |
| Virtual Windows driver | Make ordinary applications see persistent Patchy playback and recording endpoints | Planned next major subsystem |
| Vendor-ASIO backend | Open a manufacturer's existing ASIO driver through Patchy | Optional later backend; not required for WASAPI hardware |

The ASIO frontend can in principle work with a user-space engine independently of
the virtual kernel driver. The driver is needed for ordinary system-wide Windows
endpoints. Neither automatically grants the other's capabilities.

## Proposed implementation contract

Keep the host-loaded frontend small. Put hardware ownership, graph management and
recovery in the engine. A versioned, bounded client transport provides buffers and
timestamps, validates dimensions/sequence numbers, and never trusts shared offsets.
Whether the first frontend uses this out-of-process transport or an in-process
engine is an engineering decision gated by measured latency and failure isolation.

Expose a stable ordered channel list, supported rates, buffer sizes, sample formats
and input/output latency. Double-buffer ownership and callbacks must follow the
chosen SDK contract exactly. The host callback must not allocate, log, take locks
or wait for a device or control-plane response. A missed deadline yields bounded
silence and diagnostics. Engine loss must stop or silence safely without hanging
the DAW. Recovery must discard stale samples and reconcile host state explicitly.

For independent hardware clocks, choose a timeline/master and adapt other clock
domains. Host sample position must be monotonic within a stream epoch and correlated
with system time. Report all known buffering/conversion delay; measure the residual
with an independent round-trip test. Sample-rate changes require a controlled reset,
not silent changes underneath active host buffers.

The present single-worker monitor is not yet this client protocol or a qualified
low-latency full-duplex DAW engine. Device identity, processing and recovery code
can be reused, but the protocol and its timing behavior need dedicated validation.

## Acceptance before claiming Ableton support

1. Choose compatible ASIO SDK licensing and retain all required notices. No SDK
   code is currently included; see [licensing policy](../THIRD_PARTY.md).
2. An x64 test host enumerates and opens Patchy, reports its channels/rate/buffers,
   and repeatedly creates, starts, stops and disposes streams without leaks.
3. Ableton records a known physical input while independently playing to hardware.
   Verify channel isolation, correct pitch/duration, reported latency and measured
   round-trip timing at 44.1/48 kHz and representative buffer sizes.
4. Sustain eight hours under representative CPU/I/O load with bounded memory and
   queues, documented dropout/discontinuity counters, and host/engine crash tests.
5. Exercise hardware removal, engine restart, exclusive contention and host-driven
   rate/buffer changes. No callback deadlocks, stale playback bursts or host crash.
6. Validate install/uninstall and 64-bit registration without weakening Windows
   security, and document any elevation required for system-wide registration.

Microsoft's forthcoming [low-latency UAC2/ASIO work](https://github.com/microsoft/low-latency-audio)
may improve the hardware foundation. As reviewed 2026-10-19 its README says there
is no public driver release yet, and its Microsoft-specific Steinberg agreement
does not transfer to downstream builders. It does not provide Patchy's routing,
aggregate policy, client transport or host-facing identity automatically.
