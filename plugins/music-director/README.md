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
  grid is re-anchored). Plus `bass` (0..1) for breathing effects.

## Docks

| Dock id | Content |
|---|---|
| `music-director-dock` | BPM / confidence, phrase.bar.beat, beat lights, section, level/bass meter, 64-beat timeline, the audio source picker, input gain, kick sensitivity, fallback BPM, phrase length, and the clock buttons (Tap tempo, Phrase is now, Drop now, Force breakdown, Lock BPM, Nudge). |
| `midi-director-dock` | MIDI in (Windows): learn a MIDI note or CC and map it to **any** OBS hotkey — Music (`md_*`), Palette, Clips or Synesthesia. Mappings are stored with the scene collection under `midi_director`, so existing mappings load unchanged. |

## Hotkeys

`md_phrase_now`, `md_drop_now`, `md_force_breakdown`, `md_tap_tempo`, `md_lock_bpm`,
`md_nudge_back`, `md_nudge_fwd` (bind them in Settings > Hotkeys).

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
