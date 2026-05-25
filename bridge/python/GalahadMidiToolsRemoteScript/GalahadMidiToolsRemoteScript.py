try:
    from _Framework.ControlSurface import ControlSurface
except ImportError:
    from ableton.v2.control_surface import ControlSurface


PROTOCOL_VERSION = 0x01

CLIP_BASE_NOTE = 36
SESSION_TRACKS = 8
SESSION_SCENES = 8

MIXER_VOLUME_BASE_CC = 20
MIXER_PAN_BASE_CC = 28
MIXER_SEND_A_BASE_CC = 36
MIXER_SEND_B_BASE_CC = 44
MIXER_SEND_C_BASE_CC = 52
MIXER_MUTE_BASE_CC = 68
MIXER_SOLO_BASE_CC = 76
MIXER_ARM_BASE_CC = 84
MIXER_SELECT_BASE_CC = 92
DEVICE_PARAM_BASE_CC = 100
DEVICE_BANK_LEFT_CC = 108
DEVICE_BANK_RIGHT_CC = 109
TRANSPORT_CC = 110
TRACK_BANK_CC = 111
SCENE_BANK_CC = 112
RECORD_FEEDBACK_CC = 113
METRONOME_FEEDBACK_CC = 114
SCENE_LAUNCH_CC = 115
DEVICE_TOGGLE_CC = 116
PLAY_FEEDBACK_CC = 117
TRACK_STOP_CC = 118
CLIP_SECTION_CC = 119
CLIP_SECTIONS = 4

SYSEX_HEADER = (0xF0, 0x7D, 0x47, 0x48)
SYSEX_REFRESH_LEGACY = 0x01
SYSEX_REFRESH_ALL = 0x01
SYSEX_REFRESH_SESSION = 0x02
SYSEX_REFRESH_TRANSPORT = 0x03
SYSEX_REFRESH_MIXER = 0x04
SYSEX_REFRESH_DEVICE = 0x05
SYSEX_SET_SESSION_OFFSET = 0x11
SYSEX_CLIP_STOP = 0x12
SYSEX_SCENE_LAUNCH = 0x14
SYSEX_SELECT_TRACK = 0x20
SYSEX_SET_DEVICE_BANK = 0x30
SYSEX_MIXER_SET_VALUE = 0x40
SYSEX_MIXER_ACTION = 0x41

MIXER_PARAM_VOLUME = 0
MIXER_PARAM_PAN = 1
MIXER_PARAM_SEND_A = 2
MIXER_PARAM_SEND_B = 3
MIXER_PARAM_SEND_C = 4

MIXER_ACTION_MUTE = 0
MIXER_ACTION_SOLO = 1
MIXER_ACTION_ARM = 2
MIXER_ACTION_SELECT = 3
MIXER_ACTION_STOP_TRACK = 4


def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def midi_to_unit(value):
    return clamp(float(value) / 127.0, 0.0, 1.0)


def unit_to_midi(value):
    return int(round(clamp(value, 0.0, 1.0) * 127.0))


class GalahadMidiToolsRemoteScript(ControlSurface):
    """Ableton Live bridge for Galahad session, mixer, and device control."""

    def __init__(self, c_instance):
        super().__init__(c_instance)
        self._track_offset = 0
        self._scene_offset = 0
        self._device_bank_offset = 0
        self._last_feedback = {}
        self._listeners = []
        self._last_launched_cell = None
        self._clip_section_ranges = {}

        with self.component_guard():
            self.log_message("GalahadMidiToolsRemoteScript protocol v1 loaded")
            self._install_song_listeners()
            self._refresh_all(force=True)

    def disconnect(self):
        self._remove_all_listeners()
        self.log_message("GalahadMidiToolsRemoteScript disconnected")
        super().disconnect()

    def receive_midi(self, midi_bytes):
        if not midi_bytes:
            return

        midi_bytes = tuple(midi_bytes)
        if self._is_galahad_sysex(midi_bytes):
            self._handle_sysex(midi_bytes)
            return

        status = midi_bytes[0] & 0xF0
        channel = midi_bytes[0] & 0x0F
        if status == 0x90 and len(midi_bytes) >= 3 and midi_bytes[2] > 0:
            self._handle_note_on(channel, midi_bytes[1], midi_bytes[2])
        elif status == 0xB0 and len(midi_bytes) >= 3:
            self._handle_cc(channel, midi_bytes[1], midi_bytes[2])

    def _handle_note_on(self, channel, note, velocity):
        del channel, velocity
        cell_index = note - CLIP_BASE_NOTE
        if cell_index < 0 or cell_index >= SESSION_TRACKS * SESSION_SCENES:
            return

        track = cell_index % SESSION_TRACKS
        scene = cell_index // SESSION_TRACKS
        slot = self._clip_slot_at(track, scene)
        if slot is None:
            return

        slot.fire()
        self._last_launched_cell = (track, scene)
        self._refresh_clip_feedback(track, scene)

    def _handle_cc(self, channel, cc, value):
        del channel

        if cc == TRANSPORT_CC:
            self._handle_transport(value)
            return
        if cc == RECORD_FEEDBACK_CC:
            self._set_record_mode(value >= 64)
            return
        if cc == TRACK_BANK_CC:
            self._move_track_bank(-SESSION_TRACKS if value < 64 else SESSION_TRACKS)
            return
        if cc == SCENE_BANK_CC:
            self._move_scene_bank(-SESSION_SCENES if value < 64 else SESSION_SCENES)
            return
        if cc == SCENE_LAUNCH_CC and value < SESSION_SCENES:
            self._launch_scene(value)
            return
        if cc == TRACK_STOP_CC and value < SESSION_TRACKS:
            self._stop_track_clips(value)
            return
        if cc == CLIP_SECTION_CC:
            self._set_clip_section(value)
            return
        if cc == DEVICE_BANK_LEFT_CC and value > 0:
            self._move_device_bank(-1)
            return
        if cc == DEVICE_BANK_RIGHT_CC and value > 0:
            self._move_device_bank(1)
            return
        if cc == DEVICE_TOGGLE_CC and value > 0:
            self._toggle_selected_device()
            return

        if MIXER_VOLUME_BASE_CC <= cc < MIXER_VOLUME_BASE_CC + SESSION_TRACKS:
            self._set_track_parameter(cc - MIXER_VOLUME_BASE_CC, "volume", value)
            return
        if MIXER_PAN_BASE_CC <= cc < MIXER_PAN_BASE_CC + SESSION_TRACKS:
            self._set_track_parameter(cc - MIXER_PAN_BASE_CC, "panning", value)
            return
        if MIXER_SEND_A_BASE_CC <= cc < MIXER_SEND_A_BASE_CC + SESSION_TRACKS:
            self._set_track_send(cc - MIXER_SEND_A_BASE_CC, 0, value)
            return
        if MIXER_SEND_B_BASE_CC <= cc < MIXER_SEND_B_BASE_CC + SESSION_TRACKS:
            self._set_track_send(cc - MIXER_SEND_B_BASE_CC, 1, value)
            return
        if MIXER_SEND_C_BASE_CC <= cc < MIXER_SEND_C_BASE_CC + SESSION_TRACKS:
            self._set_track_send(cc - MIXER_SEND_C_BASE_CC, 2, value)
            return
        if MIXER_MUTE_BASE_CC <= cc < MIXER_MUTE_BASE_CC + SESSION_TRACKS and value > 0:
            self._toggle_track_bool(cc - MIXER_MUTE_BASE_CC, "mute")
            return
        if MIXER_SOLO_BASE_CC <= cc < MIXER_SOLO_BASE_CC + SESSION_TRACKS and value > 0:
            self._toggle_track_bool(cc - MIXER_SOLO_BASE_CC, "solo")
            return
        if MIXER_ARM_BASE_CC <= cc < MIXER_ARM_BASE_CC + SESSION_TRACKS and value > 0:
            self._toggle_arm(cc - MIXER_ARM_BASE_CC)
            return
        if MIXER_SELECT_BASE_CC <= cc < MIXER_SELECT_BASE_CC + SESSION_TRACKS and value > 0:
            self._select_track(cc - MIXER_SELECT_BASE_CC)
            return
        if DEVICE_PARAM_BASE_CC <= cc < DEVICE_PARAM_BASE_CC + 8:
            self._set_device_parameter(cc - DEVICE_PARAM_BASE_CC, value)

    def _handle_transport(self, value):
        song = self.song()
        if value == 0:
            song.stop_playing()
        elif value == 1:
            song.start_playing()
        elif value == 2:
            song.record_mode = not song.record_mode
        elif value == 3 and hasattr(song, "continue_playing"):
            song.continue_playing()
        elif value == 4 and hasattr(song, "tap_tempo"):
            song.tap_tempo()
        elif value == 5:
            song.metronome = not song.metronome
        self._refresh_transport_feedback()

    def _set_record_mode(self, enabled):
        song = self.song()
        if song.record_mode != enabled:
            song.record_mode = enabled
        self._refresh_transport_feedback()

    def _set_clip_section(self, value):
        section = clamp(int(value), 0, CLIP_SECTIONS - 1)
        clip = self._automation_section_clip()
        if clip is None:
            return

        bounds = self._clip_section_bounds(clip)
        if bounds is None:
            return

        start, end = bounds
        length = end - start
        if length <= 0.0:
            return

        section_start = start + (length * float(section) / float(CLIP_SECTIONS))
        self._set_clip_start(clip, section_start)

    def _automation_section_clip(self):
        if self._last_launched_cell is not None:
            slot = self._clip_slot_at(self._last_launched_cell[0], self._last_launched_cell[1])
            clip = self._clip_from_slot(slot)
            if clip is not None:
                return clip

        slot = self._safe_get(self.song().view, "highlighted_clip_slot", None)
        clip = self._clip_from_slot(slot)
        if clip is not None:
            return clip

        return self._safe_get(self.song().view, "detail_clip", None)

    def _clip_from_slot(self, slot):
        if slot is None or not self._safe_get(slot, "has_clip", False):
            return None
        return self._safe_get(slot, "clip", None)

    def _clip_section_bounds(self, clip):
        key = id(clip)
        if key in self._clip_section_ranges:
            return self._clip_section_ranges[key]

        start = self._safe_get(clip, "start_marker", None)
        if start is None:
            start = self._safe_get(clip, "loop_start", 0.0)

        end = self._safe_get(clip, "end_marker", None)
        if end is None:
            end = self._safe_get(clip, "loop_end", 0.0)

        start = float(start)
        end = float(end)
        if end <= start:
            return None

        bounds = (start, end)
        self._clip_section_ranges[key] = bounds
        return bounds

    def _set_clip_start(self, clip, section_start):
        try:
            clip.start_marker = section_start
        except Exception:
            pass

        scrub = self._safe_get(clip, "scrub", None)
        if self._safe_get(clip, "is_playing", False) and callable(scrub):
            try:
                scrub(section_start)
                stop_scrub = self._safe_get(clip, "stop_scrub", None)
                if callable(stop_scrub):
                    stop_scrub()
            except Exception:
                pass

    def _safe_get(self, target, name, default=None):
        if target is None:
            return default
        try:
            return getattr(target, name, default)
        except Exception:
            return default

    def _track_at(self, bank_track):
        track_index = self._track_offset + bank_track
        tracks = self.song().tracks
        if 0 <= track_index < len(tracks):
            return tracks[track_index]
        return None

    def _clip_slot_at(self, bank_track, bank_scene):
        track = self._track_at(bank_track)
        scene_index = self._scene_offset + bank_scene
        if track is None or scene_index >= len(self.song().scenes):
            return None
        return track.clip_slots[scene_index]

    def _selected_device(self):
        track = self.song().view.selected_track
        view = getattr(track, "view", None)
        if view is None:
            return None
        return getattr(view, "selected_device", None)

    def _parameter_to_midi(self, parameter):
        minimum = getattr(parameter, "min", 0.0)
        maximum = getattr(parameter, "max", 1.0)
        if maximum <= minimum:
            return 0
        return unit_to_midi((parameter.value - minimum) / (maximum - minimum))

    def _set_parameter_from_midi(self, parameter, value):
        minimum = getattr(parameter, "min", 0.0)
        maximum = getattr(parameter, "max", 1.0)
        parameter.value = minimum + (maximum - minimum) * midi_to_unit(value)

    def _set_track_parameter(self, bank_track, parameter_name, value):
        track = self._track_at(bank_track)
        if track is None:
            return

        parameter = getattr(track.mixer_device, parameter_name, None)
        if parameter is None:
            return

        self._set_parameter_from_midi(parameter, value)
        self._refresh_mixer_feedback(bank_track)

    def _set_track_send(self, bank_track, send_index, value):
        track = self._track_at(bank_track)
        if track is None:
            return

        sends = track.mixer_device.sends
        if send_index >= len(sends):
            return

        self._set_parameter_from_midi(sends[send_index], value)
        self._refresh_mixer_feedback(bank_track)

    def _handle_mixer_value(self, bank_track, parameter, value):
        if bank_track < 0 or bank_track >= SESSION_TRACKS:
            return

        if parameter == MIXER_PARAM_VOLUME:
            self._set_track_parameter(bank_track, "volume", value)
        elif parameter == MIXER_PARAM_PAN:
            self._set_track_parameter(bank_track, "panning", value)
        elif parameter == MIXER_PARAM_SEND_A:
            self._set_track_send(bank_track, 0, value)
        elif parameter == MIXER_PARAM_SEND_B:
            self._set_track_send(bank_track, 1, value)
        elif parameter == MIXER_PARAM_SEND_C:
            self._set_track_send(bank_track, 2, value)

    def _handle_mixer_action(self, bank_track, action):
        if bank_track < 0 or bank_track >= SESSION_TRACKS:
            return

        if action == MIXER_ACTION_MUTE:
            self._toggle_track_bool(bank_track, "mute")
        elif action == MIXER_ACTION_SOLO:
            self._toggle_track_bool(bank_track, "solo")
        elif action == MIXER_ACTION_ARM:
            self._toggle_arm(bank_track)
        elif action == MIXER_ACTION_SELECT:
            self._select_track(bank_track)
        elif action == MIXER_ACTION_STOP_TRACK:
            self._stop_track_clips(bank_track)

    def _toggle_track_bool(self, bank_track, property_name):
        track = self._track_at(bank_track)
        if track is None:
            return

        setattr(track, property_name, not getattr(track, property_name))
        self._refresh_mixer_feedback(bank_track)

    def _toggle_arm(self, bank_track):
        track = self._track_at(bank_track)
        if track is None or not getattr(track, "can_be_armed", False):
            return

        track.arm = not track.arm
        self._refresh_mixer_feedback(bank_track)

    def _select_track(self, bank_track):
        track = self._track_at(bank_track)
        if track is None:
            return

        self.song().view.selected_track = track
        self._device_bank_offset = 0
        self._refresh_mixer_feedback(force=True)
        self._refresh_device_feedback(force=True)

    def _set_device_parameter(self, index_in_bank, value):
        device = self._selected_device()
        if device is None:
            return

        parameter_index = self._device_bank_offset + index_in_bank
        parameters = list(device.parameters)
        if parameter_index >= len(parameters):
            return

        parameter = parameters[parameter_index]
        if getattr(parameter, "is_enabled", True):
            self._set_parameter_from_midi(parameter, value)
            self._refresh_device_feedback()

    def _move_device_bank(self, direction):
        device = self._selected_device()
        if device is None:
            self._device_bank_offset = 0
            self._refresh_device_feedback(force=True)
            return

        max_offset = max(0, len(device.parameters) - 8)
        self._device_bank_offset = clamp(self._device_bank_offset + direction * 8, 0, max_offset)
        self._refresh_device_feedback(force=True)

    def _toggle_selected_device(self):
        device = self._selected_device()
        if device is None or len(device.parameters) == 0:
            return

        activator = device.parameters[0]
        if getattr(activator, "is_enabled", True):
            minimum = getattr(activator, "min", 0.0)
            maximum = getattr(activator, "max", 1.0)
            midpoint = minimum + (maximum - minimum) * 0.5
            activator.value = minimum if activator.value > midpoint else maximum
            self._refresh_device_feedback(force=True)

    def _launch_scene(self, bank_scene):
        scene_index = self._scene_offset + bank_scene
        scenes = self.song().scenes
        if 0 <= scene_index < len(scenes):
            scenes[scene_index].fire()
            self._refresh_session_feedback(force=True)

    def _stop_track_clips(self, bank_track):
        track = self._track_at(bank_track)
        if track is None:
            return

        if hasattr(track, "stop_all_clips"):
            track.stop_all_clips()
        self._refresh_session_feedback(force=True)

    def _stop_clip(self, bank_track, bank_scene):
        slot = self._clip_slot_at(bank_track, bank_scene)
        if slot is not None and hasattr(slot, "stop"):
            slot.stop()
            self._refresh_clip_feedback(bank_track, bank_scene)

    def _move_track_bank(self, delta):
        max_offset = max(0, len(self.song().tracks) - SESSION_TRACKS)
        self._track_offset = clamp(self._track_offset + delta, 0, max_offset)
        self._refresh_session_feedback(force=True)
        self._refresh_mixer_feedback(force=True)

    def _move_scene_bank(self, delta):
        max_offset = max(0, len(self.song().scenes) - SESSION_SCENES)
        self._scene_offset = clamp(self._scene_offset + delta, 0, max_offset)
        self._refresh_session_feedback(force=True)

    def _set_session_offset(self, track_bank, scene_bank):
        self._track_offset = clamp(track_bank * SESSION_TRACKS, 0, max(0, len(self.song().tracks) - SESSION_TRACKS))
        self._scene_offset = clamp(scene_bank * SESSION_SCENES, 0, max(0, len(self.song().scenes) - SESSION_SCENES))
        self._refresh_session_feedback(force=True)
        self._refresh_mixer_feedback(force=True)

    def _set_device_bank(self, bank):
        device = self._selected_device()
        if device is None:
            self._device_bank_offset = 0
        else:
            max_offset = max(0, len(device.parameters) - 8)
            self._device_bank_offset = clamp(bank * 8, 0, max_offset)
        self._refresh_device_feedback(force=True)

    def _refresh_all(self, force=False):
        self._refresh_session_feedback(force=force)
        self._refresh_transport_feedback()
        self._refresh_mixer_feedback(force=force)
        self._refresh_device_feedback(force=force)

    def _refresh_session_feedback(self, force=False):
        if force:
            self._last_feedback = {}

        for scene in range(SESSION_SCENES):
            for track in range(SESSION_TRACKS):
                self._refresh_clip_feedback(track, scene)

    def _refresh_clip_feedback(self, track, scene):
        slot = self._clip_slot_at(track, scene)
        value = 12
        if slot is not None:
            if getattr(slot, "is_recording", False):
                value = 110
            elif getattr(slot, "is_triggered", False):
                value = 100
            elif getattr(slot, "is_playing", False):
                value = 127
            elif getattr(slot, "has_clip", False):
                value = 80
            elif getattr(slot, "has_stop_button", False):
                value = 40

        note = CLIP_BASE_NOTE + scene * SESSION_TRACKS + track
        self._send_cached(("clip", track, scene), (0x90, note, value))

    def _refresh_transport_feedback(self):
        song = self.song()
        self._send_cached(("transport", "play"), (0xB0, PLAY_FEEDBACK_CC, 127 if song.is_playing else 0))
        self._send_cached(("transport", "record"), (0xB0, RECORD_FEEDBACK_CC, 127 if song.record_mode else 0))
        self._send_cached(("transport", "metronome"), (0xB0, METRONOME_FEEDBACK_CC, 127 if song.metronome else 0))

    def _refresh_mixer_feedback(self, bank_track=None, force=False):
        tracks = range(SESSION_TRACKS) if bank_track is None or force else [bank_track]
        selected_track = self.song().view.selected_track

        for index in tracks:
            track = self._track_at(index)
            if track is None:
                self._send_empty_mixer_feedback(index)
                continue

            volume = self._parameter_to_midi(track.mixer_device.volume)
            pan = self._parameter_to_midi(track.mixer_device.panning)
            self._send_cached(("mix", index, "volume"), (0xB0, MIXER_VOLUME_BASE_CC + index, volume))
            self._send_cached(("mix", index, "pan"), (0xB0, MIXER_PAN_BASE_CC + index, pan))

            sends = track.mixer_device.sends
            send_a = self._parameter_to_midi(sends[0]) if len(sends) > 0 else 0
            send_b = self._parameter_to_midi(sends[1]) if len(sends) > 1 else 0
            send_c = self._parameter_to_midi(sends[2]) if len(sends) > 2 else 0
            self._send_cached(("mix", index, "send_a"), (0xB0, MIXER_SEND_A_BASE_CC + index, send_a))
            self._send_cached(("mix", index, "send_b"), (0xB0, MIXER_SEND_B_BASE_CC + index, send_b))
            self._send_cached(("mix", index, "send_c"), (0xB0, MIXER_SEND_C_BASE_CC + index, send_c))

            self._send_cached(("mix", index, "mute"), (0xB0, MIXER_MUTE_BASE_CC + index, 127 if track.mute else 0))
            self._send_cached(("mix", index, "solo"), (0xB0, MIXER_SOLO_BASE_CC + index, 127 if track.solo else 0))
            self._send_cached(("mix", index, "arm"), (0xB0, MIXER_ARM_BASE_CC + index, 127 if getattr(track, "arm", False) else 0))
            self._send_cached(("mix", index, "select"), (0xB0, MIXER_SELECT_BASE_CC + index, 127 if track == selected_track else 0))

    def _send_empty_mixer_feedback(self, index):
        feedback = (
            (MIXER_VOLUME_BASE_CC, "volume"),
            (MIXER_PAN_BASE_CC, "pan"),
            (MIXER_SEND_A_BASE_CC, "send_a"),
            (MIXER_SEND_B_BASE_CC, "send_b"),
            (MIXER_SEND_C_BASE_CC, "send_c"),
            (MIXER_MUTE_BASE_CC, "mute"),
            (MIXER_SOLO_BASE_CC, "solo"),
            (MIXER_ARM_BASE_CC, "arm"),
            (MIXER_SELECT_BASE_CC, "select"),
        )
        for base_cc, name in feedback:
            self._send_cached(("mix", index, name), (0xB0, base_cc + index, 0))

    def _refresh_device_feedback(self, force=False):
        if force:
            for index in range(8):
                self._last_feedback.pop(("device", index), None)

        device = self._selected_device()
        parameters = list(device.parameters) if device is not None else []

        for index in range(8):
            parameter_index = self._device_bank_offset + index
            value = 0
            if parameter_index < len(parameters):
                value = self._parameter_to_midi(parameters[parameter_index])
            self._send_cached(("device", index), (0xB0, DEVICE_PARAM_BASE_CC + index, value))

    def _is_galahad_sysex(self, midi_bytes):
        if len(midi_bytes) == 6:
            return tuple(midi_bytes[:4]) == SYSEX_HEADER and midi_bytes[-1] == 0xF7

        return (
            len(midi_bytes) >= 7
            and tuple(midi_bytes[:4]) == SYSEX_HEADER
            and midi_bytes[4] == PROTOCOL_VERSION
            and midi_bytes[-1] == 0xF7
        )

    def _handle_sysex(self, midi_bytes):
        if len(midi_bytes) == 6:
            if midi_bytes[4] == SYSEX_REFRESH_LEGACY:
                self._refresh_all(force=True)
            return

        command = midi_bytes[5]
        payload = midi_bytes[6:-1]

        if command == SYSEX_REFRESH_ALL:
            self._refresh_all(force=True)
        elif command == SYSEX_REFRESH_SESSION:
            self._refresh_session_feedback(force=True)
        elif command == SYSEX_REFRESH_TRANSPORT:
            self._refresh_transport_feedback()
        elif command == SYSEX_REFRESH_MIXER:
            self._refresh_mixer_feedback(force=True)
        elif command == SYSEX_REFRESH_DEVICE:
            self._refresh_device_feedback(force=True)
        elif command == SYSEX_SET_SESSION_OFFSET and len(payload) >= 2:
            self._set_session_offset(payload[0], payload[1])
        elif command == SYSEX_CLIP_STOP and len(payload) >= 2:
            self._stop_clip(payload[0], payload[1])
        elif command == SYSEX_SCENE_LAUNCH and len(payload) >= 1:
            self._launch_scene(payload[0])
        elif command == SYSEX_SELECT_TRACK and len(payload) >= 1:
            self._select_track(payload[0])
        elif command == SYSEX_SET_DEVICE_BANK and len(payload) >= 1:
            self._set_device_bank(payload[0])
        elif command == SYSEX_MIXER_SET_VALUE and len(payload) >= 3:
            self._handle_mixer_value(payload[0], payload[1], payload[2])
        elif command == SYSEX_MIXER_ACTION and len(payload) >= 2:
            self._handle_mixer_action(payload[0], payload[1])

    def _send_cached(self, key, message):
        if self._last_feedback.get(key) == message:
            return
        self._last_feedback[key] = message
        self._send_midi(message)

    def _install_song_listeners(self):
        song = self.song()
        self._add_listener(song, "is_playing", self._refresh_transport_feedback)
        self._add_listener(song, "record_mode", self._refresh_transport_feedback)
        self._add_listener(song, "metronome", self._refresh_transport_feedback)
        self._add_listener(song, "tracks", self._on_song_structure_changed)
        self._add_listener(song, "scenes", self._on_song_structure_changed)
        self._add_listener(song.view, "selected_track", self._on_selected_track_changed)

    def _on_song_structure_changed(self):
        self._set_session_offset(self._track_offset // SESSION_TRACKS, self._scene_offset // SESSION_SCENES)

    def _on_selected_track_changed(self):
        self._device_bank_offset = 0
        self._refresh_mixer_feedback(force=True)
        self._refresh_device_feedback(force=True)

    def _add_listener(self, owner, name, callback):
        add = getattr(owner, "add_{}_listener".format(name), None)
        has = getattr(owner, "{}_has_listener".format(name), None)
        if add is None:
            return
        if has is not None and has(callback):
            return
        add(callback)
        self._listeners.append((owner, name, callback))

    def _remove_all_listeners(self):
        for owner, name, callback in self._listeners:
            remove = getattr(owner, "remove_{}_listener".format(name), None)
            has = getattr(owner, "{}_has_listener".format(name), None)
            if remove is not None and (has is None or has(callback)):
                remove(callback)
        self._listeners = []
