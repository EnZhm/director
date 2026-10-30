# MIDI Director (archived)

MIDI Director was the MIDI-in dock in Music Director 2.0.0 (moved there from Scene Director). It
listened to one MIDI input device (Windows, WinMM), learned a note or CC and mapped it to any
registered OBS hotkey: Music Director's `md_*`, Palette Director, Clip Director, and Scene
Director's `scene_director_next` / `scene_director_prev`. Mappings were saved with the scene
collection under the `midi_director` key.

It was removed in Director 2.1. Synesthesia is driven over OSC by Scene Director, controllers are
mapped in Synesthesia's own MIDI panel, and Director's actions are OBS hotkeys on a keyboard or a
Stream Deck.

## Archive

Tag `2.0.0` (`cc5bd6b`) is the archive: the last state with the dock and `common/midi`. There is no
separate archive branch.

```
git checkout 2.0.0 -- plugins/music-director/src/dock/midi-director-dock.cpp
```

## Removed paths

| Path | What it was |
|---|---|
| `plugins/music-director/src/dock/midi-director-dock.{h,cpp}` | The dock: device picker, learn, mapping table, message monitor |
| `mdhotkey::list` / `mdhotkey::trigger` (`plugins/music-director/src/obs/md-hotkeys.*`) | Enumerate and fire any OBS hotkey by identity |
| `common/midi/` (`director-midi.*`, `midi_map.*`, `midi_msg.*`) | WinMM MIDI in and out, the mapping model, the message assembler |
| `winmm` link in `plugins/music-director/CMakeLists.txt` | The Windows MIDI system library |
| `tests/xplugin/test_midi_map.cpp`, `test_midi_msg.cpp`, target `director_midi_core` | Tests of the shared MIDI code |
| `Midi*` keys in `plugins/music-director/data/locale/en-US.ini` | Dock strings |

Kept: every `md_*` frontend hotkey and its saved bindings under `music_director`, the Music dock,
the clock and the 2.0 migration.

## Saved mappings

- Music Director reads the collection's `midi_director` value on load and writes it back verbatim
  on save, so a 2.1 save never drops it and a 2.0.0 install still finds its mappings.
- When the value holds mappings, Music Director logs once per collection and session:
  `[music-director] MIDI Director was removed in 2.1: <N> mappings kept in the scene collection (inactive), copy at <path>`
- It writes the same JSON once to `plugin_config/music-director/midi-director-<collection>.json`
  (characters outside `A-Z a-z 0-9 space . _ -` in the collection name become `_`).
- It logs one rebind hint per mapping, for example:
  `[music-director] MIDI Director mapping 'Note 36 ch 1' -> md_drop_now (Drop now) is inactive: bind Drop now in Settings > Hotkeys or on a Stream Deck`

## Rebinding

| Action | 2.0 | 2.1 |
|---|---|---|
| Synesthesia Next / Prev / scene / meta from a controller | Controller -> MIDI Director -> `scene_director_next/prev` | Map the controller in Synesthesia's own MIDI panel. Scene Director logs the change as `external` and leaves it alone |
| Drop now | Controller -> MIDI Director -> `md_drop_now` | Bind `md_drop_now` to a key or a Stream Deck button (OBS Settings > Hotkeys, or Stream Deck's OBS hotkey action) |
| Other Director hotkeys (Force breakdown, Tap, Nudge, palette, clips) | Controller -> MIDI Director | Keyboard or Stream Deck, as above |

OBS's saved dock layout may still name `midi-director-dock`; Qt ignores unknown docks.
