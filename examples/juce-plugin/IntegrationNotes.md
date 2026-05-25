# JUCE Integration Notes

## Goal

Use Galahad from a JUCE plugin without violating real-time audio thread safety.

## Recommended pattern

1. Own a `galahad::LiveController` inside your `AudioProcessor`.
2. Connect/disconnect in `prepareToPlay` and destructor or suspend callbacks.
3. Push user actions from GUI thread into a lock-free queue.
4. Drain the queue on a timer thread and call Galahad transport methods there.
5. Avoid direct socket operations inside `processBlock`.

## Pseudocode sketch

- Editor button click -> enqueue command
- Timer callback (30-60 Hz) -> dequeue and `controller.fireClip(...)`
- Optional callback from transport -> post async update to GUI model

## Host behavior caveat

Ableton Live, like other DAWs, isolates plugin scope. Deep DAW control requires an external bridge endpoint (Remote Script, AbletonOSC, or helper M4L device).
