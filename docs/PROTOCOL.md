# Galahad Protocol

Galahad has two bridge lanes:

- OSC over UDP for C++/Python bridge commands and telemetry.
- MIDI Remote Script messages for Push/APC-style Ableton control.

## OSC Transport

Default OSC transport is localhost UDP.

```text
Bridge receive port: 11000
C++ event port:      11001
```

Core commands:

```text
/live/song/start_playing
/live/song/stop_playing
/live/song/stop_all_clips
/live/song/set/tempo <bpm:float>
/live/scene/fire <scene:int>
/live/clip/fire <track:int> <clip:int>
/live/track/set/volume <track:int> <value:float>
```

Telemetry events sent back to C++:

```text
/galahad/event/song_started
/galahad/event/song_stopped
/galahad/event/tempo <bpm:float>
/galahad/event/transport <beat:float> <bpm:float>
/galahad/event/clip_fired <track:int> <clip:int>
/galahad/event/track_volume <track:int> <value:float>
```

Future handshake:

```text
/galahad/hello <version:string> <capabilities:string>
/galahad/error <code:int> <message:string>
```

## MIDI Remote Script Protocol

The MIDI lane is compact and designed for an 8x8 visible session ring. All
messages currently use MIDI channel 1.

```text
note on status: 0x90
cc status:      0xB0
```

### Session Grid

```text
tracks: 8
scenes: 8
base note: 36
note = 36 + scene * 8 + track
track = 0..7
scene = 0..7
velocity > 0 launches the clip slot
```

Example:

```text
90 24 64
```

This launches track 0, scene 0 because note `0x24` is decimal `36`.

### Clip Feedback

Live sends note feedback using the same grid note numbers.

```text
12  empty
40  stop button available
80  clip loaded/stopped
100 triggered/queued
110 recording
127 playing
```

Clients should treat unknown lower values as empty and unknown higher values as
active.

### Transport

Commands to Live:

```text
CC 110 value 0 = stop
CC 110 value 1 = play
CC 110 value 2 = toggle record
CC 110 value 3 = continue playing, if supported
CC 110 value 4 = tap tempo, if supported
CC 110 value 5 = toggle metronome
```

Feedback from Live:

```text
CC 117 value 0/127 = stopped/playing
CC 113 value 0/127 = record off/on
CC 114 value 0/127 = metronome off/on
```

Play feedback intentionally uses CC `117`, not CC `110`, so feedback does not
look like a transport command.

The Remote Script also accepts CC `113` from Galahad as an explicit record-mode
set command. Value `0..63` turns record off; value `64..127` turns record on.

### Session Navigation

```text
CC 111 value < 64  = move track bank left by 8
CC 111 value >= 64 = move track bank right by 8
CC 112 value < 64  = move scene bank up by 8
CC 112 value >= 64 = move scene bank down by 8
CC 119 value 0..3  = select automation clip section C1..C4
```

The Remote Script clamps banks to available Live tracks/scenes. Clip section
selection divides the last Galahad-launched clip, or the currently highlighted
clip if no Galahad clip has been launched, into four equal beat ranges and moves
that clip's start marker to the selected range.

### Scene And Stop Commands

```text
CC 115 value 0..7 = launch scene in current scene bank
CC 118 value 0..7 = stop clips on that track in current track bank
```

Stop one clip slot:

```text
F0 7D 47 48 01 12 track scene F7
```

## Mixer Bank

All mixer controls address the current 8-track bank. Galahad prefers dedicated
SysEx commands for app-to-Live mixer actions so the Ableton-only mixer surface
does not overlap with normal MIDI controller CC assignments. Live still sends
compact CC feedback so the UI can stay synchronized.

Direct mixer value command:

```text
F0 7D 47 48 01 40 track parameter value F7
```

Mixer value parameter ids:

```text
0 volume
1 pan
2 send A
3 send B
4 send C
```

Direct mixer action command:

```text
F0 7D 47 48 01 41 track action F7
```

Mixer action ids:

```text
0 mute toggle
1 solo toggle
2 arm toggle
3 select track
4 stop track clips
```

Legacy CC map:

```text
CC 20-27 = track volume
CC 28-35 = track pan feedback
CC 36-43 = send A
CC 44-51 = send B
CC 52-59 = send C
CC 68-75 = mute toggle / feedback
CC 76-83 = solo toggle / feedback
CC 84-91 = arm toggle / feedback
CC 92-99 = select track / selected feedback
```

Continuous controls:

```text
0   minimum
64  midpoint
127 maximum
```

Buttons:

```text
Client sends 127 to toggle.
Live sends 0 or 127 as current state.
```

## Selected Device Bank

The selected Live device is exposed as an 8-parameter bank.

```text
CC 100-107 = selected device parameters 1-8 in current bank
CC 108     = device bank left
CC 109     = device bank right
CC 116     = selected device activator toggle
```

Feedback:

```text
CC 100-107 = current parameter values
```

Names are not sent in this MIDI protocol. A higher-level OSC or Max for Live
bridge should provide device names, parameter names, and display strings.

## SysEx Envelope

Galahad uses the non-commercial SysEx ID during development:

```text
F0 7D 47 48 version command payload... F7
```

Fields:

```text
F0      SysEx start
7D      non-commercial manufacturer ID
47 48   ASCII-ish GH project tag
01      protocol version
command command byte
payload 7-bit payload bytes
F7      SysEx end
```

Legacy refresh is accepted:

```text
F0 7D 47 48 01 F7
```

## SysEx Commands

```text
01 refresh all
02 refresh session
03 refresh transport
04 refresh mixer
05 refresh selected device
11 set session offset
12 stop clip slot
14 launch scene
20 select track
30 set selected device bank
40 set mixer value
41 mixer action
```

Examples:

```text
F0 7D 47 48 01 01 F7                  refresh all
F0 7D 47 48 01 11 trackBank sceneBank F7
F0 7D 47 48 01 12 track scene F7
F0 7D 47 48 01 14 scene F7
F0 7D 47 48 01 20 track F7
F0 7D 47 48 01 30 bank F7
F0 7D 47 48 01 40 track parameter value F7
F0 7D 47 48 01 41 track action F7
```

## Versioning

All payload values must be `0..127`.

## Implementation Status

Implemented or scaffolded:

- OSC command codec and UDP transport.
- Real-time-safe C++ event mapper and SPSC handoff queue.
- JUCE MIDI protocol helpers.
- 8x8 clip launch.
- Clip state feedback.
- Track and scene bank navigation.
- Transport commands and feedback.
- Scene launch.
- Track stop.
- Per-clip stop by SysEx.
- Mixer volume, pan, sends A/B/C, mute, solo, arm, select, stop.
- Selected-device 8-parameter bank.
