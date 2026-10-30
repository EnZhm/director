# Music Director

Music Director owns **the clock** of the Director suite: it hears the music, locks a beat grid to
it, labels GROOVE / BREAKDOWN / DROP sections, and publishes all of it over one OBS procedure.

```
music -> Music Director (the clock) -> palette (Palette Director) / clips (Clip Director) / Synesthesia (Scene Director)
```

Palette Director, Clip Director and Scene Director **require** Music Director: they are followers,
they do not analyse audio themselves. Without it they idle (one log line, "Waiting for Music
Director") and keep rendering the live state.

## What it does

- Captures one configured audio source, downmixes to mono and analyses it with the Palette Director
  1.2.0 analysis core (unchanged: the same audio gives the same beat grid and section decisions).
- Estimates BPM, tracks beats and phrases, and detects silence (fallback BPM).
- Detects the section (GROOVE / BREAKDOWN / DROP) and logs every change:
  `[music-director] section -> DROP seq=3 beat=48 (natural|forced)`.
- Registers `void director_clock(...)` on the global proc handler — the contract in
  `include/director-clock.h`. It is a **singleton**: if the proc already answers, this copy logs an
  error and registers nothing.
- Adds the follower signals on top of the old Palette Director clock: `section_seq` (+1 on every
  section change), `section_beat` (the beat a section starts on) and `epoch` (+1 whenever the beat
  grid is re-anchored). Plus `bass` (0..1) for breathing effects, and `phrase_beat` (2.1: the beat
  the current phrase started on; bar lines are `phrase_beat + 4k`).

## Docks

| Dock id | Content |
|---|---|
| `music-director-dock` | BPM / confidence, phrase.bar.beat, beat lights, section, level/bass meter, 64-beat timeline, the audio source picker, input gain, kick sensitivity, fallback BPM, phrase length, and the clock buttons (Tap tempo, Phrase is now, Drop now, Force breakdown, Lock BPM, Nudge). |

## Hotkeys

`md_phrase_now`, `md_drop_now`, `md_force_breakdown`, `md_tap_tempo`, `md_lock_bpm`,
`md_nudge_back`, `md_nudge_fwd`. Bind them in Settings > Hotkeys to a key, or to a Stream Deck
button with Stream Deck's OBS hotkey action. `md_drop_now` ("Drop now") is the one a controller
usually drove in 2.0.

## Upgrading from 2.0: rebinding a controller

Music Director has no MIDI in. MIDI Director (the 2.0 MIDI-in dock) was removed in 2.1; the archive
is tag `2.0.0` ([docs/archive/midi-director.md](../../docs/archive/midi-director.md)).

A scene collection's saved `midi_director` mappings are kept verbatim and inactive, and written back
on every save. On load Music Director logs, once per collection:

```
[music-director] MIDI Director was removed in 2.1: <N> mappings kept in the scene collection (inactive), copy at <path>
[music-director] MIDI Director mapping 'Note 36 ch 1' -> md_drop_now (Drop now) is inactive: bind Drop now in Settings > Hotkeys or on a Stream Deck
```

The first line counts the mappings and names the JSON copy
(`%APPDATA%\obs-studio\plugin_config\music-director\midi-director-<collection>.json`). Each further
line is one mapping: the control, the hotkey it drove, and where to bind it now.

| 2.0 mapping target | Rebind in 2.1 |
|---|---|
| `scene_director_next` / `scene_director_prev` | In Synesthesia's MIDI panel: (1) devices tab, click "refresh connected devices" until the controller shows as connected; (2) scenes tab, map the playlist **next** and **previous** (and **play**) to pads. Scene Director logs those changes as `external scene change: <scene>` and sends nothing |
| `md_drop_now` | OBS Settings > Hotkeys > "Music Director: Drop now": press a key. Or a Stream Deck button with Stream Deck's OBS hotkey action set to that hotkey |
| `md_force_breakdown`, `md_tap_tempo`, `md_lock_bpm`, `md_nudge_*`, `md_phrase_now`, palette and clip hotkeys | The same way: a key or a Stream Deck button |

## Settings and migration

Settings live in the scene collection under `music_director` (`audio_source`, `input_gain_db`,
`kick_sensitivity`, `fallback_bpm`, `phrase_beats`, `enabled`, `migrated_from`, plus the `md_*`
hotkey bindings).

On the first load of a scene collection that has no `music_director` key, Music Director copies the
audio settings and the bound clock hotkeys from a Palette Lock filter (Palette Director 1.2.0): the
first ENABLED filter with a user `audio_source`, else the first Palette Lock. It **never writes** to
the filter; the Palette Lock keeps its settings. If several Palette Lock filters hold different
audio values, the others are named in a warning.

## Building and tests

```
# the pure-C++ clock core + doctest suite (no libobs/Qt)
cmake -S plugins/music-director/tests -B build-tests -G Ninja
cmake --build build-tests && ctest --test-dir build-tests --output-on-failure

# the OBS plugin (libobs + Qt6 + obs-frontend-api)
cmake -S plugins/music-director -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The clock core (`md_core`) is a libobs-free, Qt-free static library: `tests/xplugin` links it with
Palette Director's scheduler to prove equivalence with Palette Director 1.2.0.
