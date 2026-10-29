# Scene Director

Scene Director is an OBS Studio plugin (Windows x64) for VJ sets. It follows the
[Palette Director](https://github.com/EnZhm/palette-director) beat clock and sends MIDI
**Next** and **Previous** to a virtual MIDI port, so a Synesthesia playlist switches on the
beat: on breakdowns and drops by itself, and from a dock button or an OBS hotkey by hand.
Part of the [Director](https://github.com/EnZhm/director) suite.

Scene Director 2.0.0 was called **Clip Director** up to 1.1.0. It no longer plays clips: the
clip pipeline and the Synesthesia OSC control are gone, and the plugin has been renamed. The
repository is [github.com/EnZhm/scene-director](https://github.com/EnZhm/scene-director).

| | |
|---|---|
| Version | 2.0.0 |
| Platform | Windows 10/11 x64 |
| OBS Studio | 32.2.x (built against 32.2.2) |
| Needs | Palette Director 1.1 or later for the beat clock |
| Licence | GPL-2.0 |

## What it adds

| Added | Name | Where it goes |
|---|---|---|
| Source | Scene Director | Any scene. It renders nothing (`0 x 0`, no video output): it holds the set's settings and drives the MIDI |
| Dock | Scene Director | Clock and section, the MIDI out link, Next / Prev, the automatic switches, Scene lead, the message editors and the log |
| Dock | MIDI Director | Maps MIDI notes and CCs to OBS hotkeys across the suite |
| Hotkeys | `scene_director_next`, `scene_director_prev` | OBS Settings > Hotkeys: "Scene Director: Next scene" / "Previous scene" |

With Palette Director 1.1 or newer, every automatic switch lands on Palette Director's beat
grid. With an older Palette Director (or none) Scene Director still runs, free-running at a
fixed BPM, so the dock buttons and the hotkeys keep working.

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | 32.2.2 at release. |
| Palette Director 1.1+ | The beat clock: `palette_director_clock`. Installed by the suite zip. |
| Synesthesia | For the playlist. It must accept MIDI input for the automatic switching to do anything. |
| A virtual MIDI port | Carries the messages from Scene Director to Synesthesia: loopMIDI, or a Windows MIDI Services loopback pair. |
| MIDI controller | Optional, for the MIDI Director dock. |

## Install

1. Install the suite first: download `director-1.1.0-windows-x64.zip` from
   [EnZhm/director releases](https://github.com/EnZhm/director/releases) and copy both
   folders into `C:\ProgramData\obs-studio\plugins\`. That gets you Scene Director and
   Palette Director in one step, which is the combination that ships.
2. If you only want Scene Director, take `scene-director-2.0.0-windows-x64.zip` from
   [this repo's releases](https://github.com/EnZhm/scene-director/releases) and copy the
   folder `scene-director` into `C:\ProgramData\obs-studio\plugins\`. Palette Director
   1.1+ must already be installed for the clock.
3. If you are upgrading from Clip Director, delete
   `C:\ProgramData\obs-studio\plugins\clip-director` first.
4. Start OBS and check Help > Log Files > View Current Log for:

   ```
   [scene-director] plugin loaded successfully (version 2.0.0)
   [palette-director] plugin loaded successfully (version 1.2.0)
   ```

Step-by-step install, the playlist workflow, troubleshooting and uninstall:
[release/INSTALL.txt](release/INSTALL.txt).

## The playlist workflow

The playlist lives in Synesthesia. Scene Director only ever sends two messages - Next and
Previous - and Synesthesia moves the playlist when it hears them.

```
DJ mixer -> OBS audio -> Palette Director clock -> Scene Director -> MIDI
                                                  -> virtual MIDI port -> Synesthesia playlist
```

1. **Build the playlist in Synesthesia.** Put the scenes you want on the playlist, in the
   order you want them, and set the playlist's own transition as you like.
2. **Scene Director sends Next / Prev.** By itself it sends **Next** when Palette Director
   reports a **breakdown** (on the breakdown crossfade) and on the **drop downbeat**;
   nothing is sent during a groove unless you turn the groove-phrase trigger on. By hand it
   sends **Next** or **Prev** from the dock's buttons, or from the hotkeys
   `scene_director_next` / `scene_director_prev` (a Palette Director or MIDI Director
   mapping can drive those like any other hotkey).
3. **A virtual MIDI port carries it.** Install a loopback port - loopMIDI is the usual one;
   a Windows MIDI Services loopback pair works too - and pick it as Scene Director's MIDI
   out.
4. **Teach Synesthesia the two messages** (four steps):

   1. Create or open the virtual port and pick it as Scene Director's MIDI out (dock >
      MIDI out > Rescan > the port).
   2. In Synesthesia, open its MIDI settings and enable that port as a **MIDI input**.
   3. In Synesthesia's MIDI mapping, enter **MIDI learn** on the playlist's **next**
      control and press **Next** in the Scene Director dock.
   4. Do the same for **previous** with **Prev**, then save Synesthesia's mapping.

After that a breakdown or a drop moves the playlist by itself, and the dock buttons and the
hotkeys let you move it by hand. Nothing in Synesthesia needs to know about beats: Scene
Director does the timing.

## The Scene Director dock

Open it from Docks > "Scene Director".

| Area | Contents |
|---|---|
| Top | The Scene Director source, the clock (`PD` = following Palette Director, `free` = not) and the section chip (GROOVE / BREAKDOWN / DROP) |
| Link | The link chip and the MIDI out device, with a Rescan button. The chip reads `Port open` (green), `Port not found` (red: the device is not connected right now - it is retried quietly) or `No port selected` (grey) |
| Next / Prev | Send the message now. A manual switch always fires, and restarts the cooldown |
| Automatic switches | Breakdown start (on the breakdown crossfade), drop downbeat, and optionally every phrase downbeat during a groove |
| Cooldown (beats) | After a switch, an automatic switch this close to it is held back (default 16). Manual switches always go through |
| Scene lead (ms) | Send the automatic message this much early, to cover Synesthesia's own scene-load time (default 0). It applies to the automatic switches only; the target beat does not move |
| Messages | The Next and Previous messages: type (Note or Control change), channel 1-16, number 0-127. A Note is sent at velocity 127 with its note off 50 ms later; a Control change is sent with value 127 |
| Log | Every switch with its beat, reason and the packed MIDI bytes, one per line |

The dock is one set's control surface and drives the first Scene Director source in the scene
collection. Its refresh timer also steps the scheduler, so the automatic switches keep
working when the source is not in the active scene.

Example log:

```
  128.00  -> NEXT Note ch16 #60  msg 0x7F3C9F  at beat 128.00  [breakdown start]
  160.00  -> NEXT Note ch16 #60  msg 0x7F3C9F  at beat 160.00  [drop downbeat]
  192.00  -> NEXT Note ch16 #60  msg 0x7F3C9F  at beat 192.00  [groove phrase] (early)
  200.50  -> PREV Note ch16 #59  msg 0x7F3B9F  at beat 200.50  [manual prev]
```

## Settings

Everything is stored with the Scene Director source in the scene collection, and can be
edited in the dock or in the source's properties (right-click the source > Properties).

| Property | Key | Default |
|---|---|---|
| Follow Palette Director | `clock_source` | Auto (the Palette Lock on this source, else the first one) |
| Sync offset (ms) | `sync_offset_ms` | 0 |
| Fallback BPM | `fallback_bpm` | 128 |
| MIDI out device | `midi_out_device` | none |
| Next message | `next_type`, `next_channel`, `next_number` | Note, channel 16, number 60 |
| Previous message | `prev_type`, `prev_channel`, `prev_number` | Note, channel 16, number 59 |
| Send Next on a breakdown start | `trigger_breakdown` | on |
| Send Next on the drop downbeat | `trigger_drop` | on |
| Send Next every phrase during a groove | `trigger_groove_phrase` | off |
| Cooldown (beats) | `cooldown_beats` | 16 |
| Scene lead (ms) | `syn_lead_ms` | 0 |

## Upgrading from Clip Director 1.1.0

- **Delete `C:\ProgramData\obs-studio\plugins\clip-director` before copying
  `scene-director`.** The two register the same source id, and two copies of the same source
  is not a state OBS handles well.
- **Existing "Clip Director" sources keep loading.** The old source id is still registered as
  a deprecated alias of Scene Director (it is hidden from the Add menu, but a scene
  collection that holds one loads unchanged). Such a source now renders nothing.
- **Remove any Spout filter that was on it.** The source no longer produces a picture, so a
  Spout filter on it has nothing to send. The Spout input that carries Synesthesia into OBS
  is unaffected.
- **Clip playback is gone**: clip folders, the cut rules, level evening, brightness
  inversion, thumbnails and the ingest tool `tools/cd-ingest.py` are all removed.
- **The Synesthesia OSC tab is gone**, and with it OSC scene control: Auto, the scene pools,
  the link chip over OSC and Import from Synesthesia.
- **MIDI Director mappings to the removed hotkeys** (`cd_skip`, `cd_hold`, `cd_ban`,
  `cd_syn_next_scene`, `cd_syn_hold_scene`, `cd_syn_bang`) are dropped when they are
  triggered; rebind those controls to `scene_director_next` / `scene_director_prev`.
- **Your old settings are kept, not deleted.** A scene collection written by 1.1.0 still
  carries the clip and OSC keys; Scene Director ignores them and leaves them in the file.

## The MIDI Director dock

Open it from Docks > "MIDI Director". Learn mode maps a MIDI note or CC from a controller
to an OBS hotkey in the suite.

| Area | Contents |
|---|---|
| Device | Device picker, a Rescan button and an activity light that flashes on incoming MIDI |
| Learn | Pick the hotkey in the searchable list, choose the mode (press / release or toggle), press Learn to arm it, then move a control; the first press binds it |
| Mapping table | Control, target and mode, with a Remove button per row; saved with the scene collection |
| Monitor | The last MIDI message received, with a counter and a short history |

The list is every hotkey OBS knows about (frontend, source, output, encoder and service),
searchable by description or name. The suite's own hotkeys are in it: Palette Director
(Force off / on, Pause, Rotate colours, PANIC, Next / Previous palette, Palette 1-8 go
live) and its Music clock commands (Phrase is now, Drop now, Force breakdown, Tap tempo,
Lock BPM, Nudge back / forward); Scene Director (Next scene, Previous scene). Bind any of
them to a MIDI note or CC - for example one pad to `scene_director_next` and another to
`scene_director_prev`.

Windows MIDI needs no extra driver or library: both directions go through WinMM. Mappings
are saved with the scene collection.

## Build from source

The project is an OBS plugin template layout: `buildspec.json` names the plugin, its version
and the dependency versions, and `CMakePresets.json` holds the presets.

| Requirement | Version |
|---|---|
| CMake | 3.28 or newer |
| Windows | Visual Studio 17 2022 (desktop C++), Windows 10 SDK 10.0.20348 or newer |
| Linux (unit tests and a development build) | a C++17 compiler, Ninja, pkg-config, libobs-dev, Qt6 |

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

The tests cover the switch scheduler (breakdown and drop triggers, the exact target beats,
cooldown suppression and restart, Scene lead), the MIDI encoder (note on / off, control
change, clamping, DWORD packing), the settings migration from a Clip Director 1.1.0 JSON, and
the MIDI Director map.

Only the Windows x64 packages are released; the macOS and Ubuntu presets are kept from
the template for development.

## Licence

GPL-2.0. See [LICENSE](LICENSE).

- Suite, install and changelog: <https://github.com/EnZhm/director>
- Palette Director (the beat clock this plugin follows):
  <https://github.com/EnZhm/palette-director>
- Changelog: [CHANGELOG.md](CHANGELOG.md) | Install: [release/INSTALL.txt](release/INSTALL.txt)
