# Changelog

Director is the suite release: it bundles Palette Director and Clip Director, which
keep their own versions and their own changelogs.

## 2.0.0 - the Director suite

Suite = Music Director + Palette Director + Clip Director + Scene Director, one version (2.0.0).

### Breaking

- Requires **Music Director**. Music Director is the ONE clock of the suite: Palette Director
  no longer captures or analyses audio and no longer registers `palette_director_clock` /
  `palette_director_instances`. Without Music Director the Palette Lock filters hold, the dock
  reads "Waiting for Music Director", and the palette keeps rendering.
- The Music dock (audio source, input gain, kick sensitivity, fallback BPM, phrase length, the
  level/bass meter, beat and phrase lights, Tap tempo / Lock BPM / Nudge back / Nudge forward /
  Phrase now / Drop now / Force breakdown) and its hotkeys moved to Music Director.
- Palette Director follows the clock (`director_clock`) through `director_follow::Follower`,
  driving the palette scheduler in 1.2.0's order (section, phrase, beat, tick).

### Unchanged (presets EXACT)

- The filter id `palette_lock_filter`, every palette setting key, the palette hotkeys and the
  set-file schema (2) are unchanged.
- Presets load EXACT. The retired audio keys (`audio_source`, `input_gain_db`,
  `kick_sensitivity`, `fallback_bpm`, `phrase_beats`) are preserved verbatim (never removed,
  never rewritten). Set files round-trip byte-identical; spectrum palettes (16 palettes of
  5-8 colours) load and render unchanged.

### Added

- Render guards (`pl_sanitize_for_render`, spec 4.2): an empty set, out-of-range live/prev,
  an out-of-range colour count and an unknown mode are normalised on load and render, with a
  once-per-filter-per-cause warning. The filter never draws nothing.
- `palette_director_drop_cycle(out bool active, out int steps_per_beat)`: true while any
  Palette Lock runs a drop colour cycle (Clip Director's stutter follows it).

## 1.0.0 - first release

Suite = Palette Director 1.2.0 + Clip Director 1.1.0.

| Component | Version | Changelog |
|---|---|---|
| Palette Director | 1.2.0 | https://github.com/EnZhm/palette-director/blob/master/CHANGELOG.md |
| Clip Director | 1.1.0 | https://github.com/EnZhm/clip-director/blob/main/CHANGELOG.md |

What it adds:

- One Windows x64 zip with both plugins, `director-1.0.0-windows-x64.zip`; both folders
  are copied into `C:\ProgramData\obs-studio\plugins\`.
- Palette Director: filter "Palette Lock", dock "Palette Director", dock "Music"
  (BPM, beat lights, section, level and bass meter, 64-beat graph, audio controls,
  Phrase is now / Drop now / Force breakdown / Tap tempo / Lock BPM / Nudge), and the
  read-only beat clock for other plugins.
- Clip Director: source "Clip Director", dock "Clip Director" with the new Synesthesia
  tab (OSC scene control), and the new dock "MIDI Director" (MIDI learn to OBS hotkeys).
- Windows-only packaging for both plugins.
