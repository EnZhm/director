# Music Director changelog

## 2.1.0

Part of Director 2.1.0.

- **MIDI Director removed.** The MIDI Director dock (`midi-director-dock`), firing arbitrary OBS
  hotkeys by identity, `common/midi` (MIDI in and out) and the `winmm` link are gone. Music Director
  has no MIDI in. The archive is tag `2.0.0` (`docs/archive/midi-director.md`).
- **Hotkeys kept.** Every `md_*` hotkey stays a registered OBS frontend hotkey with its saved
  binding under `music_director`. `md_drop_now` ("Music Director: Drop now") binds to a key in
  Settings > Hotkeys or to a Stream Deck button with Stream Deck's OBS hotkey action.
- **Saved mappings kept.** The scene collection's `midi_director` value is read on load and written
  back verbatim on save, so a 2.1 save never drops it and a 2.0.0 rollback still finds it. When it
  holds mappings, Music Director logs once per collection:
  `[music-director] MIDI Director was removed in 2.1: <N> mappings kept in the scene collection (inactive), copy at <path>`,
  writes the JSON once to `plugin_config/music-director/midi-director-<collection>.json`, and logs
  one rebind hint per mapping:
  `[music-director] MIDI Director mapping '<control>' -> <target> (<label>) is inactive: <hint>`.
- **Rebind** each control as its hint says:

  | 2.0 mapping target | Hint | In 2.1 |
  |---|---|---|
  | `scene_director_next` / `scene_director_prev` | map the control in Synesthesia's MIDI panel | Synesthesia's MIDI panel: devices tab "refresh connected devices", then map the playlist **next** / **previous** in the scenes tab. Scene Director logs the change as `external` |
  | `md_drop_now` | bind Drop now in Settings > Hotkeys or on a Stream Deck | A key in OBS Settings > Hotkeys, or a Stream Deck button (OBS hotkey action) |
  | any other hotkey | bind `<label>` in Settings > Hotkeys | A key, or a Stream Deck button |

- OBS's saved dock layout may still name `midi-director-dock`; Qt ignores unknown docks.
- **`phrase_beat`** in `director_clock`: the beat the current phrase started on (bar lines are
  `phrase_beat + 4k`). `api_version` stays 1; a follower reading an older Music Director gets 0 and
  places bars on `beat % 4 == 0`. Tested across a phrase edge, Phrase now and a 16/32 phrase-length
  change.

## 2.0.0

Music Director is the new home of the Director suite's music clock, split out of Palette Director
1.2.0. It owns the audio analysis and THE clock; Palette Director, Clip Director and Scene Director
follow it.

- **The clock** (`void director_clock(...)`, `include/director-clock.h`, api_version 1): one global
  singleton. The audio analysis, beat tracker, section detector, command queue and clock
  extrapolation move from Palette Director 1.2.0 unchanged, so the same audio gives the same beat
  grid and section decisions. New fields for followers: `section_seq` (+1 on every section change,
  natural or forced), `section_beat` (the beat the section starts on), `epoch` (+1 whenever the grid
  is re-anchored) and `bass` (0..1). The worker publishes the snapshot about every 5 ms; the proc
  takes one short mutex and never blocks on audio. Before registering it calls the proc: if it
  already answers, this copy logs an error and registers nothing.
- **Music dock** (`music-director-dock`): the Palette Director 1.2.0 Music dock, pointed at the
  global clock and extended with the audio source picker, input gain, kick sensitivity, fallback BPM
  and phrase length. It shows the audio meter, BPM/confidence, beat/phrase lights, the section, and
  Tap tempo / Lock BPM / Nudge back / Nudge forward / Phrase is now / Drop now / Force breakdown.
- **Clock hotkeys**: `md_phrase_now`, `md_drop_now`, `md_force_breakdown`, `md_tap_tempo`,
  `md_lock_bpm`, `md_nudge_back`, `md_nudge_fwd`.
- **Settings** in the scene collection under `music_director` (audio source, input gain, kick
  sensitivity, fallback BPM, phrase length, enabled, migrated_from, plus the hotkey bindings).
- **Migration** on first load (spec 3.1): when the collection has no `music_director` key, copy the
  audio settings and the bound `pd_*` clock hotkeys from a Palette Lock filter (the first ENABLED one
  with a user audio source, else the first Palette Lock) and record `migrated_from`. The filter is
  never written. Other filters with different audio values are named in a warning.
- **MIDI Director dock** (`midi-director-dock`), moved from Scene Director 2.0.0: MIDI in over
  `common/midi`, MIDI learn, maps notes and CCs to any OBS hotkey (music, palette, clips,
  Synesthesia). Mappings keep the `midi_director` storage key, so existing mappings load unchanged.
  Music Director is the only plugin with MIDI in.
- Section changes log `[music-director] section -> <NAME> seq=<S> beat=<B> (<natural|forced>)`.
- Tests: the clock-side suite from Palette Director 1.2.0 (audio level, BPM, sections, silence,
  clock API, music commands, RNG, determinism) plus new tests for `section_seq`/`section_beat`/
  `epoch` and for the migration picker.
