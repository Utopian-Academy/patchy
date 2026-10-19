# Core Audio parity
This map describes the destination and Windows building blocks, not delivered parity.
Apple's architecture spans HAL, Audio Units, Audio Toolbox and Core MIDI. Patchy
does not implement Apple's proprietary APIs or AU binary compatibility.
[Apple overview](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/WhatisCoreAudio/WhatisCoreAudio.html),
[current aggregate API](https://developer.apple.com/documentation/coreaudio/audiohardwareaggregatedevice).

| Capability | Windows foundation / constraint | Patchy status |
|---|---|---|
| Unified hardware discovery | MMDevice, later topology/PnP; vendor-only interfaces can be separate | Inventory and notifications implemented |
| Class-compliant USB | Inbox UAC1/UAC2 subject to descriptors and driver binding | Use existing drivers; PnP diagnostics later |
| Format/channel discovery | Shared mix plus individual supported-format probes | Mix/channel/period display; exhaustive list not claimed |
| Low-latency streams | WASAPI events, hardware-dependent periods | Shared bridge; exclusive later |
| Stable configuration | Endpoint identities can change | IDs pinned for route lifetime; persistence later |
| System playback capture | Shared endpoint loopback, protected-content restrictions | Implemented |
| Physical input monitoring | Shared event-driven WASAPI capture and render | Implemented; bounded live capture smoke tested, analog path qualification pending |
| Channel assignment | Prepared user-space channel map | Selection, duplication and silence implemented; weighted mixing later |
| DAW-selectable Patchy device | Separate host-facing ASIO frontend; virtual endpoints do not imply ASIO | Planned; Ableton integration not delivered |
| Per-app capture | Supported process-tree loopback on modern Windows | Planned; capture does not force rerouting |
| App-to-app input/output | Standard capture/render endpoints require a driver | Genuine virtual device is next major target |
| Aggregate devices | User engine combines clocks; virtual driver exposes device | Planned after driver |
| Multi-output | Per-output drift and delay compensation | One destination now |
| Concurrent clients | Windows shared mode; future virtual transport/mixing | One route per process; no shared Patchy service yet |
| Audio Unit-like processing | Independent graph and optional isolated hosts | SRC/format core now; nodes/plugins later |
| Audio Toolbox-like services | File/codec/converter facilities with license review | Planned |
| MIDI synchronization | Shared timestamp model and supported Windows facilities | Later subsystem |
| Reconnect/sleep survival | Release/recreate invalidated clients | Retry path implemented; full hardware matrix pending |
| System-wide policy simplicity | Third parties cannot override every app/driver/OS policy | Supported integration only |

Ordinary user-space inventory and shared streams need no driver install/elevation,
subject to OS permissions. Virtual driver installation is privileged and signing is
a distribution requirement. Perfect hardware sync, firmware repair and universal
forced application routing are outside a user-space layer's power.
