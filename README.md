# Galahad

Galahad is an open-source framework for extending Ableton Live control from outside Max for Live by combining:

- a host-safe C++ core (usable inside JUCE VST3/AU plugins)
- a bridge layer (OSC, WebSocket, or MIDI loopback)
- adapters that map high-level commands to Ableton endpoints

## Why this exists

Standard plugin APIs cannot natively control arbitrary Live tracks, clips, or UI state. Galahad provides an explicit bridge architecture so you can build those workflows in a portable, testable way.

## Current scope

- OSC command model
- UDP transport with minimal OSC encoder/decoder
- convenience API for clip launch, transport, and track volume
- Python bridge stub for Ableton-side integration

## Repository layout

- `include/galahad`: public C++ API
- `src`: framework implementation
- `docs`: architecture and protocol notes
- `bridge/python`: bridge stubs and experiments
- `examples`: integration examples

## Build

1. Configure:
   - `cmake -S . -B build`
2. Build:
   - `cmake --build build`
3. Run example CLI:
   - `./build/galahad_cli`

## Optional JUCE MIDI tools build

Galahad can also build a small JUCE MIDI tools plugin inspired by the Wolfgang
session-grid workflow:

- `cmake -S . -B build-juce -DGALAHAD_BUILD_JUCE_MIDI_TOOLS=ON`
- `cmake --build build-juce --target GalahadMidiTools_VST3`

See `docs/MIDI_TOOLS.md` for the note map, transport CCs, and Ableton Remote
Script setup.

The MIDI tools scaffold now includes a reusable modular engine, automatable
plugin parameters, an algorithmic Euclidean sequencer, MIDI CC LFOs, and
merge/reroute primitives that can run inside the JUCE MIDI plugin.

## Ableton integration options

1. AbletonOSC-compatible script in Live:
   - use Galahad OSC commands against localhost bridge ports
2. Custom Python Remote Script:
   - implement an endpoint translator from Galahad protocol to Live API calls
3. Hybrid Max helper device:
   - route bridge packets between plugin and Live Object Model

## JUCE plugin usage

Inside your JUCE `AudioProcessor`, keep Galahad traffic off the real-time audio thread. Trigger bridge messages from timer callbacks, async queues, or lock-free command buffers.

See `examples/juce-plugin/IntegrationNotes.md`.

## Roadmap

- WebSocket transport
- MIDI loopback adapter and mapping layer
- authorization and capability negotiation
- host profiling matrix (Live, Bitwig, Logic, Reaper)
- unit/integration tests with mock bridge

## License

MIT
