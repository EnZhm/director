# Changelog

Director is one repository and one version for four OBS plugins: Music Director, Palette
Director, Clip Director and Scene Director. Each plugin's own changelog lives in
`plugins/<name>/CHANGELOG.md`.

## 2.0.0

One repository, one version, one zip (`director-2.0.0-windows-x64.zip`). The music drives
the palette, the clips and the Synesthesia playlist through one clock.

### Breaking

- **Music Director** is new and required. It owns the audio analysis and the only clock
  (`director_clock`, `include/director-clock.h`): BPM, beats, phrases, breakdown and drop
  detection, Tap / Lock BPM / Nudge, Phrase now / Drop now / Force breakdown and the Music dock.
  MIDI Director moved into it.
- **Palette Director** follows the clock and does no audio analysis. Its audio settings move to
  Music Director automatically on first load; palettes, sets and filters load exactly as saved.
- **Clip Director** plays clips only (the Synesthesia OSC control and the MIDI dock are gone)
  and follows the clock.
- **Scene Director** sends Synesthesia playlist Next / Prev over MIDI out only, and follows the
  clock. Its `clip_director_source` alias is gone; Clip Director owns that id.
- Install: remove the 1.x plugin folders, then copy the four folders from the zip.

### Added

- One landing beat: on every breakdown and drop, the palette change, the clip cut and the
  Synesthesia Next land on Music Director's `section_beat`. Clip Director keeps breakdown and
  drop clips armed for this.
- Forced sections and manual Next / Prev bypass Scene Director's cooldown; automatic section
  changes keep it.
- Palette Director render guards: an empty or malformed palette set renders a fallback palette
  and warns once.
- CI builds all four plugins, runs every test, including the cross-plugin tests (exactly one
  reaction per clock edge, and equivalence with Palette Director 1.2.0's palette decisions),
  and runs a collision audit of OBS ids across the four plugins.

### Versions

| Plugin | 2.0.0 replaces |
|---|---|
| Music Director | new |
| Palette Director | Palette Director 1.2.0 (EnZhm/palette-director) |
| Clip Director | Clip Director 1.1.0 (EnZhm/clip-director) |
| Scene Director | Scene Director 2.0.0 (EnZhm/scene-director) |

## 1.1.0 (tag only, not released)

The tag `1.1.0` marks a playlist-only suite that was never released; 2.0.0 replaces it.

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
