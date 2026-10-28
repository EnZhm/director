# Changelog

Palette Director for OBS Studio 32.2.x, Windows x64. Part of the
[Director](https://github.com/EnZhm/director) suite.

This repository's first release is 1.2.0. The 1.0.0, 1.0.1 and 1.1.0 builds were made
before it and are listed here for completeness; their files are not republished.

## 1.2.0

- New dock **Music**: BPM, confidence, phrase / bar / beat, the section and how long it
  has held, beat lights, the level and bass meter with the kick lamp, the last 64 beats
  with director-action markers, and the audio controls (audio source, Input gain, Kick
  sensitivity, Fallback BPM, Phrase length). Everything about hearing the music lives
  there; the Palette Director dock keeps palettes, look, changes and lock behaviours.
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
