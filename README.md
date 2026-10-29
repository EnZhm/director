# Director

The music drives the picture. Director is a suite of four OBS Studio plugins. One listens to the DJ mix and keeps the beat clock; the other three follow that clock to change the palette, cut the video clips and move the Synesthesia playlist, all on the same beat.

| | |
|---|---|
| Version | Director 2.0.0, one version for the suite and all four plugins |
| Platform | Windows 10/11 x64, OBS Studio 32.2.x |
| Licence | GPL-2.0 ([LICENSE](LICENSE)) |

## Signal chain

```
DJ mix -> OBS audio source -> Music Director (the clock: BPM, beats, phrases, breakdown / drop)
                                   |
                                   +-> Palette Director -> palette on the picture (Palette Lock filter)
                                   +-> Clip Director    -> video clips (Spout out)
                                   +-> Scene Director   -> MIDI Next / Prev -> Synesthesia playlist
MIDI controller -> MIDI Director (in Music Director) -> any OBS hotkey in the suite
```

On every breakdown and drop, the palette change, the clip cut and the Synesthesia Next land on the same beat: the beat Music Director gives that section.

## The four plugins

| Plugin | Role |
|---|---|
| **Music Director** | The conductor. It analyses the mix and runs the only clock: BPM, beats, phrases, breakdown and drop. It has the Music dock (Tap, Lock BPM, Nudge, Phrase now, Drop now, Force breakdown) and MIDI Director, which maps a controller to any OBS hotkey. |
| **Palette Director** | Its Palette Lock filter recolours the picture with palette sets and changes palettes on the clock. |
| **Clip Director** | Plays folders of clips and cuts them on the clock. Breakdown and drop clips are armed ahead of time, so the cut lands on the section beat. |
| **Scene Director** | Sends Synesthesia playlist Next and Prev over MIDI out on breakdowns, drops, buttons and hotkeys. |

Palette, Clip and Scene Director all **require Music Director**. Without it they idle safely: no crash, one log line, and their dock reads "Waiting for Music Director".

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | All four plugins are built against 32.2.2. |
| A mixer feed into OBS | One OBS audio source carrying the DJ mix; an "Audio Input Capture" works. |
| Synesthesia | Optional. A Spout input for the OBS picture, and MIDI input for the playlist. |
| A virtual MIDI port | For Scene Director: loopMIDI, or a Windows MIDI Services loopback pair. |
| MIDI controller | Optional, for MIDI Director. Any device Windows exposes as a MIDI input. |

## Install

1. Close OBS.
2. **Upgrading from 1.x:** delete the old `palette-director`, `clip-director` and `scene-director` folders from `C:\ProgramData\obs-studio\plugins\`.
3. Download `director-2.0.0-windows-x64.zip` from the [releases](https://github.com/EnZhm/director/releases) page and unzip it. Copy its four folders into `C:\ProgramData\obs-studio\plugins\`, so that these files exist:

   ```
   C:\ProgramData\obs-studio\plugins\music-director\bin\64bit\music-director.dll
   C:\ProgramData\obs-studio\plugins\palette-director\bin\64bit\palette-director.dll
   C:\ProgramData\obs-studio\plugins\clip-director\bin\64bit\clip-director.dll
   C:\ProgramData\obs-studio\plugins\scene-director\bin\64bit\scene-director.dll
   ```

4. Start OBS. Help > Log Files > View Current Log shows `plugin loaded successfully (version 2.0.0)` once for each of `[music-director]`, `[palette-director]`, `[clip-director]` and `[scene-director]`.

The step-by-step version, with checksums, is [INSTALL.txt](INSTALL.txt), which is also inside the zip.

### What carries over from 1.x

- **Palette Lock filters, palettes and sets** load exactly as saved, spectrum palettes included.
- **Audio settings move to Music Director on first load.** It takes the audio source, input gain, kick sensitivity, fallback BPM and phrase length, plus any bound clock hotkeys, from the first enabled Palette Lock filter that has an audio source. The log line `[music-director] migrated audio settings from ...` says which filter it used. If other Palette Lock filters hold different values, the log names them.
- **Clip Director sources** load unchanged (same source id, same settings).
- **MIDI Director mappings** load unchanged in Music Director. Mappings that targeted Clip Director 1.1's retired Synesthesia OSC hotkeys (`cd_syn_*`) have nothing to press now; map those controls to `scene_director_next` / `scene_director_prev`.

## Quick start

1. **Music Director.** In the Music dock, pick the mix as the audio source. The BPM, the beat lights and the section (GROOVE / BREAKDOWN / DROP) follow the music.
2. **Palette Director.** Add the filter "Palette Lock" to the source that carries the picture (right-click the source > Filters > + > Palette Lock). Then open the Palette Director dock and choose a set; the bundled set is Neon Default.
3. **Clip Director.** Add a "Clip Director" source and your clip folders, and tag each folder Groove, Breakdown or Drop. To send the clips to Synesthesia, put a Spout Filter on the source and pick that sender under Media in Synesthesia.
4. **Scene Director.** Add a "Scene Director" source, then follow the Synesthesia steps below.

Try it without music. **Force breakdown** and **Drop now** in the Music dock land the palette change, the clip cut and the Synesthesia Next on the next beat.

## Synesthesia: teach the playlist Next and Prev

1. Create the virtual MIDI port, then pick it in the Scene Director dock (MIDI out > Rescan > the port).
2. In Synesthesia's MIDI settings, enable that port as a **MIDI input**.
3. In Synesthesia's MIDI mapping, MIDI-learn the playlist's **next** control, then press **Next** in the Scene Director dock.
4. Do the same for **previous** with **Prev**, then save Synesthesia's mapping.

Synesthesia needs no beat settings, because Scene Director does the timing. Breakdowns and drops send Next by themselves. Automatic section changes respect a 16-beat cooldown. Forced sections, and Next / Prev from the buttons, the hotkeys or MIDI, always go through.

## MIDI Director: map a controller

1. Docks > **MIDI Director**. Pick the controller as the MIDI input (Rescan if it was plugged in after OBS started).
2. Select the OBS hotkey to drive in the list ("Search hotkeys" filters it). Any hotkey works:
   - Music: `md_drop_now`, `md_force_breakdown`, `md_phrase_now`, `md_tap_tempo`, `md_lock_bpm`, `md_nudge_back`, `md_nudge_fwd`
   - Palette: `PaletteDirector.Next.<filter>`, `PaletteDirector.Prev.<filter>`, `PaletteDirector.Panic.<filter>` and the other Palette Lock hotkeys
   - Clips: `cd_skip`, `cd_hold`, `cd_ban`
   - Synesthesia: `scene_director_next`, `scene_director_prev`
3. Click **Learn** and move the pad, key or knob. Choose **Press / release** or **Toggle**.

Mappings are saved with the scene collection.

## Components

| Plugin | Adds | Docs |
|---|---|---|
| Music Director | The clock (`director_clock`); docks "Music Director" and "MIDI Director" | [README](https://github.com/EnZhm/director/blob/main/plugins/music-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/music-director/CHANGELOG.md) |
| Palette Director | Filter "Palette Lock"; dock "Palette Director" | [README](https://github.com/EnZhm/director/blob/main/plugins/palette-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/palette-director/CHANGELOG.md) |
| Clip Director | Source "Clip Director"; dock "Clip Director" | [README](https://github.com/EnZhm/director/blob/main/plugins/clip-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/clip-director/CHANGELOG.md) |
| Scene Director | Source "Scene Director"; dock "Scene Director" | [README](https://github.com/EnZhm/director/blob/main/plugins/scene-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/scene-director/CHANGELOG.md) |

The followers read the clock through [`include/director-clock.h`](https://github.com/EnZhm/director/blob/main/include/director-clock.h). The full specification is [`docs/spec/director-2.0.0.md`](https://github.com/EnZhm/director/blob/main/docs/spec/director-2.0.0.md).

## Links

- [CHANGELOG.md](CHANGELOG.md)
- [INSTALL.txt](INSTALL.txt): install and first-run notes
- [LICENSE](LICENSE): GPL-2.0
