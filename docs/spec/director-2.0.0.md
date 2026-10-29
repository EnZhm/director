# Director 2.0.0 specification

"The music controls the palette, the video clips and the Synesthesia scenes."

Signal chain: music -> Music Director (the clock) -> palette (Palette Director) / clips (Clip Director) / Synesthesia (Scene Director, MIDI out).

## 1. Repo, version, package

| Path | Content |
|---|---|
| `plugins/music-director` | Music Director: audio analysis, THE clock, Music dock, MIDI Director |
| `plugins/palette-director` | Palette Director: the Palette Lock filter, follows the clock |
| `plugins/clip-director` | Clip Director: clip source with Spout out, follows the clock |
| `plugins/scene-director` | Scene Director: Synesthesia playlist Next/Prev over MIDI out, follows the clock |
| `include/director-clock.h` | The one clock contract (`api_version`) |
| `common/director-follow.h` | Follower edge detector (exactly-once section/phrase/beat edges) |
| `common/midi/` | WinMM MIDI helper shared by Music Director (MIDI in) and Scene Director (MIDI out) |
| `tests/xplugin` | Cross-plugin tests |
| `tools/` | `check-version.sh`, `package-suite.sh`, `collision-audit` |

- One version: `VERSION` (2.0.0). Every `plugins/*/buildspec.json` carries it (`tools/check-version.sh`, enforced in CI); each plugin's version string comes from its buildspec.
- One CI (`.github/workflows/director.yaml`): version check, clang-format 19, every plugin's tests and `tests/xplugin`, the collision audit, a Windows x64 build of each plugin, and the suite package `director-2.0.0-windows-x64.zip` (four plugin folders, INSTALL.txt, LICENSE, SHA256SUMS.txt).
- Install: unzip into `C:\ProgramData\obs-studio\plugins\`. Upgrading from 1.x: remove the old `palette-director`, `clip-director` and `scene-director` folders first.

## 2. The clock (Music Director)

- Singleton. Music Director registers `director_clock` on the global proc handler (contract and field list: `include/director-clock.h`). Before registering it calls the proc; if it already answers, this copy logs an error and registers nothing.
- The fields keep the Palette Director 1.x clock style, so a follower changes only its source: `pd_clock_read(id, source, filter, &k)` becomes `director_clock_read(&k)`. New: `section_seq`, `section_beat`, `epoch`, `bass`. Not in the contract: instance ids, `filter_enabled`, `drop_cycle*` (palette-owned, see 4.4).
- The core moves from Palette Director 1.2.0 unchanged: `analysis`, `beat_tracker`, `section_state`, `command_queue`, `rng`, the clock half of `director.cpp`, and `clock_api.h` (AudioClockMap, clock_extrapolate). The same audio gives the same beat grid and section decisions as PD 1.2.0.
- `section_seq` increments on every section change, natural or forced; `section_beat` is the beat the section starts on. Drop now / Force breakdown land on the next beat, as in PD 1.2.0. `epoch` increments when the grid is re-anchored (Phrase now, tap, nudge, audio source change, reset).
- The worker thread publishes the snapshot about every 5 ms.
- Log prefix `[music-director]`. Each section change logs `section -> <GROOVE|BREAKDOWN|DROP> seq=<S> beat=<B> (<natural|forced>)`.

## 3. Music Director: settings, dock, hotkeys, MIDI Director, migration

- Settings live in the scene collection (frontend save/load callback) under `music_director`: `audio_source`, `input_gain_db`, `kick_sensitivity`, `fallback_bpm`, `phrase_beats`, `enabled`, `migrated_from`, plus MD hotkey bindings.
- Music dock (`music-director-dock`): Palette Director 1.2.0's Music dock with the audio source picker, input gain, kick sensitivity, fallback BPM and phrase length added. It shows the audio meter, BPM/confidence, beat and phrase lights, the section, Tap / Lock BPM / Nudge back / Nudge forward and Phrase now / Drop now / Force breakdown.
- Frontend hotkeys: `md_phrase_now`, `md_drop_now`, `md_force_breakdown`, `md_tap_tempo`, `md_lock_bpm`, `md_nudge_back`, `md_nudge_fwd`.
- MIDI Director (`midi-director-dock`), moved from Scene Director 2.0.0: MIDI in over `common/midi`, MIDI learn, maps notes and CCs to any OBS hotkey. That covers Music (md_*), Palette (Palette Director hotkeys), Clips (Clip Director hotkeys) and Synesthesia (`scene_director_next/prev`). Mappings keep their storage key `midi_director`, so existing mappings load unchanged. Music Director is the only plugin with MIDI in.

### 3.1 Migration from Palette Director 1.2.0 (automatic, first load)
On `OBS_FRONTEND_EVENT_FINISHED_LOADING` and `OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED`, when the collection has no `music_director` key:
1. Enumerate Palette Lock filters (`palette_lock_filter`) in `obs_enum_sources` order, then each source's filters in order.
2. Pick the first ENABLED filter whose settings carry a user value for `audio_source`. If there is none, pick the first Palette Lock.
3. Copy its `audio_source`, `input_gain_db`, `kick_sensitivity`, `fallback_bpm` and `phrase_beats` verbatim. Where the filter has no user value, use PD 1.2.0's defaults.
4. Copy the bound clock hotkeys from the filter's `hotkey_bindings` (`pd_phrase_now`, `pd_drop_now`, `pd_force_breakdown`, `pd_tap_tempo`, `pd_lock_bpm`, `pd_nudge_back`, `pd_nudge_fwd`) to the matching `md_*`.
5. Record `migrated_from` = {source, filter, values} and log `[music-director] migrated audio settings from '<filter>' on '<source>': ...`. The Music dock shows the same.
6. If other Palette Lock filters hold different audio values, log a warning that names them.

Music Director never writes Palette Lock settings. No Palette Lock and no key means MD defaults.

## 4. Palette Director (follower)

- The filter id `palette_lock_filter` is unchanged.
- Removed: audio capture, analysis, beat tracker, section state, the Music dock, the 7 clock hotkeys and their commands, the clock procs (`palette_director_clock`, `palette_director_instances`), and the copied `pd-clock-api.h`.
- Kept: Palette Lock filter, sets, palette scheduler, lock behaviours, drop colour cycle, palette commands and hotkeys, and the Palette Director dock (palette parts; clock status read-only from MD).
- Every ~5 ms the worker reads `director_clock_read()` through `director_follow::Follower`. On edges it calls the scheduler in PD 1.2.0's order: section, then phrase, then beat, then `on_tick(beats_now)`. Breathing uses the clock's `bass`.
- Log `[palette-director] '<filter>': section -> <NAME> seq=<S> beat=<B>` for each section edge.

### 4.1 Presets EXACT
- PD reads and writes every 1.2.0 filter-setting and set-file key. Keys it no longer uses (`audio_source`, `input_gain_db`, `kick_sensitivity`, `fallback_bpm`, `phrase_beats`) are preserved verbatim, never removed and never rewritten with defaults. Set files keep schema 2 and round-trip byte-identical. Entries for retired hotkeys in `hotkey_bindings` are preserved verbatim.
- Spectrum palettes (16 gradient_map palettes, 5-8 stops) load and render unchanged.

### 4.2 Render guards
- `pl_sanitize_for_render()` is a pure function (no libobs). It clamps an empty palette set (built-in fallback palette), live/prev out of range, `n_colours` outside [1, PL_MAX_COLOURS], and an unknown mode (gradient_map). It returns a mask of what it fixed. The render path uses it and logs a warning once per filter per cause, never per frame. The same normalisation runs on set load. Unit tests: empty set, n_colours 0 and 99, mode 7, live/prev out of range, and a spectrum set sanitising to a no-op.

### 4.3 No Music Director
- No crash. PD logs once: `[palette-director] Music Director not found: palettes hold, director idle`. The dock status reads "Waiting for Music Director", and the filter keeps rendering the live palette.

### 4.4 Drop colour cycle signal
- Clip Director's stutter follows PD's drop colour cycle. PD registers `void palette_director_drop_cycle(out bool active, out int steps_per_beat)`, which is true while any Palette Lock runs a drop cycle. It is not a clock.

## 5. Clip Director (clips only, follower)

- From 1.1.0, remove the Synesthesia OSC tab and transport, and the MIDI Director dock and MIDI input.
- The clock comes from `director_clock_read()` through `director_follow::Follower`. The `clock_source` setting key stays in saved data but is no longer shown.
- Stutter reads `palette_director_drop_cycle` when PD is present; without PD, stutter stays off.
- Unchanged: source id `clip_director_source`, dock `clip-director-dock`, CD hotkeys and all clip settings. 1.1.0 collections load EXACT.
- No MD: idle, one log line, status "Waiting for Music Director".
- Log `[clip-director] cut ... seq=<S> beat=<B>` for section-triggered cuts.

## 6. Scene Director (Synesthesia MIDI out, follower)

- Only `scene_director_source` is registered (the `clip_director_source` alias is removed), and the no-op `video_render` is kept.
- Scene Director sends Synesthesia playlist Next/Prev over MIDI out (`common/midi`). It keeps the MIDI out device, the Next/Prev notes and channel, the `scene_director_next/prev` hotkeys, the triggers (breakdown, drop, groove phrase), cooldown and lead. The MIDI Director dock moves to Music Director.
- The clock comes from `director_clock_read()` through `director_follow::Follower`; the `clock_source` key is kept and hidden.
- With several SD sources, one section edge sends exactly ONE Next, through a process-wide guard keyed by `section_seq`.
- No MD: idle, one log line.
- Log `[scene-director] MIDI Next seq=<S> beat=<B>` on each section-triggered Next.

## 7. Tests

- MD: analysis, BPM, sections, silence, clock API, music commands and determinism (moved from PD), migration picker, MIDI map.
- PD: palette, set-file, scheduler, drop cycle, palette commands, cbuffer and host API, plus a follower test (scheduler driven by scripted clock samples) and the render-guard tests.
- `tests/xplugin`:
  - (a) director-follow edge cases.
  - (b) One scripted clock sequence drives the PD scheduler, the CD scheduler and the SD switch scheduler. It covers natural and forced sections, epoch changes, and MD lost and found. Each handles every `section_seq` exactly once, on the same `section_beat`.
  - (c) Equivalence: the MD core plus the PD scheduler on the fixture WAVs give the same palette event log as PD 1.2.0's Director (golden captured from 1.2.0).

## 8. Collision audit

`tools/collision-audit` runs in CI. It checks source/filter ids, dock ids, hotkey names, config and scene-collection keys, proc handler names, signal names and log prefixes, and requires zero overlaps across the four plugins.

## 9. Gate 3 (show rig)

The real show collection, with all four plugins installed from the CI suite zip:
- A forced breakdown and a forced drop each give the palette change, the clip cut and the MIDI Next on the same beat. Every follower logs `seq` and `beat`, and all of them equal MD's `section_beat`.
- No crash.
- Presets EXACT, and the migrated audio settings equal the old Palette Lock values.
- The soak runs clean.
- On failure, roll back to the pre-gate snapshot.
