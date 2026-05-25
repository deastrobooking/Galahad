# Galahad Architecture

## Design goals

- Keep plugin code host-safe and deterministic.
- Abstract transport from control intent.
- Support multiple bridge backends without changing plugin logic.
- Make protocol explicit and versioned.

## Layers

1. Intent layer (`LiveController`)
   - Provides typed high-level operations.
   - No socket or DAW specifics.

2. Protocol layer (`Command`, OSC codec)
   - Maps intents to address + argument tuples.
   - Handles serialization and deserialization.

3. Transport layer (`ITransport`, `OscUdpTransport`)
   - Owns network I/O and background receive loops.

4. Bridge adapters (future)
   - AbletonOSC adapter
   - Python Remote Script adapter
   - MIDI loopback adapter

## Threading model

- Never call transport send from audio callback.
- Use timer/worker thread for outbound commands.
- Receive callbacks should be forwarded to app state thread.

## Extension model

- Add commands to `Command` factories.
- Add transport implementation by inheriting `ITransport`.
- Add bridge adapters as separate modules to keep core lightweight.
