#!/usr/bin/env python3
"""
Galahad OSC bridge stub.

Standalone development bridge:
- receives Galahad OSC commands
- logs decoded address/arguments
- optionally forwards packets to another OSC endpoint
- emits simulated /galahad/event/... telemetry to a C++ client return port

The production Ableton Remote Script should use the same message vocabulary, but
drain inbound commands from Live's update cycle before touching the Live Object
Model.
"""

from __future__ import annotations

import argparse
import socket
import struct
from dataclasses import dataclass
from typing import Iterable, Optional, Sequence, Union

OscArg = Union[int, float, str]


@dataclass(frozen=True)
class OscMessage:
    address: str
    arguments: Sequence[OscArg]


def _pad_size(size: int) -> int:
    return (size + 3) & ~3


def _read_padded_string(packet: bytes, offset: int) -> tuple[str, int]:
    end = packet.index(b"\0", offset)
    value = packet[offset:end].decode("utf-8")
    return value, offset + _pad_size((end - offset) + 1)


def _append_padded_string(out: bytearray, value: str) -> None:
    raw = value.encode("utf-8") + b"\0"
    out.extend(raw)
    out.extend(b"\0" * (_pad_size(len(raw)) - len(raw)))


def decode_osc(packet: bytes) -> OscMessage:
    offset = 0
    address, offset = _read_padded_string(packet, offset)
    type_tags, offset = _read_padded_string(packet, offset)

    if not type_tags.startswith(","):
        raise ValueError("OSC type tag string is missing comma prefix")

    arguments: list[OscArg] = []
    for tag in type_tags[1:]:
        if tag == "i":
            arguments.append(struct.unpack_from(">i", packet, offset)[0])
            offset += 4
        elif tag == "f":
            arguments.append(struct.unpack_from(">f", packet, offset)[0])
            offset += 4
        elif tag == "s":
            value, offset = _read_padded_string(packet, offset)
            arguments.append(value)
        else:
            raise ValueError(f"Unsupported OSC type tag: {tag}")

    return OscMessage(address, tuple(arguments))


def encode_osc(address: str, arguments: Iterable[OscArg] = ()) -> bytes:
    args = tuple(arguments)
    out = bytearray()
    _append_padded_string(out, address)

    tags = ","
    for arg in args:
        if isinstance(arg, int):
            tags += "i"
        elif isinstance(arg, float):
            tags += "f"
        elif isinstance(arg, str):
            tags += "s"
        else:
            raise TypeError(f"Unsupported OSC argument type: {type(arg)!r}")

    _append_padded_string(out, tags)

    for arg in args:
        if isinstance(arg, int):
            out.extend(struct.pack(">i", arg))
        elif isinstance(arg, float):
            out.extend(struct.pack(">f", arg))
        elif isinstance(arg, str):
            _append_padded_string(out, arg)

    return bytes(out)


def simulated_events_for(message: OscMessage) -> list[OscMessage]:
    if message.address == "/live/clip/fire" and len(message.arguments) >= 2:
        return [OscMessage("/galahad/event/clip_fired", (int(message.arguments[0]), int(message.arguments[1])))]

    if message.address == "/live/track/set/volume" and len(message.arguments) >= 2:
        return [OscMessage("/galahad/event/track_volume", (int(message.arguments[0]), float(message.arguments[1])))]

    if message.address == "/live/song/start_playing":
        return [OscMessage("/galahad/event/song_started", ())]

    if message.address == "/live/song/stop_playing":
        return [OscMessage("/galahad/event/song_stopped", ())]

    if message.address == "/live/song/set/tempo" and message.arguments:
        return [OscMessage("/galahad/event/tempo", (float(message.arguments[0]),))]

    return []


def run_bridge(
    listen_host: str,
    listen_port: int,
    event_host: str,
    event_port: int,
    forward_host: Optional[str],
    forward_port: Optional[int],
) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((listen_host, listen_port))

    send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    forward_target = (forward_host, forward_port) if forward_host is not None and forward_port is not None else None
    event_target = (event_host, event_port)

    print(f"Galahad bridge listening on {listen_host}:{listen_port}")
    print(f"Galahad telemetry target is {event_host}:{event_port}")

    while True:
        packet, addr = sock.recvfrom(4096)

        try:
            message = decode_osc(packet)
            print(f"{addr} -> {message.address} {tuple(message.arguments)}")
        except Exception as exc:
            print(f"{addr} -> invalid OSC packet: {exc}")
            continue

        if forward_target is not None:
            send_sock.sendto(packet, forward_target)

        for event in simulated_events_for(message):
            send_sock.sendto(encode_osc(event.address, event.arguments), event_target)
            print(f"event -> {event.address} {tuple(event.arguments)}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Galahad OSC bridge stub")
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=11000)
    parser.add_argument("--event-host", default="127.0.0.1")
    parser.add_argument("--event-port", type=int, default=11001)
    parser.add_argument("--forward-host", default=None)
    parser.add_argument("--forward-port", type=int, default=None)

    args = parser.parse_args()
    run_bridge(
        args.listen_host,
        args.listen_port,
        args.event_host,
        args.event_port,
        args.forward_host,
        args.forward_port,
    )


if __name__ == "__main__":
    main()
