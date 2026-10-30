# Scene Director

Scene Director is the OBS Studio plugin (Windows x64) of the [Director](https://github.com/EnZhm/director)
suite that drives Synesthesia. It follows Music Director's beat clock (`director_clock`) and sends
OSC to Synesthesia, so the Synesthesia playlist switches on the beat: on breakdowns and drops by
itself, and from a dock button or an OBS hotkey by hand. It listens on Synesthesia's OSC output to
confirm each scene change.

| | |
|---|---|
| Version | 2.1.0 |
| Platform | Windows 10/11 x64 |
| OBS Studio | 32.2.x (built against 32.2.2) |
| Needs | Music Director for the beat clock; Synesthesia Pro for OSC |
| Licence | GPL-2.0 |

## What it adds

| Added | Name | Where it goes |
|---|---|---|
| Source | Scene Director | Any scene. It renders nothing (`0 x 0`, no video output): it holds the OSC settings and drives the sends |
| Dock | Scene Director | Clock and section, the OSC link, host and ports, counters, Next / Prev, the breakdown and drop switches, cooldown, Scene lead, the event map and the log |
| Hotkeys | `scene_director_next`, `scene_director_prev` | OBS Settings > Hotkeys: "Scene Director: Next scene" / "Previous scene" |
| File | `event-map.json` | `%APPDATA%\obs-studio\plugin_config\scene-director\`: which OSC actions fire on which musical events |

With Music Director installed, every automatic send lands on Music Director's beat grid: a
breakdown and a drop both land on the section's `section_beat`, the same beat the palette change
and the clip cut land on. Without Music Director, Scene Director idles (no automatic sends, one log
line, the dock reads "Waiting for Music Director"), and the dock buttons and the hotkeys still send.

## Requirements

| Item | Notes |
|---|---|
| Windows 10 or 11, x64 | Windows only. |
| OBS Studio 32.2.x | 32.2.2 at release. |
| Music Director | The beat clock (`director_clock`). In the suite zip. |
| Synesthesia Pro | OSC input and output are Synesthesia Pro features. Tested with Synesthesia 1.25.4.189. |

## Install

Install the suite: download `director-2.1.0-windows-x64.zip` from
[EnZhm/director releases](https://github.com/EnZhm/director/releases) and copy its four plugin
folders into `C:\ProgramData\obs-studio\plugins\`. Then start OBS and check Help > Log Files >
View Current Log for:

```
[scene-director] plugin loaded successfully (version 2.1.0)
[music-director] plugin loaded successfully (version 2.1.0)
```

Step-by-step: [release/INSTALL.txt](release/INSTALL.txt) and the suite's
[INSTALL.txt](https://github.com/EnZhm/director/blob/main/INSTALL.txt).

## How it works

```
DJ mixer -> OBS audio -> Music Director clock -> Scene Director -> OSC (UDP) ------> Synesthesia input (7000)
                                                                <- OSC feedback --- Synesthesia output (9001)
```

1. **Build the playlist in Synesthesia**, with the scenes in the order you want and the playlist's
   own transition.
2. **Scene Director sends `/playlist/next`** when Music Director reports a breakdown and on the
   drop, both landing on the section's `section_beat`. Nothing is sent during a groove. By hand it
   sends `/playlist/next` or `/playlist/previous` from the dock's buttons or the hotkeys.
3. **Synesthesia answers** on its OSC output with the scene it loaded. Scene Director matches that
   against what it sent and counts confirmations, misses, mismatches and external changes.

What fires on which event is set in the [event map](#event-map). The defaults send only
`/playlist/next` and `/playlist/previous`.

## Synesthesia: OSC setup

In Synesthesia, Settings > OSC:

| Synesthesia | Value | Scene Director |
|---|---|---|
| Input | on | |
| Input port | 7000 | Send port (`osc_send_port`) |
| Output | on | |
| Output address | 127.0.0.1 | |
| Output port | 9001 | Feedback port (`osc_feedback_port`) |
| Output controls | not needed | scene feedback arrives without it |

**The output port must not be the input port.** A freshly configured Synesthesia can have its output
pointed at its own input, which hides all feedback. Scene Director refuses equal ports in the dock,
and when a saved setting has them equal it logs
`[scene-director] '<source>': feedback port <p> equals the send port; set Synesthesia's OSC Output port to a different port (default 9001)`,
shows an amber warning, and keeps sending without feedback.

Synesthesia stores these settings in `Data/preferences.json`: `OSC_INPUT`, `OSC_INPUT_PORT`,
`OSC_OUTPUT`, `OSC_OUTPUT_ADDRESS`, `OSC_OUTPUT_PORT`, `OSC_OUTPUT_AUDIO`, `OSC_OUTPUT_CONTROLS`,
`OSC_OUTPUT_CONTROLS_FORMAT`. Synesthesia's OSC API is "subject to change"; after a Synesthesia
update, check that Next moves the playlist and that the link chip turns green.

## Confirmations

- Feedback is **best-effort**. Synesthesia does not answer every command, so a missed confirmation
  does not mean the scene did not change. Scene Director never resends (a relative Next resent late
  skips two scenes) and never holds a send back waiting for feedback.
- Each scene-changing send arms a confirmation for `confirm_timeout_ms` (default 2000). A
  `/scenes/<name>` from Synesthesia in time confirms it and records the latency:
  `[scene-director] confirm <scene> in <ms> ms`. Otherwise:
  `[scene-director] no confirmation for <route> after <ms> ms`.
- A `/scenes/<name>` send whose answer names another scene logs
  `[scene-director] mismatch: sent <a>, Synesthesia reports <b>`.
- A scene change Scene Director did not send (for example a controller mapped in Synesthesia's
  MIDI panel) logs `[scene-director] external scene change: <scene>`. Scene Director never answers
  it with a send of its own.

## The Scene Director dock

Open it from Docks > "Scene Director". It drives the first Scene Director source in the scene
collection. Its refresh timer also steps the engine, so the automatic sends keep working when the
source is not in the active scene.

| Area | Contents |
|---|---|
| Top | The clock ("Following Music Director's clock" or "Waiting for Music Director") and the section chip (GROOVE / BREAKDOWN / DROP) |
| Link chip | **OSC linked** (green): feedback seen in the last 5 s, or a confirmation. **Sending** (grey): sends go out, no feedback seen. **OSC error** (red): a socket error or a send queue overflow. A missed confirmation never turns it red |
| Host / Send / Feedback | Edit and press **Apply**. A feedback port equal to the send port is not saved. An amber line reports a port conflict or a feedback port in use (sending continues; the port is retried every 5 s) |
| Traffic | Sent, received, dropped |
| Confirm | Last and median latency, confirmed, missed, mismatched, external |
| Next / Previous | Send now. A manual send always fires |
| Next on breakdown / Next on drop, Cooldown | The event map's `breakdown-next` / `drop-next` entries (`enabled`, `min_interval_beats`) |
| Scene lead | The source's `syn_lead_ms`: send this much early to cover Synesthesia's scene load time |
| Event map | The file path, **Reload**, **Open folder** and the last load summary |
| Log | Every send with route, args, beat, reason, seq and the confirmation result (last 200 lines) |

## Event map

`%APPDATA%\obs-studio\plugin_config\scene-director\event-map.json`, shared by every Scene Director
source. It is written with the defaults when missing; after that only the dock's breakdown / drop
switches and cooldown write to it, and they change only those values.

Defaults: four enabled mappings that reproduce 2.0 (`breakdown-next`, `drop-next` on `section_beat`
with Scene lead and a 16-beat cooldown that forced sections bypass; `manual-next`, `manual-prev`
immediate with a 150 ms debounce), plus three disabled examples (`groove-example`,
`drop-scene-example`, `phrase-example`).

Events: `breakdown`, `drop`, `groove`, `section`, `phrase`, `bar`, `manual_next`, `manual_prev`,
`scene_loaded`. Fields: `id`, `event`, `enabled`, `quantize`, `offset_ms`, `lead`,
`min_interval_beats`, `cooldown_group`, `forced_bypass`, `debounce_ms`, `actions` (`route` + `args`,
or `use`). Placeholders: `{section}`, `{scene}`, `{beat}`, `{phrase}`, `{bar}`, `{section_seq}`,
`{bpm}`.

A syntax error keeps the last good map; a bad entry is skipped alone; neither crashes OBS. The full
reference, with ranges, defaults and worked examples, is the
[Event map](https://github.com/EnZhm/director#event-map) section of the suite README.

With several Scene Director sources, a process-wide guard sends each event once, from the first
source that sees it.

## Settings

Stored with the Scene Director source in the scene collection; edit them in the dock or in the
source's properties (right-click the source > Properties).

| Property | Key | Default |
|---|---|---|
| Host | `osc_host` | `127.0.0.1` |
| Send port | `osc_send_port` | 7000 (Synesthesia's OSC Input port) |
| Feedback port | `osc_feedback_port` | 9001 (Synesthesia's OSC Output port; 0 = no feedback; must differ from the send port) |
| Confirmation timeout (ms) | `confirm_timeout_ms` | 2000 (100..10000) |
| Scene lead (ms) | `syn_lead_ms` | 0 (0..500) |
| Sync offset (ms) | `sync_offset_ms` | 0 |

## Upgrading from 2.0

- MIDI out is removed; Synesthesia is driven over OSC. Set up Synesthesia as above. No virtual MIDI
  port (loopMIDI or a Windows MIDI Services loopback) is needed.
- The 2.0 MIDI settings (`midi_out_device`, `next_*`, `prev_*`) stay in the scene collection, unused.
  When a device was set, the log says once per source:
  `[scene-director] '<source>': MIDI out was removed in 2.1; Synesthesia is driven over OSC (<host>:<port>)`.
- `trigger_breakdown`, `trigger_drop` and `cooldown_beats` stay in the source and seed
  `breakdown-next` / `drop-next` when `event-map.json` is created on the first run.
- A source holding Clip Director 1.1's `syn_host` / `syn_send_port` / `syn_feedback_port` takes its
  OSC ports from them and logs `[scene-director] '<source>': OSC ports from Clip Director 1.1 settings: <host> <send>/<feedback>`.
- A controller that drove Next / Prev through MIDI Director is now mapped in Synesthesia's MIDI
  panel (see the suite README, "Manual control").
- Rollback: the Director 2.0.0 zip; the 2.0 settings are still in the scene collection.

## Build from source

The project is an OBS plugin template layout: `buildspec.json` names the plugin, its version and the
dependency versions, and `CMakePresets.json` holds the presets.

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

Unit tests (libobs-free, run anywhere with CMake):

```
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The tests cover the event map (parse, validation, defaults, first-run seeding, a byte fuzz), the
event engine (quantize, offset, lead, cooldown, forced bypass, debounce), the confirmation tracker,
the settings migration and the one-send guard. The cross-plugin tests in `tests/xplugin` check
exactly one Next per section edge and equivalence with 2.0's decisions.

## Licence

GPL-2.0. See [LICENSE](LICENSE).

- Suite, install and changelog: <https://github.com/EnZhm/director>
- Changelog: [CHANGELOG.md](CHANGELOG.md) | Install: [release/INSTALL.txt](release/INSTALL.txt)
