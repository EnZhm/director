# Changelog

Clip Director for OBS Studio 32.2.x, Windows x64. Part of the
[Director](https://github.com/EnZhm/director) suite.

This repository's first release is 1.1.0. The 1.0.0 build was made before it and is
listed here for completeness; its files are not republished.

## 2.0.0

- Clips only. Synesthesia OSC scene control and the MIDI Director dock are gone:
  Synesthesia is driven by **Scene Director**, and MIDI by **Music Director**.
- The clock comes from **Music Director** (`director_clock`). The `clock_source` setting
  key stays in saved data but is no longer read or shown. Without Music Director the clips
  hold: one log line, and the dock status reads "Waiting for Music Director".
- **Three armed decks**: the next clip of the section, an armed Breakdown-pool clip and an
  armed Drop-pool clip, each loaded, seeked and paused ahead of time. A breakdown or drop
  edge now cuts on the section beat, so the palette change, the clip cut and Scene
  Director's Synesthesia Next land together. A return to groove keeps the bar-line rule.
- Stutter follows Palette Director's drop colour cycle (the `palette_director_drop_cycle`
  signal); without Palette Director it stays off.
- Clips, dock and hotkeys are unchanged: 1.1.0 collections load exact.

## 1.1.0

- Synesthesia scene control over OSC: link chip, host and ports, Groove / Breakdown /
  Drop scene pools, Next scene / Hold scene / Fire bang 1, Import from Synesthesia, and
  a log of every message with its beat position and reason. OSC needs a Synesthesia Pro
  licence.
- Scene launches land on the same beat as the clip transition they pair with: the start
  of the breakdown crossfade and the drop downbeat.
- New dock **MIDI Director**: learn mode maps MIDI notes and CCs to OBS hotkeys across
  the suite (Palette Director, Music, Clip Director, Synesthesia).
- Windows-only packaging: releases ship the Windows x64 zip. The Ubuntu and macOS
  packaging jobs are gone; the core unit tests still run on Ubuntu.

## 1.0.0

- First release: the Clip Director source (clip folders, beat-aligned cuts, deep
  shuffle, level evening, brightness inversion), the Clip Director dock, and the ingest
  tool `tools/cd-ingest.py`.
