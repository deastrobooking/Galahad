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

## Modular Engine

The reusable engine lives in `MidiToolsEngine` and is used by the JUCE MIDI
Tools processor:

- `AlgorithmicSequencer`: fixed-size step sequencer with Euclidean pattern fill,
  per-step probability, transpose, velocity, and gate.
- `MidiLfo`: MIDI CC LFO with sine, triangle, saw, square, and sample-hold
  shapes.
- `MidiMergerRouter`: deterministic event merge plus simple channel remap,
  transpose, velocity offset, and note/CC filtering rules.
- `MidiToolsEngine`: combines the modules into one process call for plugins,
  command-line tools, or future bridge adapters.

Current plugin defaults:

```text
Sequencer: 16 steps, 5 Euclidean pulses, root note 48, channel 1, fixed channel mode
LFO:       CC 74, channel 1, triangle, 0.2 Hz, range 24..104
Routing:   pass input through to channel 1
```

## Plugin Parameters

The VST3/Standalone target is a silent MIDI-source instrument with a built-in
controller mapper. It exposes the modular engine as automatable plugin
parameters. Ableton can map these directly, and the existing selected-device
Remote Script controls can edit them when `Galahad MIDI Tools` is the selected
device.

First device bank:

```text
Seq Run
Seq Ch
Seq Ch Mode
Seq Root
Seq Rate
Seq Steps
Seq Pulses
Seq Rotate
```

Second device bank:

```text
Seq Prob
Seq Gate
Seq Vel
LFO On
LFO Ch
LFO CC
LFO Shape
LFO Rate
```

Third device bank:

```text
LFO Min
LFO Max
Route On
Route In Ch
Route Out Ch
Route Transpose
Route Vel
Route Notes
```

Fourth device bank:

```text
Route CCs
Hardware Capture
Map Thru
Map 1 On
Map 1 In Ch
Map 1 In CC
Map 1 Out Ch
Map 1 Out CC
Map 1 Min
Map 1 Max
```

Additional banks expose the same controller mapping parameters for map slots 2
through 4.

`Sequencer Rate` is in steps per beat. The default value `4.0` means sixteenth
notes at the host tempo. `Route Input Channel` accepts `0` for omni.
`Sequencer Channel Mode` controls note output channel selection:

```text
Fixed   = every sequencer note uses Seq Ch
Rotate  = notes advance through channels from Seq Ch
Random  = each triggered note chooses a random channel
Step    = step 1..16 maps directly to channel 1..16
```

## Master Setup Page

The plugin editor starts with a master setup page for performance mapping:

```text
Controller slots  1..8 visible hardware/controller contexts
Layer buttons     A..D performance mapping contexts
Target channels   1..16 quick channel focus buttons
Clip circles      C1..C4 automation clip contexts
```

The mapper model supports up to 8 captured controller inputs. Each controller
slot has 8 fader-style controls and 15 button-style controls. Every one of
those 23 controls has four layer mappings named `Map 1` through `Map 4`.

`Controller Layer` selects which surface layer is active. The editor's A-D layer
buttons update that parameter. Surface mappings are evaluated only for the
hardware input slot that produced the MIDI event, so two controllers can use the
same CC numbers without colliding once Galahad has opened them directly.

Each surface mapping has:

```text
On
Input channel / CC
Output channel / CC
Min / Max
```

The compact four-row mapper remains available as a legacy/global mapper and
still works for host-routed MIDI input. The 8-controller surface mapper is meant
for Galahad's direct hardware capture lane.

## Controller Mapper

The editor includes four controller-map slots for Drop-style performance
layering:

```text
Hardware     opens Akai/Novation controller inputs directly
Rescan       rechecks connected MIDI inputs
Map On       enables the slot
Learn        captures the next incoming CC into the input channel and CC fields
Input        matches a specific channel or Omni
Output       emits a remapped CC on a selected MIDI channel
Min/Max      scales or inverts the outgoing value range
Map Thru     keeps the original mapped CC alongside generated outputs
```

Multiple slots can listen to the same input CC, so one hardware knob can fan out
to several destinations. With `Map Thru` off, matched source CCs are intercepted
and replaced by their mapped outputs. With `Map Thru` on, Galahad layers the
mapped outputs on top of the original controller stream.

The editor visualizes the last incoming CC, the last mapped output, and per-slot
activity.

Hardware capture currently auto-opens input devices whose names include:

```text
Akai
MIDImix
MIDI Mix
Novation
Launch Control
```

If Ableton is also routing the same physical controller ports into the Galahad
track, disable that duplicate track input or turn `Map Thru` off to avoid doubled
controller messages.

## Drop-Inspired Roadmap

Galahad is moving toward a software performance-control brain inspired by
standalone snapshot controllers:

```text
1. Controller layers and macro fan-out
2. Snapshot capture and recall for every map/sequencer/router state
3. Timed Drop transitions over 1..32 bars
4. Curves per macro destination
5. OSC and Remote Script hooks for Live clips, tracks, devices, and scenes
```

## Ableton Control Workflow

Galahad supports two Wolfgang-style MIDI output routes.

### Host Track Output

1. Put the plugin on MIDI Track 1 as a VST3 instrument/source.
2. Create MIDI Track 2 for capture or monitoring.
3. On Track 2, set `MIDI From` to Track 1, then choose `Post FX` or
   `Galahad MIDI Tools` if Live exposes the device by name.
4. Arm Track 2 or set monitoring to `In`.

### Native Virtual Output

The plugin also creates a native virtual MIDI source named:

```text
Galahad 1
```

To record from it, create a MIDI track and set `MIDI From` to `Galahad 1`.
The port is created when the plugin is loaded and processing, so reopen Live's
MIDI chooser or restart Live if it does not appear immediately.

## Remote Control Workflow

1. Select `Galahad MIDI Tools` in Live's device view.
2. Use the Galahad Remote Script selected-device bank controls (`CC 100-107`,
   `CC 108`, `CC 109`) to edit the plugin's parameter banks.
3. Route the plugin MIDI output to Live, a hardware synth, or the Galahad Remote
   Script lane depending on whether you want generated notes, generated CCs, or
   session-control feedback.
