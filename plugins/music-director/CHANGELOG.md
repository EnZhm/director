# Music Director changelog

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
