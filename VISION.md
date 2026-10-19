# Patchy
**Open audio infrastructure for Windows.** Name availability has not been reviewed.

## Purpose
Give Windows a coherent, dependable audio experience inspired by Apple's Core Audio:
a consistent hardware abstraction, low-latency streams, routing, virtual and aggregate
devices, processing, timing and diagnostics. This is an independent implementation,
not Apple's APIs or a claim of current parity. A virtual cable is one component.

## Everyday use
Plug in a supported interface and use it. Choose a simple connection once, then
rarely open settings. Keep device/channel choices through app restarts, reconnects
and ordinary system changes. No account, subscription, ads, artificial channel
limits or default telemetry. Everything we develop is free and open source.

The eventual compact settings app has Devices, Connections and Diagnostics.
Device details expose sample rate, format, channels, clocks, latency and health.
Advanced users can edit channel maps, aggregates and processing graphs. Basic
users see a source, destination and understandable status, not a mixing console.
An optional tray entry can pause connections and surface persistent errors.
Never silently fall back from headphones to speakers or move private audio.

A saved virtual device retains its name and channel identity. Missing aggregate
members preserve channel slots and produce silence until they return. Ambiguous
hardware replacements require explicit rebinding rather than matching names.

## Engineering promise
Build on Windows and its class drivers. Do not replace Microsoft kernel components
to hide a settings problem. Keep kernel code small and carefully reviewed. Publish
limitations, failures and reproducible measurements. Distinguish a user-space
bridge, a Windows-recognized endpoint and a synchronized aggregate.

High fidelity, multichannel support and low latency all matter. Sample-rate
conversion is not bit-perfect transfer. Independently clocked devices cannot become
hardware-locked through software. Report measured latency and its conditions.

First-party source is MIT. Dependencies must remain compatible with an open build
and distribution. Signing and hardware testing have real project costs even
though users will not pay to unlock functionality.

## Breadth
Over time: USB Audio Class 1/2, built-in audio, HDMI/DisplayPort, Bluetooth and
supported professional interfaces; shared/exclusive backends; multiple clients;
virtual endpoints; aggregates; app capture/routing; processing nodes and isolated
plugins; MIDI synchronization and file/conversion services. No DAW workflow is
required. BlackHole's unobtrusive simplicity guides the experience.

## Current scope
The initial native CLI inventories endpoints, watches changes, exports a redacted
snapshot, bridges one shared output loopback to another output, and monitors an
existing input through an explicit output with channel assignment. It creates no
virtual microphone, ASIO frontend or kernel driver. Hardware acceptance, reconnection and long-duration
tests remain necessary before calling the first milestone complete.

The Behringer Xenyx 302USB is a required acceptance device. Diagnose USB enumeration,
driver start, endpoint state, format negotiation and signal routing separately.
Never promise user-mode software can repair firmware or failed USB/PnP enumeration.
