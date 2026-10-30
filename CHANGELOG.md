# Changelog

Director is one repository and one version for four OBS plugins: Music Director, Palette
Director, Clip Director and Scene Director. Each plugin's own changelog lives in
`plugins/<name>/CHANGELOG.md`.

## 2.1.0

One zip (`director-2.1.0-windows-x64.zip`), every plugin at 2.1.0. Scene Director drives
Synesthesia over OSC, a user-editable event map decides what fires on which musical event, and
Palette Director gains a palette pool. Specification: `docs/spec/director-2.1.0.md`.

### Breaking

- **Scene Director drives Synesthesia over OSC** (Synesthesia Pro): it sends to Synesthesia's OSC
  input (default port 7000) and listens on Synesthesia's OSC output (default port 9001) for scene
  confirmations. MIDI out is removed; no virtual MIDI port is needed. Set up Synesthesia's OSC as in
  the README ("Synesthesia: OSC setup") or INSTALL.txt.
- **MIDI Director is removed** from Music Director, with `common/midi`. A controller is mapped in
  Synesthesia's own MIDI panel; Drop now (`md_drop_now`) and every other Director hotkey stay OBS
  hotkeys for a keyboard or a Stream Deck. Saved MIDI Director mappings are kept in the scene
  collection, inactive; Music Director logs them once with a rebind hint per mapping and writes a
  JSON copy to `plugin_config/music-director/`. Tag `2.0.0` is the archive of MIDI Director
  (`docs/archive/midi-director.md`).
- Rollback: the 2.0.0 zip. 2.1 keeps the 2.0 settings in the scene collection, so 2.0.0 picks
  them up again.

### Added

- **Event map** (`plugin_config/scene-director/event-map.json`): musical events (breakdown, drop,
  groove, section, phrase, bar, manual Next / Prev, scene loaded) -> OSC actions, with per-mapping
  enable, quantize, offset, Scene lead, cooldown, forced bypass and debounce, typed arguments and
  placeholders. The defaults reproduce 2.0 (Next on breakdown and drop on `section_beat`, manual
  Next / Prev) and send only `/playlist/next` and `/playlist/previous`. A bad file never crashes:
  a syntax error keeps the last good map, a bad entry is skipped and logged.
- **Scene confirmations**: Scene Director decodes Synesthesia's OSC feedback (bundles included),
  confirms each scene change, measures latency, and counts misses, mismatches and external
  changes. Confirmations are best-effort; nothing is resent or held back.
- **Scene Director dock** for OSC: link chip, host and ports with a guard against a feedback port
  equal to the send port, traffic and confirmation counters, breakdown / drop toggles, cooldown and
  Scene lead, the event map path with Reload and Open folder, and the log.
- **Palette pool** (Palette Director): each palette can be taken out of the automatic pool from
  the dock (right-click a pad, or the editor checkbox), live. Automatic changes skip it; a pad
  press still plays it. Saved per set.
- **`phrase_beat`** in `director_clock` (Music Director): the beat the current phrase started on,
  so followers place bar lines on the phrase grid. `api_version` stays 1.
- `common/osc`: the shared OSC codec (messages and bundles) and UDP transport, with tests.
- Cross-plugin tests: one send per section edge across several Scene Director sources, the default
  map sends only Next / Prev, every plugin lands on the same `section_beat`, and the Palette
  Director 1.2.0 equivalence golden is unchanged. The collision audit covers the event map, the log
  prefixes and the new clock field.

### Versions

| Plugin | 2.1.0 |
|---|---|
| Music Director | `phrase_beat`; MIDI Director removed |
| Palette Director | Palette pool |
| Clip Director | Version only |
| Scene Director | OSC, event map, confirmations, new dock; MIDI out removed |

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
