# Director

Director is an OBS Studio plugin suite for VJ sets: it locks a Synesthesia VJ feed to
neon palettes, plays folders of video clips, and switches Synesthesia scenes on the
beat of the DJ mix, all from inside OBS. It ships as three repositories: this one
(README, combined install, changelog) plus
[palette-director](https://github.com/EnZhm/palette-director) and
[clip-director](https://github.com/EnZhm/clip-director).

| | |
|---|---|
| Suite version | Director 1.0.0 = Palette Director 1.2.0 + Clip Director 1.1.0 |
| Platform | Windows 10/11 x64, OBS Studio 32.2.x |
| Licence | GPL-2.0 ([LICENSE](LICENSE)) |

## Signal chain

```
DJ mixer -> OBS audio source -> Palette Director: beat clock, sections, palettes
                            -> Clip Director: clips -> Spout -> Synesthesia
                                              scenes -> OSC -> Synesthesia
MIDI controller -> MIDI Director -> any OBS hotkey in the suite
```

Palette Director owns the clock. It listens to one OBS audio source and finds the BPM,
bars, phrases, breakdowns and drops itself; everything else follows that clock on the
same beat grid.

## The four docks

<!-- TODO(verify): docs/img/*.png screenshots are added at release; see docs/img/README.md -->

| Dock | What it does | Screenshot |
|---|---|---|
| Palette Director | Palette pads, look, palette changes and lock behaviours, plus the decision log. | ![Palette Director dock](docs/img/palette-director.png) |
| Music | BPM, beat lights, section, level and bass meter, the last 64 beats, and the audio-side controls. | ![Music dock](docs/img/music.png) |
| Clip Director | Now playing / next clip, Skip / Hold / Ban, clip folders, the Synesthesia tab and the log. | ![Clip Director dock, Synesthesia tab](docs/img/clip-director.png) |
| MIDI Director | Maps MIDI notes and CCs from a controller to OBS hotkeys across the suite. | ![MIDI Director dock](docs/img/midi-director.png) |

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | Both plugins are built against 32.2.2. |
| Synesthesia (Pro licence) | For the Spout input and for OSC scene control. |
| A mixer feed into OBS | One OBS audio source carrying the DJ mix; an "Audio Input Capture" works. |
| MIDI controller | Optional. Any device Windows exposes as a MIDI input. |
| Python 3 + ffmpeg | Optional, for `cd-ingest.py` (clip preparation in Clip Director). |

## Install

1. Close OBS.
2. Download `director-1.0.0-windows-x64.zip` from the
   [releases](https://github.com/EnZhm/director/releases) page and unzip it.
3. Copy the folders `palette-director` and `clip-director` into
   `C:\ProgramData\obs-studio\plugins\` so that these files exist:

   ```
   C:\ProgramData\obs-studio\plugins\palette-director\bin\64bit\palette-director.dll
   C:\ProgramData\obs-studio\plugins\clip-director\bin\64bit\clip-director.dll
   ```

4. Start OBS. Help > Log Files > View Current Log should show:

   ```
   [palette-director] plugin loaded successfully (version 1.2.0)
   [clip-director] plugin loaded successfully (version 1.1.0)
   ```

If either line is missing, check the folder layout above. The step-by-step version is
in [INSTALL.txt](INSTALL.txt), which is also inside the zip.

## Quick start

1. Install the suite zip as above and start OBS.
2. In Synesthesia (Pro): set up the Spout input under Settings > Syphon/Spout Input, and
   in Settings > OSC set the sending port and the feedback port (for example 7000 and
   7001).
   <!-- TODO(verify): exact Synesthesia menu labels at 1.0 release -->
3. Add the Synesthesia Spout source to a scene. Add the filter "Palette Lock" to it
   (right-click source > Filters > + > Palette Lock), then open the Palette Director
   dock and choose a set. The bundled set is Neon Default.
4. Make the mixer feed an OBS audio source (for example an Audio Input Capture named
   "Mixer") and pick it in the Music dock (Audio source). The BPM, the beat lights and
   the section should follow the music.
5. Add a "Clip Director" source, add your clip folders, leave Clock on
   "Follow Palette Director", and put a Spout Filter on the source (for example named
   `CD-Clips`). In Synesthesia pick that sender under Media.
6. Open the Clip Director dock > Synesthesia tab: host `127.0.0.1`, send and feedback
   ports as in step 2, then fill the Groove / Breakdown / Drop pools ("Import from
   Synesthesia" reads the scene list out of Synesthesia). Scenes then launch on the same
   beat as the clip cut and the drop downbeat.
7. Optional: open the MIDI Director dock, press Learn, and move a control on the
   controller. Then save the set in the Palette Director dock (Set tab > Save).

## Components

| Component | Adds | Docs |
|---|---|---|
| Palette Director 1.2.0 | Filter "Palette Lock"; docks "Palette Director" and "Music"; the beat clock other plugins read | [README](https://github.com/EnZhm/palette-director#readme), [INSTALL.txt](https://github.com/EnZhm/palette-director/blob/master/release/INSTALL.txt) |
| Clip Director 1.1.0 | Source "Clip Director"; dock "Clip Director" with the Synesthesia tab; dock "MIDI Director" | [README](https://github.com/EnZhm/clip-director#readme), [INSTALL.txt](https://github.com/EnZhm/clip-director/blob/main/release/INSTALL.txt) |

## Links

- [CHANGELOG.md](CHANGELOG.md) - suite version to component versions
- [INSTALL.txt](INSTALL.txt) - combined install and first-run notes
- [LICENSE](LICENSE) - GPL-2.0
- [Palette Director](https://github.com/EnZhm/palette-director) | [Clip Director](https://github.com/EnZhm/clip-director)
