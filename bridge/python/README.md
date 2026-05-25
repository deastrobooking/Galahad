# Python Bridge Stub

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

`GalahadMidiToolsRemoteScript` is an Ableton MIDI Remote Script skeleton for the
optional JUCE MIDI tools plugin.

- note 36 maps to track 0, scene 0
- the grid is 8 tracks by 8 scenes
- CC 110 controls stop/play/record toggle
- CC 111 and CC 112 bank tracks/scenes

Copy the `GalahadMidiToolsRemoteScript` folder into Live's MIDI Remote Scripts
directory, restart Live, then select it in Live's MIDI preferences.
