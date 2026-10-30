# Director

The music drives the picture. Director is a suite of four OBS Studio plugins. One listens to the DJ mix and keeps the beat clock; the other three follow that clock to change the palette, cut the video clips and move Synesthesia, all on the same beat.

| | |
|---|---|
| Version | Director 2.1.0, one version for the suite and all four plugins |
| Platform | Windows 10/11 x64, OBS Studio 32.2.x |
| Licence | GPL-2.0 ([LICENSE](LICENSE)) |

## Signal chain

```
DJ mix -> OBS audio source -> Music Director (the clock: BPM, beats, phrases, breakdown / drop)
                                   |
                                   +-> Palette Director -> palette on the picture (Palette Lock filter, in OBS)
                                   +-> Clip Director    -> video clips (Spout out)
                                   +-> Scene Director   -> OSC out ------> Synesthesia (playlist, scenes)
                                                        <- OSC feedback -- Synesthesia (scene confirmations)
```

On every breakdown and drop, the palette change, the clip cut and the Synesthesia scene switch land on the same beat: the beat Music Director gives that section.

Scene Director is the only plugin that talks to Synesthesia. It sends OSC to Synesthesia's OSC input and listens on Synesthesia's OSC output to confirm each scene change. Colours never go to Synesthesia: Palette Lock recolours the Synesthesia picture inside OBS.

## The four plugins

| Plugin | Role |
|---|---|
| **Music Director** | The conductor. It analyses the mix and runs the only clock: BPM, beats, phrases, breakdown and drop. The Music dock has Tap, Lock BPM, Nudge, Phrase now, Drop now and Force breakdown, and each is also an OBS hotkey. |
| **Palette Director** | Its Palette Lock filter recolours the picture with palette sets and changes palettes on the clock. Each palette can be taken out of the automatic pool. |
| **Clip Director** | Plays folders of clips and cuts them on the clock. Breakdown and drop clips are armed ahead of time, so the cut lands on the section beat. |
| **Scene Director** | Drives Synesthesia over OSC: playlist Next on breakdowns and drops, Next / Prev from buttons and hotkeys, and any OSC action the event map names. It confirms scene changes from Synesthesia's OSC feedback. |

Palette, Clip and Scene Director all **require Music Director**. Without it they idle safely: no crash, one log line, and their dock reads "Waiting for Music Director".

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | All four plugins are built against 32.2.2. |
| A mixer feed into OBS | One OBS audio source carrying the DJ mix; an "Audio Input Capture" works. |
| Synesthesia Pro | For Scene Director. OSC is a Synesthesia Pro feature. Tested with Synesthesia 1.25.4.189. A Spout input carries the OBS picture. |
| Keyboard or Stream Deck | Optional, for Drop now and the other Director hotkeys. |
| MIDI controller | Optional. It is mapped in Synesthesia's own MIDI panel, not in Director. |

No virtual MIDI port is needed.

## Install

1. Close OBS.
2. **Upgrading from 1.x:** delete the old `palette-director`, `clip-director` and `scene-director` folders from `C:\ProgramData\obs-studio\plugins\`. **Upgrading from 2.0:** read [Upgrading from 2.0](#upgrading-from-20) first.
3. Download `director-2.1.0-windows-x64.zip` from the [releases](https://github.com/EnZhm/director/releases) page and unzip it. Copy its four folders into `C:\ProgramData\obs-studio\plugins\`, replacing what is there, so that these files exist:

   ```
   C:\ProgramData\obs-studio\plugins\music-director\bin\64bit\music-director.dll
   C:\ProgramData\obs-studio\plugins\palette-director\bin\64bit\palette-director.dll
   C:\ProgramData\obs-studio\plugins\clip-director\bin\64bit\clip-director.dll
   C:\ProgramData\obs-studio\plugins\scene-director\bin\64bit\scene-director.dll
   ```

4. Start OBS. Help > Log Files > View Current Log shows `plugin loaded successfully (version 2.1.0)` once for each of `[music-director]`, `[palette-director]`, `[clip-director]` and `[scene-director]`.

The step-by-step version, with checksums, is [INSTALL.txt](INSTALL.txt), which is also inside the zip.

## Quick start

1. **Music Director.** In the Music dock, pick the mix as the audio source. The BPM, the beat lights and the section (GROOVE / BREAKDOWN / DROP) follow the music.
2. **Palette Director.** Add the filter "Palette Lock" to the source that carries the picture (right-click the source > Filters > + > Palette Lock). Then open the Palette Director dock and choose a set; the bundled set is Neon Default.
3. **Clip Director.** Add a "Clip Director" source and your clip folders, and tag each folder Groove, Breakdown or Drop. To send the clips to Synesthesia, put a Spout Filter on the source and pick that sender under Media in Synesthesia.
4. **Scene Director.** Add a "Scene Director" source, then follow [Synesthesia: OSC setup](#synesthesia-osc-setup).

Try it without music. **Force breakdown** and **Drop now** in the Music dock land the palette change, the clip cut and the Synesthesia Next on the next beat.

## Synesthesia: OSC setup

Scene Director sends to Synesthesia's OSC **input** and listens on Synesthesia's OSC **output**. Both are set in Synesthesia, Settings > OSC. The defaults below match Scene Director's defaults (send port 7000, feedback port 9001).

| Synesthesia setting | Value | Scene Director setting |
|---|---|---|
| Input | on | |
| Input port | `7000` | Send port (`osc_send_port`) |
| Output | on | |
| Output address | `127.0.0.1` (Synesthesia and OBS on the same machine) | Host (`osc_host`) is Synesthesia's address, `127.0.0.1` |
| Output port | `9001` | Feedback port (`osc_feedback_port`) |
| Output controls | not needed | Scene feedback arrives without it |
| Output audio variables | not needed | |

1. In Synesthesia, open Settings > OSC. Turn **Input** on and set the input port to `7000`.
2. Turn **Output** on. Set the output address to `127.0.0.1` and the output port to `9001`.
3. **The output port must not be the input port.** A freshly configured Synesthesia can have its output pointed at its own input port; Synesthesia then sends its feedback into itself, and Scene Director never sees a confirmation. Scene Director refuses equal ports in its dock and warns about them in the log.
4. In OBS, open the Scene Director dock. Host `127.0.0.1`, Send `7000`, Feedback `9001` are the defaults; change them and press **Apply** if Synesthesia uses other ports.
5. Press **Next** in the dock. The Synesthesia playlist moves on, and the link chip turns green ("OSC linked") as soon as Synesthesia's feedback arrives.

Breakdowns and drops then send Next by themselves. Synesthesia needs no beat settings, because Scene Director does the timing. Automatic section changes respect a 16-beat cooldown; forced sections (Force breakdown, Drop now) and Next / Prev from the buttons or the hotkeys always go through.

### The link chip and the port warning

| Dock shows | Meaning |
|---|---|
| **OSC linked** (green) | A feedback datagram from Synesthesia arrived in the last 5 seconds, or a send was confirmed. |
| **Sending** (grey) | Sends go out, but no feedback has been seen. Check Synesthesia's Output setting and port. Sends still work. |
| **OSC error** (red) | A socket error, or the send queue overflowed. |
| Amber "Feedback port equals the send port ..." | The feedback port is the send port. Set Synesthesia's OSC Output port to a different port (default 9001). Scene Director keeps sending, without feedback. |
| Amber "Feedback port N is in use" | Another program holds the feedback port. Scene Director keeps sending and retries the port every 5 seconds. |
| "Confirm last ... median ... confirmed ... missed ... mismatched ... external" | Confirmation latency and counters. |

**Confirmations are best-effort.** Synesthesia does not answer every command, so a missed confirmation does not mean the scene did not change. Scene Director counts the miss and logs it; it never resends and never holds a send back waiting for a confirmation. A scene change Synesthesia reports without a send from Scene Director (for example from a controller mapped in Synesthesia) is counted as **external** and left alone.

### Troubleshooting: Synesthesia's preferences file

Synesthesia keeps its OSC settings in `Data/preferences.json` in its settings folder. With Synesthesia closed, these keys show what it will use:

| Key | Expected |
|---|---|
| `OSC_INPUT` | on (1 / true) |
| `OSC_INPUT_PORT` | the Scene Director send port (7000) |
| `OSC_OUTPUT` | on (1 / true) |
| `OSC_OUTPUT_ADDRESS` | `127.0.0.1` |
| `OSC_OUTPUT_PORT` | the Scene Director feedback port (9001), different from `OSC_INPUT_PORT` |
| `OSC_OUTPUT_AUDIO` | any; not needed |
| `OSC_OUTPUT_CONTROLS` | any; not needed (0 works) |
| `OSC_OUTPUT_CONTROLS_FORMAT` | any |

Synesthesia's OSC API is marked "subject to change" by its makers. Director 2.1.0 is tested against Synesthesia 1.25.4.189; after a Synesthesia update, check that Next moves the playlist and that the link chip turns green.

## Event map

The event map decides which OSC actions Scene Director sends on which musical events. It is one JSON file shared by every Scene Director source:

```
%APPDATA%\obs-studio\plugin_config\scene-director\event-map.json
```

The dock's **Open folder** button opens that folder, **Reload** re-reads the file, and the dock shows the last load summary (`N mappings, M enabled, K rejected`). Scene Director writes the file with the defaults when it is missing and never rewrites it after that, except when the dock's **Next on breakdown**, **Next on drop** or **Cooldown** controls change: they rewrite only `enabled` and `min_interval_beats` of `breakdown-next` and `drop-next`, and keep everything else in the file as it was, unknown keys included.

### The default file

```json
{
  "version": 1,
  "mappings": [
    { "id": "breakdown-next", "event": "breakdown", "enabled": true,
      "quantize": "section_beat", "offset_ms": 0, "lead": true,
      "min_interval_beats": 16, "cooldown_group": "scene", "forced_bypass": true, "debounce_ms": 0,
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "drop-next", "event": "drop", "enabled": true,
      "quantize": "section_beat", "offset_ms": 0, "lead": true,
      "min_interval_beats": 16, "cooldown_group": "scene", "forced_bypass": true, "debounce_ms": 0,
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "manual-next", "event": "manual_next", "enabled": true, "quantize": "now",
      "debounce_ms": 150, "cooldown_group": "scene",
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "manual-prev", "event": "manual_prev", "enabled": true, "quantize": "now",
      "debounce_ms": 150, "cooldown_group": "scene",
      "actions": [ { "route": "/playlist/previous" } ] },
    { "id": "groove-example", "event": "groove", "enabled": false, "quantize": "section_beat",
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "drop-scene-example", "event": "drop", "enabled": false, "quantize": "section_beat", "lead": true,
      "actions": [ { "route": "/scenes/hexarray", "args": ["Peak"] } ] },
    { "id": "phrase-example", "event": "phrase", "enabled": false, "quantize": "phrase",
      "actions": [ { "route": "/controls/global/bang/1", "args": [1.0] } ] }
  ]
}
```

Four mappings are enabled: Next on a breakdown, Next on a drop, manual Next and manual Prev. The three `-example` mappings are disabled; set `"enabled": true` to use one. The defaults send only `/playlist/next` and `/playlist/previous`.

On the first run, when a Scene Director source carries the older `trigger_breakdown`, `trigger_drop` or `cooldown_beats` settings, those values go into `breakdown-next` and `drop-next`.

### Events

| Event | When it fires |
|---|---|
| `breakdown`, `drop`, `groove` | A section change into that section, natural or forced. Once per section change. |
| `section` | Any section change. |
| `phrase` | Every phrase edge. |
| `bar` | Every bar line (bars start on the phrase's first beat). |
| `manual_next`, `manual_prev` | The dock's Next / Prev buttons and the hotkeys `scene_director_next` / `scene_director_prev`. |
| `scene_loaded` | Synesthesia reports a scene change on its OSC output. |

With several Scene Director sources, each event is sent once, by the first source that sees it.

### Fields

| Field | Values | Default |
|---|---|---|
| `id` | Unique, non-empty string. | required |
| `event` | One of the events above. | required |
| `enabled` | `true` / `false` | `true` |
| `quantize` | `section_beat` (section events only: the section's landing beat), `now`, `beat`, `bar`, `phrase` (the next boundary at or after the event) | `section_beat` for section events, `now` otherwise |
| `offset_ms` | -500..500, added to the target time (negative = earlier) | 0 |
| `lead` | `true` subtracts the source's Scene lead (`syn_lead_ms`) | `true` for section events, `false` otherwise |
| `min_interval_beats` | 0..4096. Cooldown: an automatic section event this close to the last fire is skipped. Manual events never wait for it. | 0 |
| `cooldown_group` | 1..32 characters of `a-z 0-9 - _`. Mappings in the same group share one cooldown anchor; the defaults use `scene`, so a manual Next also starts the 16-beat cooldown. | the mapping's own `id` |
| `forced_bypass` | `true`: forced sections (Force breakdown, Drop now) ignore the cooldown | `true` |
| `debounce_ms` | 0..5000, a wall-clock minimum between two fires of this mapping, any event | 0 |
| `actions` | 1..16 actions, sent in order | required |

An action is either `{ "route": "...", "args": [...] }` or `{ "use": "<id>" }`, which reuses another mapping's actions (one level; no cycles).

- `route` starts with `/` and uses only `a-z 0-9 / _`. Synesthesia's routes include `/playlist/next`, `/playlist/previous`, `/playlist/position`, `/playlist/select`, `/scenes/<name>`, `/presets`, `/favslots/<n>`, `/controls/<bank>/<name>` and `/controls/global/<type>/<n>`.
- `args`, 0..8 elements: a JSON number is sent as a float, `{"int": N}` as an integer, a string as a string, and a string that is exactly one placeholder is replaced when the action is sent.
- A scene launch names the preset inside the same message: `/scenes/<name>` with `"args": ["Preset"]` (case-sensitive). No args launches the scene's default preset. Scene names are lowercase, without spaces.

### Placeholders

| Placeholder | Sent as | Value |
|---|---|---|
| `{section}` | string | The section name |
| `{scene}` | string | The scene Synesthesia reported (`scene_loaded` only) |
| `{beat}`, `{phrase}`, `{bar}`, `{section_seq}` | integer | Clock values at the event |
| `{bpm}` | float | The current BPM |

### Worked examples

**Launch a scene with a preset on every drop**, instead of the playlist Next. Turn off `drop-next` (or untick **Next on drop** in the dock) and enable a scene launch:

```json
{ "id": "drop-scene", "event": "drop", "enabled": true, "quantize": "section_beat", "lead": true,
  "min_interval_beats": 16, "cooldown_group": "scene",
  "actions": [ { "route": "/scenes/hexarray", "args": ["Peak"] } ] }
```

**Bang a control on every phrase**, on the phrase's first beat:

```json
{ "id": "phrase-bang", "event": "phrase", "enabled": true, "quantize": "phrase",
  "actions": [ { "route": "/controls/global/bang/1", "args": [1.0] } ] }
```

**No Next on breakdowns.** Untick **Next on breakdown** in the dock, or set it in the file and press **Reload**:

```json
{ "id": "breakdown-next", "event": "breakdown", "enabled": false, ... }
```

### A bad file

Scene Director never crashes on a bad event map.

- A JSON syntax error keeps the last good map (the defaults on the first load) and logs `[scene-director] event-map.json: <error> at line L col C; keeping the previous map`. The dock reports that the map was not reloaded.
- A bad mapping is skipped on its own and the rest load: `[scene-director] event-map.json: mapping <index> '<id>': <reason>; skipped`. An unknown event name counts as a bad mapping.
- Unknown keys are kept and ignored.

Colours are not part of the event map: Palette Lock recolours the picture in OBS.

## Palette pool

Each palette in a Palette Lock's set is either in the **automatic pool** or out of it. Out-of-pool palettes are never chosen automatically (phrase rotation, drop picks, the drop colour cycle, Next / Prev palette), but they stay in the set and a pad press still plays them.

- **Take a palette out:** right-click its pad in the Palette Director dock and untick **In automatic pool**, or untick the same checkbox in the palette editor.
- **Dimmed pads** with a small "off" corner mark are out of the pool. The live highlight still shows on a dimmed pad when it is live.
- **Pressing a dimmed pad** makes it live at once, like any pad.
- Changes take effect at the next automatic change. The live palette, a running crossfade and a running drop colour cycle are never interrupted.
- **One palette left** in the pool: automatic changes hold on it. **None left:** automatic changes keep the live palette and the log says `automatic pool is empty; holding '<name>'`.
- If the breakdown palette is out of the pool, a breakdown keeps the current palette.
- The pool is saved per set, with the palettes, in the filter and in the set file.

## Manual control

| Action | How |
|---|---|
| Synesthesia Next / Prev from the dock or a hotkey | Scene Director dock buttons, or the hotkeys `scene_director_next` / `scene_director_prev` (OBS Settings > Hotkeys) |
| Synesthesia from a MIDI controller | Map the controller in Synesthesia's own MIDI panel (below). Scene Director logs the change as `external` and leaves it alone |
| Drop now | Bind `md_drop_now` ("Music Director: Drop now") to a key in OBS Settings > Hotkeys, or to a Stream Deck button with Stream Deck's OBS hotkey action |
| Force breakdown, Tap, Lock BPM, Nudge, palette and clip hotkeys | The same way: keyboard or Stream Deck |

### A controller in Synesthesia's MIDI panel

1. Connect the controller and open Synesthesia's MIDI panel.
2. On the **devices** tab, click **refresh connected devices** and wait until the controller shows as connected.
3. Open the scenes tab. Its playlist controls are **previous**, **next** and **play**. Map **next** to a pad, then **previous** to another, using Synesthesia's MIDI mapping (select the control, press the pad).
4. Press the pads: the playlist moves, and Scene Director's log shows `external scene change: <scene>` without sending anything.

## Upgrading from 2.0

- **Scene Director talks OSC now.** MIDI out and the virtual MIDI port (loopMIDI or a Windows MIDI Services loopback) are no longer used. Set up [Synesthesia: OSC setup](#synesthesia-osc-setup); the old MIDI-learned Next / Prev mappings in Synesthesia can stay or go.
- **Saved Scene Director settings carry over.** Scene lead and sync offset are kept. The breakdown and drop switches and the cooldown move into the event map on the first run. The 2.0 MIDI settings (`midi_out_device`, the Next / Prev messages) are kept in the scene collection, unused; the log says once per source `[scene-director] '<source>': MIDI out was removed in 2.1; Synesthesia is driven over OSC (<host>:<port>)`. A source that still holds Clip Director 1.1 OSC settings (`syn_host`, `syn_send_port`, `syn_feedback_port`) takes its OSC ports from them and logs `OSC ports from Clip Director 1.1 settings`.
- **MIDI Director is removed** from Music Director. Its mappings stay in the scene collection, inactive, and survive saves. On load Music Director logs once per collection:
  `[music-director] MIDI Director was removed in 2.1: <N> mappings kept in the scene collection (inactive), copy at <path>`
  and writes a JSON copy of the mappings to `%APPDATA%\obs-studio\plugin_config\music-director\midi-director-<collection>.json`. Then one line per mapping says where it goes now, for example:
  `[music-director] MIDI Director mapping 'Note 36 ch 1' -> md_drop_now (Drop now) is inactive: bind Drop now in Settings > Hotkeys or on a Stream Deck`
- **Rebind** each control as the log says:

  | 2.0 mapping target | In 2.1 |
  |---|---|
  | `scene_director_next` / `scene_director_prev` | Map the control in Synesthesia's MIDI panel ([steps](#a-controller-in-synesthesias-midi-panel)) |
  | `md_drop_now` | Bind Drop now in OBS Settings > Hotkeys, or on a Stream Deck button (OBS hotkey action) |
  | Other Director hotkeys | Bind them in OBS Settings > Hotkeys, or on a Stream Deck |

- **Palettes, sets, clips and the clock** carry over unchanged. Every palette starts in the automatic pool.
- **Rollback:** close OBS and copy the four folders from `director-2.0.0-windows-x64.zip` back over the 2.1 ones. The 2.0 settings, MIDI Director mappings included, are still in the scene collection, so 2.0.0 picks them up again.

MIDI Director's last code is tag `2.0.0`; see [docs/archive/midi-director.md](docs/archive/midi-director.md).

## Upgrading from 1.x

- **Palette Lock filters, palettes and sets** load exactly as saved, spectrum palettes included.
- **Audio settings move to Music Director on first load.** It takes the audio source, input gain, kick sensitivity, fallback BPM and phrase length, plus any bound clock hotkeys, from the first enabled Palette Lock filter that has an audio source. The log line `[music-director] migrated audio settings from ...` says which filter it used.
- **Clip Director sources** load unchanged (same source id, same settings).

## Components

| Plugin | Adds | Docs |
|---|---|---|
| Music Director | The clock (`director_clock`); dock "Music Director"; the `md_*` hotkeys | [README](https://github.com/EnZhm/director/blob/main/plugins/music-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/music-director/CHANGELOG.md) |
| Palette Director | Filter "Palette Lock"; dock "Palette Director" | [README](https://github.com/EnZhm/director/blob/main/plugins/palette-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/palette-director/CHANGELOG.md) |
| Clip Director | Source "Clip Director"; dock "Clip Director" | [README](https://github.com/EnZhm/director/blob/main/plugins/clip-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/clip-director/CHANGELOG.md) |
| Scene Director | Source "Scene Director"; dock "Scene Director"; `event-map.json` | [README](https://github.com/EnZhm/director/blob/main/plugins/scene-director/README.md), [CHANGELOG](https://github.com/EnZhm/director/blob/main/plugins/scene-director/CHANGELOG.md) |

The followers read the clock through [`include/director-clock.h`](https://github.com/EnZhm/director/blob/main/include/director-clock.h). The full specification is [`docs/spec/director-2.1.0.md`](docs/spec/director-2.1.0.md), on top of [`docs/spec/director-2.0.0.md`](docs/spec/director-2.0.0.md).

## Links

- [CHANGELOG.md](CHANGELOG.md)
- [INSTALL.txt](INSTALL.txt): install and first-run notes
- [LICENSE](LICENSE): GPL-2.0
