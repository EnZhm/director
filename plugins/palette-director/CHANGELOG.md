# Changelog

Palette Director for OBS Studio 32.2.x, Windows x64. Part of the
[Director](https://github.com/EnZhm/director) suite.

This repository's first release is 1.2.0. The 1.0.0, 1.0.1 and 1.1.0 builds were made
before it and are listed here for completeness; their files are not republished.

## 2.1.0

Part of Director 2.1.0.

### Added: the palette pool

- Each palette of a Palette Lock's set is either in the **automatic pool** or out of it. Out-of-pool
  palettes are never chosen automatically; they stay in the set and a pad press still plays them.
- Dock: right-click a palette pad > **In automatic pool** toggles it; the palette editor shows the
  same checkbox. An out-of-pool pad is dimmed (about 35% opacity) with a small "off" corner mark;
  the live highlight still shows on it when it is live.
- Automatic selection draws only from in-pool palettes: phrase rotation, the drop pick,
  drop-after-breakdown, the drop colour cycle's target, and Next / Prev palette (hotkeys and dock
  buttons skip out-of-pool palettes).
- A breakdown palette that is out of the pool holds the current palette and logs
  `'<filter>': breakdown palette '<name>' is out of the pool; holding '<live>'`.
- A pad press on an out-of-pool palette makes it live at once and logs
  `'<filter>': manual '<name>' (out of the automatic pool)`.
- A toggle changes the pool for the next automatic change only: it never changes the live
  palette, interrupts a crossfade or a drop cycle, or resets the seeded random stream.
- One palette left in the pool: rotation holds on it. None left: automatic changes keep the live
  palette and log `'<filter>': automatic pool is empty; holding '<live>'` once, then
  `automatic pool has a palette again` when a palette returns to the pool.
- Saved on the palette, in the filter settings and the set file (pools are per set): key `pool`,
  written only when `false`. Set files stay schema 2; a 2.0 set or filter loads with every palette
  in the pool and round-trips byte-identical.
- With every palette in the pool, the scheduler's decisions are identical to 2.0 (the Palette
  Director 1.2.0 equivalence golden is unchanged).

## 2.0.0

Part of Director 2.0.0 (Music Director + Palette Director + Clip Director + Scene Director, one version).

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

## 1.2.0

- New dock **Music**: BPM, confidence, phrase / bar / beat, the section and how long it
  has held, beat lights, the level and bass meter with the kick lamp, the last 64 beats
  (palette strip, section band, lock line, phrase ticks and bass trace), and the audio
  controls (audio source, Input gain, Kick sensitivity, Fallback BPM, Phrase length).
  Everything about hearing the music lives there; the Palette Director dock keeps palettes,
  look, changes and lock behaviours.
- New commands in the Music dock: **Phrase is now** (re-anchor the phrase and bar to this
  beat), **Drop now**, **Force breakdown** (Clip Director and Synesthesia follow through
  the clock's section), **Tap tempo**, **Lock BPM** and **Nudge**.
- Windows-only packaging: releases ship the Windows x64 zip. The Ubuntu and macOS
  packaging jobs are gone; the unit tests still run on Ubuntu.

## 1.1.0

- Read-only beat clock for other plugins: the global procedures
  `palette_director_instances` and `palette_director_clock`, with the consumer header
  `include/pd-clock-api.h`. Clip Director uses it to cut clips on the same beat grid as
  the palettes.
- No behaviour change: the Director made exactly the same decisions as in 1.0.1.

## 1.0.1

- PANIC is a toggle: press it again, or Resume, to release.
- Input gain and Kick sensitivity controls.
- Audio meter in the panel.
- Drop colour cycle.

## 1.0.0

- First release: the Palette Lock filter, the Palette Director dock, the audio-reactive
  director, and the bundled Neon Default set.
