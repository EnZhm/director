# Clip Director

Clip Director is an OBS Studio plugin (Windows x64) for VJ sets. It is an OBS source that
plays folders of video clips in time with the music: it follows the
[Music Director](https://github.com/EnZhm/director) beat clock, cuts on the beat, shuffles
deeply, evens out brightness, and sends the picture to Synesthesia over Spout. Part of the
[Director](https://github.com/EnZhm/director) suite.

| | |
|---|---|
| Version | 2.0.0 |
| Platform | Windows 10/11 x64 |
| OBS Studio | 32.2.x (built against 32.2.2) |
| Needs | Music Director 2.0.0 for the beat clock |
| Licence | GPL-2.0 |

## What it adds

| Added | Name | Where it goes |
|---|---|---|
| Source | Clip Director | Any scene; plays clip folders, output over Spout to Synesthesia |
| Dock | Clip Director | Now playing, the three armed decks, Skip / Hold / Ban, clip folders, the log |

Clip Director cuts on Music Director's beat grid. Music Director is the one clock of the
suite: without it, Clip Director holds the current clip (see below).

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | 32.2.2 at release. |
| Music Director 2.0.0 | The beat clock: `director_clock`. Installed by the suite zip. |
| Synesthesia, Pro licence | For the Spout input. |
| Python 3 and ffmpeg | Optional, for `tools/cd-ingest.py`; local disc only, OBS is not involved. |

## Install

1. Install the suite: download `director-2.0.0-windows-x64.zip` from
   [EnZhm/director releases](https://github.com/EnZhm/director/releases) and copy the four
   plugin folders into `C:\ProgramData\obs-studio\plugins\`. **Upgrading from 1.x: remove the
   old `palette-director`, `clip-director` and `scene-director` folders first.**
2. If you only want Clip Director, take `clip-director-2.0.0-windows-x64.zip` from
   [this repo's releases](https://github.com/EnZhm/clip-director/releases) and copy the
   folder `clip-director` into `C:\ProgramData\obs-studio\plugins\`. Music Director 2.0.0
   must already be installed for the clock.
3. Start OBS and check Help > Log Files > View Current Log for:

   ```
   [clip-director] plugin loaded successfully (version 2.0.0)
   [music-director] plugin loaded successfully (version 2.0.0)
   ```

Step-by-step install, clip preparation, cut rules, troubleshooting and uninstall:
[release/INSTALL.txt](release/INSTALL.txt).

## Prepare the clip folders

Raw downloads usually have a keyframe only every ~8 seconds, so a random start point can
take longer than a beat, and there are no brightness statistics. The ingest tool writes a
cleaned copy next to the originals:

```
python tools\cd-ingest.py "D:\Clips\groove" --dry-run
python tools\cd-ingest.py "D:\Clips\groove"
```

- Needs Python 3 and ffmpeg on PATH; uses the NVIDIA encoder when there is one.
- Writes into a `_cd` folder inside the clip folder and never changes the originals
  (hashes are checked before and after).
- Resumable: run it again after adding clips and only the new ones are encoded.
- Add the **original** folder to Clip Director; it finds the `_cd` copy itself and says
  so in the log ("from ingest index").

## The Clip Director dock

Open it from Docks > "Clip Director". The dock has one page: the source, what is playing
and the three armed decks, Skip / Hold / Ban, the clip folders, the log, and the footer.

| Area | Contents |
|---|---|
| Top | The source, the clock (`MD` = following Music Director, otherwise `Waiting for Music Director`), the section (GROOVE / BREAKDOWN / DROP), four beat lights, and when the next cut comes |
| Now playing | Thumbnail, clip, folder, start point, speed, inverted or not; Invert (auto / always / never, saved per clip) and Ban clip (never play it again, saved) |
| Next / Breakdown / Drop | The three armed decks: the next clip of this section, a Breakdown-pool clip and a Drop-pool clip |
| Controls | Skip (cut on the next beat), Hold (no cuts and no stutter until released) |
| Folders | Per-folder on/off, section tag (Any / Breakdown / Drop / Groove) and weight 0-10 |
| Log | Every cut, drop, breakdown, stutter, skip, hold and ban |
| Footer | Clip count, how many have stats, how many are banned, cuts so far, average cut timing error |

Folder rules: `Any` folders play in grooves and anywhere nothing is tagged. Once any
folder carries `Breakdown` or `Drop`, that section plays only folders with that tag. A
folder whose name contains "breakdown", "drop" or "groove" starts with that tag. All of
this is saved with the scene collection.

### Three armed decks

Three clips are loaded, seeked and paused ahead of time, so a section change cuts at once:

| Deck | Holds |
|---|---|
| Next | the next normal clip of the current section |
| Breakdown | a Breakdown-pool clip, armed while the section is **not** a breakdown |
| Drop | a Drop-pool clip, armed while the section is **not** a drop |

A breakdown or drop edge cuts its armed deck **on the section beat** (a breakdown crossfade
starts on that beat), so the palette change, the clip cut and Scene Director's Synesthesia
Next all land together. If an armed deck has not had its preroll yet, Clip Director falls
back to the next bar line. A return to groove keeps the bar-line rule (the next bar line at
least `preroll_beats` ahead). After an edge the used deck is re-armed and the Next deck is
prepared for the new section.

### No Music Director

Without Music Director there is no clock, so the clips hold: the current clip keeps
playing and no cut is scheduled. The log says once
`[clip-director] Music Director not found: clips hold`, the dock status reads
`Waiting for Music Director`, and Clip Director logs `following Music Director` when the
clock is found again.

### Stutter

With "Stutter with the drop colour cycle" on, Clip Director alternates the live and the
outgoing clip at Palette Director's drop colour cycle rate. The cycle is read from Palette
Director (`palette_director_drop_cycle`); without Palette Director, stutter stays off.

## Synesthesia and MIDI

Synesthesia scene control and MIDI are no longer part of Clip Director:

- **Synesthesia** scenes are driven by **Scene Director** (playlist Next / Prev over MIDI
  out), on the same section beat as the clip cut.
- **MIDI** is the **Music Director** MIDI Director dock: it maps MIDI notes and CCs to any
  OBS hotkey, including Clip Director's Skip / Hold / Ban.

Clip Director still sends its picture to Synesthesia over Spout; no OSC is used.

## Build from source

The project is an OBS plugin template layout: `buildspec.json` names the plugin, its
version and the dependency versions, and `CMakePresets.json` holds the presets.

| Requirement | Version |
|---|---|
| CMake | 3.28 or newer |
| Windows | Visual Studio 17 2022 (desktop C++), Windows 10 SDK 10.0.20348 or newer |
| Linux (unit tests) | a C++17 compiler, Ninja, pkg-config |

Windows x64:

```
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
```

The configure step downloads the dependencies pinned in `buildspec.json` (OBS Studio
32.2.2 sources, obs-deps, Qt6) into `.deps/` and checks their SHA256 hashes.

Unit tests (libobs-free, run anywhere with CMake):

```
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Only the Windows x64 packages are released; the macOS and Ubuntu presets are kept from
the template for development.

## Licence

GPL-2.0. See [LICENSE](LICENSE).

- Suite, install and changelog: <https://github.com/EnZhm/director>
- Music Director (the beat clock this plugin follows):
  <https://github.com/EnZhm/director>
- Changelog: [CHANGELOG.md](CHANGELOG.md) | Install: [release/INSTALL.txt](release/INSTALL.txt)
