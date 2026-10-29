# Changelog

Scene Director for OBS Studio 32.2.x, Windows x64. Part of the
[Director](https://github.com/EnZhm/director) suite.

This repository was called **Clip Director** up to 1.1.0; the plugin was renamed to Scene
Director in 2.0.0, and the repository with it
([github.com/EnZhm/scene-director](https://github.com/EnZhm/scene-director)).

## 2.0.0

Breaking release: the plugin is renamed and the clip pipeline is gone.

- **Renamed to Scene Director.** The plugin is `scene-director` (loaded as
  `[scene-director] plugin loaded successfully (version 2.0.0)`), with the source and dock
  display name "Scene Director".
- **Clip playback removed**: the clip pipeline (media sources, folder scanning, the shuffle
  bag, weights, bans, polarity, level evening, alpha), the crossfade / composite / measure
  effects, the decks, thumbnails, the clip sections, the Spout-facing clip output and the
  ingest tool `tools/cd-ingest.py`.
- **OSC removed**: the Synesthesia OSC tab, the OSC transport, the scene pools and Import
  from Synesthesia. The source renders nothing now (`0 x 0`, no video output).
- **Skip / Hold / Ban hotkeys removed.**
- **New**: beat-synced **MIDI Next / Previous output**. Following Palette Director's clock,
  Scene Director sends the message when a **breakdown** starts (on the breakdown crossfade)
  and on the **drop downbeat**, and optionally on every phrase downbeat during a groove; the
  dock's **Next** / **Prev** buttons and the hotkeys `scene_director_next` /
  `scene_director_prev` send it by hand. The target, the triggers, the cooldown and Scene
  lead are configurable.
- **MIDI out** goes through one shared WinMM module (the same one the MIDI Director dock
  reads its input through): the device is picked by name, re-resolved quietly when it is
  unplugged or when a virtual port starts after OBS, and a Note message gets its note off
  50 ms later.
- **New dock "Scene Director"**: the clock and section, the MIDI out link chip and device,
  Next / Prev, the trigger toggles, the cooldown, Scene lead, the Next and Prev message
  editors (Note or Control change, channel, number) and a log of every switch with its beat,
  reason and bytes. The MIDI Director dock stays, and its hotkey list now contains the new
  hotkeys.
- Settings are read from the source's settings with defaults for the new keys: old clip and
  OSC keys are ignored and left in place, so a scene collection written by 1.1.0 keeps
  loading.
- The `clip_director_source` source id is still registered, as a deprecated alias that is
  hidden from the Add menu, so existing "Clip Director" sources load unchanged (they render
  nothing).
- Unit tests: the switch scheduler, the MIDI encoder, the settings migration from a 1.1.0
  settings object, and the MIDI Director map.

### Upgrading from Clip Director 1.1.0

- **Delete `C:\ProgramData\obs-studio\plugins\clip-director` before copying the
  `scene-director` folder.** Both register the same source id.
- **Existing "Clip Director" sources keep loading** through the deprecated alias and render
  nothing; there is no need to re-create them.
- **Remove any Spout filter that was on one of those sources** (it no longer has a picture).
  The Spout input that carries Synesthesia into OBS is unaffected.
- **MIDI Director mappings to the removed hotkeys are dropped** (`cd_skip`, `cd_hold`,
  `cd_ban`, `cd_syn_next_scene`, `cd_syn_hold_scene`, `cd_syn_bang`); rebind them to
  `scene_director_next` / `scene_director_prev`.
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
