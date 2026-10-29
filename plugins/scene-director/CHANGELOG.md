# Changelog

Scene Director for OBS Studio 32.2.x, Windows x64. Part of the
[Director](https://github.com/EnZhm/director) suite.

This repository was called **Clip Director** up to 1.1.0; the plugin was renamed to Scene
Director in 2.0.0, and the repository with it
([github.com/EnZhm/scene-director](https://github.com/EnZhm/scene-director)).

## 2.0.0

Breaking release: the plugin is renamed, the clip pipeline is gone, and the clock is now Music
Director's.

- **Renamed to Scene Director.** The plugin is `scene-director` (loaded as
  `[scene-director] plugin loaded successfully (version 2.0.0)`), with the source and dock
  display name "Scene Director".
- **Clip playback removed**: the clip pipeline (media sources, folder scanning, the shuffle
  bag, weights, bans, polarity, level evening, alpha), the crossfade / composite / measure
  effects, the decks, thumbnails, the clip sections, the Spout-facing clip output and the
  ingest tool `tools/cd-ingest.py`.
- **OSC removed**: the Synesthesia OSC tab, the OSC transport, the scene pools and Import
  from Synesthesia. The source renders nothing now (`0 x 0`, no video output); filters left
  on it from Clip Director (Spout output, shaders) still load and render nothing.
- **Skip / Hold / Ban hotkeys removed.**
- **New: beat-synced MIDI Next / Previous output.** Scene Director follows **Music Director's**
  beat clock and sends the message when a **breakdown** starts and on the **drop downbeat** -
  landing on the same beat as the palette change and the clip cut (Music Director's
  `section_beat`); the dock's **Next** / **Prev** buttons and the hotkeys
  `scene_director_next` / `scene_director_prev` send it by hand. The target, the triggers,
  the cooldown and Scene lead are configurable.
- **Removed: Fallback BPM and the groove-phrase trigger**; Scene Director sends Next on
  breakdowns, drops and manual Next/Prev only.
- **Requires Music Director** for the clock. Without it Scene Director idles: no automatic
  switches, one log line, and the dock reads "Waiting for Music Director". Manual Next / Prev
  still work.
- **Forced and manual switches bypass the cooldown.** The cooldown (16 beats by default) holds
  back an *automatic* section switch that falls too close to the previous one; a forced section
  (Force breakdown / Drop now) and any manual Next / Prev always go through.
- **One Next per section edge** even with several Scene Director sources on the one clock,
  through a process-wide guard keyed by Music Director's `section_seq`.
- **MIDI out** goes through one shared WinMM module: the device is picked by name, re-resolved
  quietly when it is unplugged or when a virtual port starts after OBS, and a Note message gets
  its note off 50 ms later.
- **New dock "Scene Director"**: the clock and section, the MIDI out link chip and device,
  Next / Prev, the trigger toggles, the cooldown, Scene lead, the Next and Prev message editors
  (Note or Control change, channel, number) and a log of every switch with its beat, reason and
  bytes. The **MIDI Director dock has moved to Music Director**: MIDI learn and mapping a MIDI
  note or CC to any OBS hotkey in the suite now live there.
- Settings are read from the source's settings with defaults for the new keys: old clip and
  OSC keys are ignored and left in place, so a scene collection written by 1.1.0 keeps
  loading. The obsolete `clock_source` key is kept in saved data but no longer shown or read.
- The `clip_director_source` alias is **removed**: only `scene_director_source` is registered,
  and **Clip Director owns the `clip_director_source` id** now. A scene collection that still
  holds a "Clip Director" source must re-create it as a Scene Director source.
- Unit tests: the switch scheduler (breakdown and drop landing beats, the cooldown and its
  forced / manual bypass), the settings, a scripted follower test (exactly one Next per
  `section_seq`), and the one-Next guard.

### Upgrading from Clip Director 1.1.0

- **Delete `C:\ProgramData\obs-studio\plugins\clip-director` before copying the
  `scene-director` folder.** Clip Director owns the `clip_director_source` id now.
- **A "Clip Director" source from 1.1.0 no longer loads** (the alias is gone): re-add it as a
  Scene Director source; it renders nothing.
- **Remove any Spout filter that was on one of those sources** (it no longer has a picture).
  The Spout input that carries Synesthesia into OBS is unaffected.
- **MIDI Director mappings to the removed hotkeys are dropped** (`cd_skip`, `cd_hold`,
  `cd_ban`, `cd_syn_next_scene`, `cd_syn_hold_scene`, `cd_syn_bang`); rebind them to
  `scene_director_next` / `scene_director_prev`. The MIDI Director dock now lives in Music
  Director; its mappings keep their storage key.
- **Install Music Director** (the suite zip) - it is the clock Scene Director follows.
- **Add a virtual MIDI port** (loopMIDI, or a Windows MIDI Services loopback pair), pick it
  as Scene Director's MIDI out, and MIDI-learn Next / Prev on Synesthesia's playlist.

## 1.1.0

- Synesthesia scene control over OSC: link chip, host and ports, Groove / Breakdown /
  Drop scene pools, Next scene / Hold scene / Fire bang 1, Import from Synesthesia, and
  a log of every message with its beat position and reason.
- Scene launches landed on the same beat as the clip transition they paired with: the start
  of the breakdown crossfade and the drop downbeat.
- New dock **MIDI Director**: learn mode maps MIDI notes and CCs to OBS hotkeys across
  the suite.
- Windows-only packaging: releases shipped the Windows x64 zip; the core unit tests still
  ran on Ubuntu.

## 1.0.0

- First release: the Clip Director source (clip folders, beat-aligned cuts, deep
  shuffle, level evening, brightness inversion), the Clip Director dock, and the ingest
  tool `tools/cd-ingest.py`.
