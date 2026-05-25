# Python Bridge And Remote Script

This folder contains a simple UDP forwarder used while developing Galahad.

## Run

- `python3 ableton_bridge_stub.py --listen-port 11000`
- optionally forward packets:
  - `python3 ableton_bridge_stub.py --listen-port 11000 --forward-host 127.0.0.1 --forward-port 12000`

## Production use

To control Ableton Live internals, run a compatible Ableton-side endpoint:

- AbletonOSC or equivalent Live API script
- a custom MIDI Remote Script exposing bridge routes

Then point Galahad transport to that endpoint.

## MIDI tools Remote Script

`GalahadMidiToolsRemoteScript` is the Ableton MIDI Remote Script for the
optional JUCE MIDI tools plugin. Copy the whole folder into Live's User Library
Remote Scripts folder, restart Live, then choose it in Live Preferences > Link,
Tempo & MIDI.

- note 36 maps to track 0, scene 0
- the grid is 8 tracks by 8 scenes
- CC 110 controls stop/play/record toggle
- CC 111 and CC 112 bank tracks/scenes
- CC 113 sets record mode off/on when received from Galahad
- CC 119 selects automation clip sections C1..C4

Recommended macOS install path:

```text
~/Music/Ableton/User Library/Remote Scripts/GalahadMidiToolsRemoteScript/
```

The Remote Script tracks the last clip launched from the Galahad grid and uses
that clip for C1..C4 automation-section start changes. If no Galahad-launched
clip is known yet, it falls back to Live's highlighted or detail clip.
