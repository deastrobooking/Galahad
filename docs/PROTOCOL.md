# Galahad Protocol v0

## Transport

Default transport: OSC over UDP localhost.

- Plugin/client send port: configurable
- Bridge/server receive port: configurable (default 11000)
- Return/event port: configurable (default 11001)

## Core command set

- `/live/song/start_playing`
- `/live/song/stop_playing`
- `/live/clip/fire <track:int> <clip:int>`
- `/live/track/set/volume <track:int> <value:float>`

## Versioning

In future versions, include:

- `/galahad/hello <version:string> <capabilities:string>`
- `/galahad/error <code:int> <message:string>`

## Notes

This protocol is intentionally small and should be expanded through stable, documented endpoint additions.
