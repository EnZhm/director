# Director

Director is an OBS Studio plugin suite for VJ sets: it locks a Synesthesia VJ feed to
neon palettes and switches Synesthesia's playlist on the beat of the DJ mix, all from
inside OBS. It ships as three repositories: this one (README, combined install,
changelog) plus
[palette-director](https://github.com/EnZhm/palette-director) and
[scene-director](https://github.com/EnZhm/scene-director).

| | |
|---|---|
| Suite version | Director 1.1.0 = Palette Director 1.2.0 + Scene Director 2.0.0 |
| Platform | Windows 10/11 x64, OBS Studio 32.2.x |
| Licence | GPL-2.0 ([LICENSE](LICENSE)) |

## Signal chain

```
DJ mixer -> OBS audio source -> Palette Director: beat clock, sections, palettes
                            -> Scene Director: MIDI Next / Prev on the beat
                                               -> virtual MIDI port -> Synesthesia playlist
MIDI controller -> MIDI Director -> any OBS hotkey in the suite
```

Palette Director owns the clock. It listens to one OBS audio source and finds the BPM,
bars, phrases, breakdowns and drops itself; everything else follows that clock on the
same beat grid. Scene Director turns that grid into MIDI: on a breakdown and on the drop
downbeat it sends the Next message, so the Synesthesia playlist moves exactly when the
music does.

## The four docks

| Dock | What it does |
|---|---|
| Palette Director | Palette pads, look, palette changes and lock behaviours, plus the decision log. |
| Music | BPM, beat lights, section, level and bass meter, the last 64 beats, and the audio-side controls. |
| Scene Director | The clock and section, the MIDI out link, Next / Prev, the automatic switches, Scene lead, the message editors and the log. |
| MIDI Director | Maps MIDI notes and CCs from a controller to OBS hotkeys across the suite. |

<!--
Screenshots (files in docs/img/, described in docs/img/README.md):
  palette-director.png  Palette Director dock: pads, controls, one-line status
  music.png             Music dock mid-groove: BPM, section, meter, 64-beat graph
  scene-director.png    Scene Director dock: MIDI out open, triggers, log
  midi-director.png     MIDI Director dock with a few mappings
-->

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | Both plugins are built against 32.2.2. |
| Synesthesia (Pro licence) | For the Spout input, and for MIDI input to drive the playlist. |
| A virtual MIDI port | loopMIDI, or a Windows MIDI Services loopback pair; it carries Scene Director's Next / Prev to Synesthesia. |
| A mixer feed into OBS | One OBS audio source carrying the DJ mix; an "Audio Input Capture" works. |
| MIDI controller | Optional. Any device Windows exposes as a MIDI input. |

## Install

1. Close OBS.
2. Download `director-1.1.0-windows-x64.zip` from the
   [releases](https://github.com/EnZhm/director/releases) page and unzip it.
3. Copy the folders `palette-director` and `scene-director` into
   `C:\ProgramData\obs-studio\plugins\` so that these files exist:

   ```
   C:\ProgramData\obs-studio\plugins\palette-director\bin\64bit\palette-director.dll
   C:\ProgramData\obs-studio\plugins\scene-director\bin\64bit\scene-director.dll
   ```

4. Start OBS. Help > Log Files > View Current Log should show:

   ```
   [palette-director] plugin loaded successfully (version 1.2.0)
   [scene-director] plugin loaded successfully (version 2.0.0)
   ```

If either line is missing, check the folder layout above. The step-by-step version is
in [INSTALL.txt](INSTALL.txt), which is also inside the zip.

## Quick start

1. Install the suite zip as above and start OBS.
2. In Synesthesia: set up the Spout input, and make sure it accepts MIDI input (its MIDI
   settings) on the virtual port you will create in step 6.
3. Add the Synesthesia Spout source to a scene. Add the filter "Palette Lock" to it
   (right-click source > Filters > + > Palette Lock), then open the Palette Director
   dock and choose a set. The bundled set is Neon Default.
4. Make the mixer feed an OBS audio source (for example an Audio Input Capture named
   "Mixer") and pick it in the Music dock (Audio source). The BPM, the beat lights and
   the section should follow the music.
5. Add a "Scene Director" source to the scene and leave Clock on "Follow Palette
   Director". The source renders nothing: it holds the set's settings and drives the
   MIDI.
6. Install a virtual MIDI port (loopMIDI, or a Windows MIDI Services loopback pair),
   pick it as Scene Director's MIDI out in the Scene Director dock, enable that port as
   a MIDI input in Synesthesia's MIDI settings, then use Synesthesia's MIDI learn on the
   playlist's "next" control and press Next in the dock; repeat with Prev on "previous"
   and save. On the next breakdown and on the next drop the playlist moves by itself.
7. Optional: open the MIDI Director dock, press Learn, and move a control on the
   controller - a pad for "Scene Director: Next scene" is a good first mapping. Then
   save the set in the Palette Director dock (Set tab > Save).

## Components

| Component | Adds | Docs |
|---|---|---|
| Palette Director 1.2.0 | Filter "Palette Lock"; docks "Palette Director" and "Music"; the beat clock other plugins read | [README](https://github.com/EnZhm/palette-director#readme), [INSTALL.txt](https://github.com/EnZhm/palette-director/blob/master/release/INSTALL.txt) |
| Scene Director 2.0.0 | Source "Scene Director" (renders nothing: it holds the settings and drives the MIDI); dock "Scene Director"; dock "MIDI Director"; the hotkeys "Scene Director: Next scene" / "Previous scene" | [README](https://github.com/EnZhm/scene-director#readme), [INSTALL.txt](https://github.com/EnZhm/scene-director/blob/main/release/INSTALL.txt) |

## Links

- [CHANGELOG.md](CHANGELOG.md) - suite version to component versions
- [INSTALL.txt](INSTALL.txt) - combined install and first-run notes
- [LICENSE](LICENSE) - GPL-2.0
- [Palette Director](https://github.com/EnZhm/palette-director) | [Scene Director](https://github.com/EnZhm/scene-director)
