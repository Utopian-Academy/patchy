# Limitations
## Current implementation
Patchy is a shared WASAPI application-level bridge. It installs no endpoint or
driver, exposes no microphone, changes no default output and does not move an
existing application's original playback. A loopback source remains audible.
The `monitor` command opens an existing input and sends it to a selected output;
it does not expose that input to a DAW through a Patchy driver or record a file.

No aggregate device, persisted configuration, GUI, exclusive stream, ASIO, plugin
host or MIDI support exists yet. One route runs per process. Cross-process,
hardware and acoustic feedback cannot be detected; same-endpoint routing is refused.

Formats are limited to float32 or PCM16/24/32, 8--192 kHz, 1--8 channels,
rate ratios 0.5--2. Layouts must match unless explicitly mapped. Maps select,
duplicate or silence channels; weighted mixing is not implemented. Unknown
multichannel layouts are rejected. The adaptive
SRC compensates modest drift, bounded at +/-2000 ppm. It is not bit-perfect,
mastering-quality-certified or hardware-synchronous.

The queue target may be increased to accommodate endpoint buffers. Total latency
also includes capture/render engines, scheduling, filtering, drivers and converters.
Source silence can stop loopback packets; priming/underrun counters do not by
themselves demonstrate a hardware dropout. Watchdogs monitor render events and,
for input monitoring, capture packet delivery. Microphone privacy policy can deny
input access; Patchy does not bypass it.
Endpoint IDs are pinned; if Windows assigns a new ID, reselect it.

## Verified platform boundaries
- Shared endpoint loopback is supported, including event mode on Windows 11.
  Exclusive-mode and protected audio are not universally capturable.
  [Microsoft](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording).
- GetMixFormat is a shared-engine format, not a hardware capability inventory.
  [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getmixformat).
- Inbox UAC2 support depends on valid descriptors and driver matching; Windows
  documents restrictions on arbitrary >8-channel shared formats.
  [Microsoft UAC2](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers).
- Process-loopback copies a process tree's audio; it does not move or mute the
  original output. Patchy does not use undocumented app-routing APIs.
  [Microsoft sample](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/).
- Driver installation is privileged; distribution needs supported signing and
  certificate infrastructure. No signature enforcement or security bypass is allowed.
  [Signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs).

Independent oscillators require correction, and software cannot guarantee phase
lock. Bluetooth radio/codec/profile latency, HDMI sink availability and vendor
drivers remain hardware/OS constraints. Windows is not a hard real-time OS;
DPC/ISR load, power policy and other clients can cause glitches.

## 302USB boundary
MMDevice inventory alone cannot distinguish failed USB enumeration from a driver
that never created an endpoint. Start with cable/port/power and Device Manager.
If PnP reports failed start, record its problem code and driver binding. If the
device starts but no endpoint appears, investigate descriptors/topology/driver.
If an endpoint exists, distinguish inactive state, format/access rejection, app
output selection, mute and actual signal flow. Automated PnP correlation and a
physical 302USB test are pending. Do not claim to fix firmware or USB/PnP failures.

## Validation boundary
Source code and passing unit tests do not establish all hardware behavior.
See VALIDATION.md for the exact local evidence and remaining tests.

The initial Waves SoundGrid 44.1 kHz -> NVIDIA 48 kHz test exhibited a sustained
rate mismatch far beyond the correction range. It is not a supported reliable
combination on the tested configuration. The timestamp guard now stops it with an
explicit clock/configuration error. This is not a diagnosis of a vendor firmware
or driver defect, and the entire Waves product family is not inferred to fail.
A wider queue did not fix that mismatch. See VALIDATION.md for the measured runs.
