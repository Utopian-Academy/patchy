# Licenses and provenance
First-party code is MIT. No third-party DSP/runtime library or sample implementation
is incorporated. Windows APIs, the SDK, MSVC and the C++ runtime are external
platform/development prerequisites under their own terms.

Reviewed 2026-10-18:
- Windows-driver-samples root is **MS-PL**, not blanket MIT. Review individual files
  and dependencies before importing SysVAD; retain notices and required source
  license. Current code is original.
  [Repository license](https://github.com/microsoft/Windows-driver-samples/blob/main/LICENSE).
- Current Steinberg ASIO SDK offers GPLv3 or proprietary terms, with file-specific
  qualifications. A future distribution needs an explicit compatible licensing
  decision; an optional DLL boundary is not assumed to remove obligations.
  No ASIO SDK is bundled.
  [SDK terms](https://github.com/audiosdk/asio/blob/main/LICENSE.txt),
  [Steinberg](https://www.steinberg.net/developers/).
- Future dependencies require pinned version/source, SPDX/notice records,
  redistribution and compatibility review. References are links, not copied code.

Reviewed 2026-10-19: Microsoft's [low-latency audio project](https://github.com/microsoft/low-latency-audio)
is a useful UAC2/ASIO reference, not a bundled dependency. Its README explicitly
states Microsoft's Steinberg agreement does not transfer to other builders and
does not grant an ASIO SDK license. No code from it has been incorporated.
