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
