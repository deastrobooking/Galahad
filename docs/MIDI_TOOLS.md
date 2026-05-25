# Galahad MIDI Tools

The optional JUCE MIDI tools layer follows the compact session-grid convention
from the Wolfgang research notes, adapted for Galahad's `GH` protocol tag.

## Build

The core library does not require JUCE. Enable the JUCE plugin explicitly:

```sh
cmake -S . -B build-juce -DGALAHAD_BUILD_JUCE_MIDI_TOOLS=ON
cmake --build build-juce --target GalahadMidiTools_VST3
```

On macOS, the VST3 bundle is ad-hoc signed after build. To copy it into the
local VST3 folder:

```sh
cmake -S . -B build-juce \
  -DGALAHAD_BUILD_JUCE_MIDI_TOOLS=ON \
  -DGALAHAD_COPY_PLUGIN_AFTER_BUILD=ON
```

## MIDI Map

The 8x8 clip grid uses note-on messages on channel 1:

```text
note = 36 + scene * 8 + track
track = 0..7
scene = 0..7
```

Transport, bank, mixer, and device controls are documented in
`docs/PROTOCOL.md`. The short core map is:

```text
CC 110 = transport
CC 111 = track bank
CC 112 = scene bank
CC 115 = launch scene in current bank
CC 118 = stop track clips in current bank
```

Clip feedback uses note velocity:

```text
12  = empty
40  = stop button available
80  = loaded
100 = triggered
110 = recording
127 = playing/launched
```

SysEx uses the educational/non-commercial manufacturer ID and Galahad tag:

```text
F0 7D 47 48 01 <command> <payload...> F7
```

## Ableton Remote Script

Copy `bridge/python/GalahadMidiToolsRemoteScript` into Live's MIDI Remote
Scripts folder, restart Live, then select it in Live's MIDI preferences. The
script maps incoming grid notes to Session View clip slots, supports mixer and
selected-device banking, and sends feedback notes/CCs back to the selected MIDI
output.

Recommended macOS install path:

```text
~/Music/Ableton/User Library/Remote Scripts/GalahadMidiToolsRemoteScript/
```

Recommended Windows install path:

```text
\Users\<username>\Documents\Ableton\User Library\Remote Scripts\GalahadMidiToolsRemoteScript\
```

The older app-bundle path also works for local testing, but the User Library path
survives Live upgrades more cleanly.

## Algorithmic Engine

The first engine scaffold lives in the JUCE MIDI Tools processor and reusable
core headers:

- `AlgorithmicSequencer`: fixed-size step sequencer with Euclidean pattern fill,
  per-step probability, transpose, velocity, and gate.
- `MidiLfo`: MIDI CC LFO with sine, triangle, saw, square, and sample-hold
  shapes.
- `MidiMergerRouter`: deterministic event merge plus simple channel remap,
  transpose, velocity offset, and note/CC filtering rules.

Current plugin defaults:

```text
Sequencer: 16 steps, 5 Euclidean pulses, root note 48, channel 1
LFO:       CC 74, channel 1, triangle, 0.2 Hz, range 24..104
Routing:   pass input through to channel 1
```

The next layer should expose these as plugin parameters and Remote Script
controls:

```text
sequencer run, root, rate, pulses, rotation, probability
LFO target CC, shape, rate, min, max
route input channel, output channel, transpose, velocity offset, filters
```
