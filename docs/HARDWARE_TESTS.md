# Hardware validation
Run ordinary CTest first. Live tests are opt-in and never run in CI automatically.
Endpoint IDs are machine-specific; copy and quote them from `patchy list`.
Use endpoints whose volume/mute state and physical connections you understand.
No test should silently change defaults, formats, volumes or security settings.

## Build and run the signal probe
```powershell
cmake -S . -B build -DPATCHY_BUILD_HARDWARE_TESTS=ON
cmake --build build --config Release --parallel
.\build\Release\patchy.exe list
.\build\Release\patchy_signal_test.exe 'SOURCE OUTPUT ID' 'DESTINATION OUTPUT ID'
```
Both arguments are output endpoints and must differ. The probe runs about eight
seconds and emits a quiet 997 Hz tone for about six seconds, channel one only.
An optional third argument sets the target queue in milliseconds (10..200).
It measures destination loopback, compares tone to baseline/neighbor energy, and
returns failure for missing tone, bridge underruns/overflows or a failed route.
Endpoint/APO gains affect results. A pass does not establish analog fidelity.
Do not use a muted endpoint as proof that a route delivers audible audio.

## Input capture and channel assignment
`monitor` sends live input to an output. Use headphones for an audible microphone
test. A bounded capture/queue check can instead map every destination channel to
silence, so microphone samples are neither played nor saved:

```powershell
.\build\Release\patchy.exe monitor --source 'INPUT ID' --to 'STEREO OUTPUT ID' --map 0,0 --seconds 12
```

This only checks input stream setup, packet delivery and queue behavior; it is not
signal proof. For a real signal test, inject a known electrical input, use `--map
1,1` for a mono source into stereo, and independently measure the output. Verify
the selected and silent channels separately. Never infer analog quality from frame
counters. Input privacy policy can deny access; use the documented Windows settings
if the user wants to grant it, without changing policy automatically.

## Manual acceptance matrix
Record OS build, driver versions, hardware model/firmware, formats, channel masks,
buffer/queue sizes, correction and timestamp rates, counters and elapsed time.
Keep raw identifying inventories local; share the redacted report when possible.

| Test | Procedure | Required result |
|---|---|---|
| Routing | Play a known signal into source, capture destination independently | Signal, channel order and expected gain; report latency by correlation |
| Input monitoring | Known physical input into chosen headphone/output endpoint | Verified signal, rate, channel order and no acoustic loop |
| Channel map | Select, duplicate, swap and silence channels; supply invalid map | Correct destination channels; invalid maps fail before streaming |
| Input access/stall | Deny microphone permission or stop capture packet delivery in a controlled test | Clear access failure or bounded watchdog/retry; no policy bypass |
| Rates | Physical 44.1 -> 48 and reverse | Correct pitch/duration, bounded queue and drift, no steady xruns |
| Long run | Two hours at representative load and rates | Bounded queue; explain every dropout and discontinuity |
| Idle/restart | Pause source for a minute, restart it | Silence while idle, clean reprime, no stale-audio burst |
| Feedback | Same ID and default resolving to same ID | Rejected before stream activation |
| Hotplug | Remove/reconnect selected USB member | Waiting status, bounded memory, same-ID recovery, no alternate output |
| Identity change | Reconnect in a way Windows assigns a new ID | Remain waiting; require explicit reselection |
| Format change | Change endpoint format in Windows settings during route | Recreate/revalidate or clear unsupported-format failure |
| Sleep/resume | Sleep while route runs, resume | Bounded cancellation, release/reopen when invalidated |
| Missing device | Start with selected endpoint absent/inactive | Backoff, actionable status, no-start exit on timed cancellation |
| Defaults | Change OS default while route runs | Pinned endpoints remain selected |
| Notification | Watch while adding/removing/enabling/disabling endpoint | Snapshot differences; document coalesced transient states |
| Driver contention | Another app opens endpoint exclusively | Explain contention, retry or clear failure |
| Privacy | Export report | No IDs, names, serials, paths, process lists or audio |

Volume/mute changes can affect loopback results. Clock correction is not
hardware synchronization. Gross timestamp-rate mismatch should fail explicitly;
investigate endpoint/driver configuration, not simply add more buffering.

## 302USB
Check USB/PnP presence in Device Manager, then problem codes and driver binding,
then MMDevice state and supported stream formats, then signal flow. Record each
layer separately. An absent MMDevice endpoint does not establish a USB or firmware
diagnosis. Patchy does not install replacement drivers to make this test pass.
