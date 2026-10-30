# Director 2.1.0 specification

"Music drives everything." At a drop, the Synesthesia scene switch, the clip cut and the palette colours land on the same beat. Scene Director is the only path that controls Synesthesia, and it does so over OSC (Synesthesia Pro). The MIDI Director dock is removed. A user-editable event map decides which OSC actions fire on which musical events.

Current state and the review behind this spec: `docs/director-2.1-control-map.md`. Everything in `docs/spec/director-2.0.0.md` stays in force except where this spec replaces it.

Signal chain: music -> Music Director (the clock) -> palette (Palette Director) / clips (Clip Director, Spout) / Synesthesia (Scene Director, OSC out + OSC feedback in).

## 1. Delivery rules

- Version 2.1.0 (`VERSION` + every `plugins/*/buildspec.json`, `tools/check-version.sh`).
- All 2.1 work merges into the integration branch `d21/integration` (from `main` at 2.0.0). `main`, tags and releases wait for the show-rig gate (G-OSC) and the release GO.
- Card branches are committed locally, swept for identity, then pushed and merged into `d21/integration` by the maintainer.
- Unit and cross-plugin tests run headless on Linux (`plugins/*/tests`, `tests/xplugin`), as in CI. Windows-only code paths compile behind `_WIN32` with a POSIX equivalent that the tests exercise.

## 2. common/osc (shared OSC library)

Promoted from Clip Director 1.1.0 history (`git show 68cf066^:plugins/clip-director/src/core/osc.{h,cpp}` and `src/net/osc-transport.{h,cpp}`), namespace `director_osc`, files `common/osc/osc.{h,cpp}` and `common/osc/osc-transport.{h,cpp}`.

- Codec: OSC 1.0 messages, int32 / float32 / string, 4-byte padding, `osc_encode`, `osc_decode` (rejects short, unterminated, mis-tagged packets), `osc_slug` (Synesthesia scene names: lowercase, no spaces, `_` or `-`).
- Transport: one UDP socket bound to the feedback port and aimed at host:send_port. `send()` only queues; a sender thread does `sendto`. A receiver thread drains the socket into a queue that `pop()` reads without waiting. Queue bound 256 each way; overflow increments `dropped()` and never blocks.
- New: `send_at(datagram, not_before_ns)`. The sender thread holds a datagram until `os_gettime_ns()`-equivalent monotonic time reaches `not_before_ns` (sub-millisecond wake via condition variable timeout), then sends. Datagrams leave in `not_before_ns` order; equal deadlines keep enqueue order.
- Feedback bind failure (port in use) leaves sending working, reports `feedback_bound() == false`, and retries the bind every 5 s.
- `stop()` joins both threads within 250 ms and is safe to call twice, from any non-graphics thread, and from the destructor. Windows keeps `SIO_UDP_CONNRESET` off and WSAStartup/WSACleanup reference-counted.
- Tests (in `tests/xplugin`, so CI picks them up): codec golden bytes for every route in section 5 and the default map; decode rejects; loopback round trip on 127.0.0.1 with ephemeral ports; deadline ordering; queue overflow counts drops; `stop()` under load returns within 250 ms; bind failure path.

## 3. Clock and palette feeds (additive)

### 3.1 Music Director: `phrase_beat`
- `director_clock` gains `out int phrase_beat`: the beat the current phrase started on. `api_version` stays 1 (fields are versioned by name). `include/director-clock.h` reads it into `struct director_clock` and `director_follow::Sample`.
- Bar lines are `phrase_beat + 4k`. A follower that reads an old MD (field absent, reads 0) treats bars as `beat % 4 == 0`.
- Test: MD clock API test asserts `phrase_beat` across a phrase edge, Phrase now (re-anchor) and a 16/32 phrase-length change.

### 3.2 Palette Director: `palette_director_palette`
- New read-only proc on the global proc handler:
  `void palette_director_palette(in string filter, out bool ok, out int api_version, out int seq, out int live, out string name, out int mode, out int n_colours, out string colours, out bool cycling, out string filter_name)`
  - `filter`: `"<source>/<filter>"`, or empty for the first ENABLED Palette Lock in `obs_enum_sources` order then filter order (the rule Music Director's migration uses).
  - `seq`: +1 every time that filter's live palette changes (section, phrase rotation, manual, each drop-cycle step).
  - `colours`: `"r,g,b;r,g,b;..."`, floats 0..1 with 4 decimals, dark -> bright, after `pl_sanitize_for_render()`.
  - `cycling`: the change came from a drop colour cycle step.
- The snapshot is taken under the filter's existing lock; the proc never touches the render path. PD behaviour, settings, set files and logs are unchanged; the PD 1.2.0 equivalence golden still passes byte-identical.
- Tests: a pure `pd::palette_snapshot()` formatter (n_colours 1 and 8, sanitised input, decimals), seq increments on every `go_live` including drop-cycle steps, `cycling` flag.

## 4. Scene Director 2.1

### 4.1 Source settings (`scene_director_source`)
| Key | Default | Notes |
|---|---|---|
| `osc_host` | `127.0.0.1` | Synesthesia machine |
| `osc_send_port` | 7000 | Synesthesia Settings > OSC > Input port |
| `osc_feedback_port` | 7001 | Synesthesia Settings > OSC > Output port; 0 = no feedback |
| `confirm_timeout_ms` | 2000 | 100..10000 |
| `palette_filter` | `""` | `""` = first enabled Palette Lock |
| `syn_lead_ms` | 0 | Scene lead, 0..500 (2.0 key, unchanged) |
| `sync_offset_ms` | 0 | 2.0 key, unchanged |

Migration on load, logged once per source:
- When `osc_host` / `osc_send_port` / `osc_feedback_port` are absent and CD 1.1.0's `syn_host` / `syn_send_port` / `syn_feedback_port` are present, seed from them: `[scene-director] '<src>': OSC ports from Clip Director 1.1 settings: <host> <send>/<feedback>`.
- 2.0 MIDI keys (`midi_out_device`, `next_*`, `prev_*`) are kept verbatim and never read. If `midi_out_device` is non-empty: `[scene-director] '<src>': MIDI out was removed in 2.1; Synesthesia is driven over OSC (<host>:<port>)`.
- `trigger_breakdown`, `trigger_drop`, `cooldown_beats` are kept verbatim. They seed the event map only when `event-map.json` does not exist yet (4.3).

### 4.2 Musical events
| Event | Source | Exactly-once key |
|---|---|---|
| `breakdown`, `drop`, `groove` | section edge into that section (natural or forced) | `section_seq` |
| `section` | any section edge | `section_seq` |
| `phrase` | phrase edge | (`epoch`, phrase) |
| `bar` | beat edge on `phrase_beat + 4k` | (`epoch`, bar index) |
| `palette` | PD feed `seq` changed | PD `seq` |
| `manual_next`, `manual_prev` | dock button, `scene_director_next/prev` hotkey | none (per press; debounce applies) |
| `scene_loaded` | feedback `/scenes/{name}` received | per datagram |

Event context available to templates: `section`, `forced`, `section_seq`, `section_beat`, `beat`, `phrase`, `bar`, `bpm`, palette (`name`, `n`, colours), `scene` (on `scene_loaded`).

### 4.3 Event map (`event-map.json`)
Location: `obs_module_config_path("event-map.json")` (Windows: `%APPDATA%\obs-studio\plugin_config\scene-director\event-map.json`). Shared by every SD source. Written with defaults when missing; never rewritten after that except by the dock's toggles (which change `enabled`, `min_interval_beats` only, preserving everything else verbatim, including unknown keys).

```json
{
  "version": 1,
  "mappings": [
    { "id": "breakdown-next", "event": "breakdown", "enabled": true,
      "quantize": "section_beat", "offset_ms": 0, "lead": true,
      "min_interval_beats": 16, "forced_bypass": true, "debounce_ms": 0,
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "drop-next", "event": "drop", "enabled": true,
      "quantize": "section_beat", "offset_ms": 0, "lead": true,
      "min_interval_beats": 16, "forced_bypass": true, "debounce_ms": 0,
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "manual-next", "event": "manual_next", "enabled": true, "quantize": "now", "debounce_ms": 150,
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "manual-prev", "event": "manual_prev", "enabled": true, "quantize": "now", "debounce_ms": 150,
      "actions": [ { "route": "/playlist/previous" } ] },
    { "id": "palette-colours", "event": "palette", "enabled": true, "quantize": "now",
      "actions": [
        { "route": "/controls/meta/low_color",  "args": ["{palette.color[0]}"] },
        { "route": "/controls/meta/mid_color",  "args": ["{palette.color[mid]}"] },
        { "route": "/controls/meta/high_color", "args": ["{palette.color[-1]}"] } ] },
    { "id": "palette-reassert", "event": "scene_loaded", "enabled": true, "quantize": "now", "offset_ms": 40,
      "actions": [ { "use": "palette-colours" } ] },
    { "id": "groove-example", "event": "groove", "enabled": false, "quantize": "section_beat",
      "actions": [ { "route": "/playlist/next" } ] },
    { "id": "phrase-example", "event": "phrase", "enabled": false, "quantize": "phrase",
      "actions": [ { "route": "/controls/global/bang/1", "args": [1.0] } ] },
    { "id": "bar-example", "event": "bar", "enabled": false, "quantize": "bar",
      "actions": [ { "route": "/controls/meta/hue/random" } ] }
  ]
}
```

The meta colour control names above are placeholders until the show-rig gate reads the real names from Synesthesia's OSC output (G-OSC step 3); the gate updates the shipped defaults.

Fields:
- `id` unique string. `event` one of 4.2. `enabled` bool (default true).
- `quantize`: `section_beat` (section events only: MD's landing beat), `now`, `beat`, `bar`, `phrase` (next boundary at or after the event). Default `section_beat` for section events, `now` otherwise.
- `offset_ms`: -500..500, added to the target time (negative = earlier).
- `lead`: subtract the source's Scene lead (`syn_lead_ms`). Default true for section events, false otherwise.
- `min_interval_beats`: 0..4096, a cooldown between fires of this mapping; automatic section edges only. `forced_bypass` (default true): forced sections ignore it. Manual events never have a cooldown.
- `debounce_ms`: 0..5000, wall-clock minimum between fires of this mapping (any event type).
- `actions`: 1..16 entries. `route` must start with `/` and use only `[a-z0-9/_]` (OSC addresses Synesthesia accepts). `args` 0..8 elements: a JSON number (float32), `{"int": N}` (int32), a JSON string (string literal), or a string that is exactly one placeholder. `{ "use": "<id>" }` reuses another mapping's actions (one level, no cycles).
- Placeholders: `{palette.color[i]}` (3 floats r g b; `i` = index, negative from the end, or `mid` = n/2), `{palette.r[i]}`/`g`/`b` (1 float), `{palette.name}`, `{section}`, `{scene}`, `{beat}`/`{phrase}`/`{bar}`/`{section_seq}` (int32), `{bpm}` (float).
- The defaults reproduce 2.0 exactly: Next on breakdown and drop at `section_beat` minus Scene lead, 16-beat cooldown on automatic edges, forced bypass, manual Next/Prev immediate. (The palette entries are new in 2.1.)
- First-run seeding: when `event-map.json` is missing and an SD source carries 2.0's `trigger_breakdown` / `trigger_drop` / `cooldown_beats`, those values go into `breakdown-next` / `drop-next`.

Validation (on load and on Reload): the parser never throws and never crashes on any input. A syntax error keeps the last good map (defaults on first load) and logs `[scene-director] event-map.json: <error> at line L col C; keeping the previous map`. A bad entry is rejected alone: `[scene-director] event-map.json: mapping <index> '<id>': <reason>; skipped`. Unknown keys are kept and ignored. Loaded maps log a one-line summary (`N mappings, M enabled, K rejected`).

### 4.4 Engine (pure core, `src/core/event_engine.*`)
- Input per tick: the clock sample (+ follower edges), the PD feed snapshot, queued manual events, queued feedback events. Output: planned sends `{mapping_id, event, key, route, args, target_beat, send_at_ns}`.
- A section event with `quantize: section_beat` aims at `section_beat`; the send time is target - lead + offset, converted with the current BPM. A target already in the past sends at once (2.0 behaviour for late edges). A pending section plan survives a manual press (2.0 regression test kept).
- Resync (first sample, MD found again, epoch change) drops pending beat/bar/phrase plans and fires no catch-up edges; section edges keep `section_seq` semantics (director-follow).
- Exactly-once: each (mapping id, event key) produces at most one plan per process. With several SD sources, a process-wide guard keyed by (event kind, key) lets the first source claim each event; the rest skip (generalises 2.0's `SeqGuard`; keys compare lexicographically).
- The engine log is the determinism oracle for tests (as `SwitchScheduler::log()`).

### 4.5 Threads and sends
- Decisions run where 2.0 runs them (`video_tick`, dock timer fallback) under the source mutex. They only encode and enqueue: `director_osc::OscTransport::send_at` with the absolute deadline, enqueued once the send time is within 50 ms. No socket call ever runs on the graphics, audio or UI thread.
- Settings changes that move host/ports restart the transport from `update` (UI thread), never from `video_tick`. `sd_destroy` stops the transport before freeing anything.
- PD feed reads and clock reads stay proc calls (short mutex, never blocking).

### 4.6 Feedback and verification (`src/core/confirm.*`)
- The receiver queue is drained on tick. `/scenes/{name}` -> `scene_loaded` event, plus the confirmation tracker.
- Each scene-changing send (`/playlist/next|previous|position|select`, `/scenes/*`) arms an expectation with deadline `confirm_timeout_ms`. A `/scenes/x` before the deadline confirms it and records the latency (send -> confirm). For `/scenes/{name}` sends the slug must match, else `mismatch`. A deadline that passes logs `no confirmation`. A `/scenes/x` with no expectation is `external` (logged, `scene_loaded` still fires).
- No automatic resend. Relative routes are not idempotent (a late confirm plus a resend skips two scenes), and a resend after the beat is off-beat on stage. Counters and the log make every miss visible.
- Link state: `linked` (feedback datagram seen in the last 5 s, or a confirmation), `sending` (sends ok, no feedback seen), `error` (socket error or send queue overflow).
- Logs: `[scene-director] OSC <route> <args> seq=<S> beat=<B> [<mapping id>]` for each section-triggered send (gate parser compatibility: the 2.0 `MIDI Next seq=S beat=B` line becomes `OSC /playlist/next seq=S beat=B`), `[scene-director] confirm <scene> in <ms> ms` / `no confirmation for <route> after <ms> ms` / `mismatch: sent <a>, Synesthesia reports <b>` / `external scene change: <b>`.

### 4.7 Dock and hotkeys
- Hotkeys unchanged: `scene_director_next` / `scene_director_prev` raise `manual_next` / `manual_prev`.
- Dock: clock and section; OSC link chip (state 4.6); host, send and feedback ports; sent / received / dropped; last and median confirmation latency, misses, mismatches, external changes; Next / Prev; breakdown and drop toggles, cooldown and Scene lead (edit the map entries and the source); map path with Reload and Open folder, and the last load summary; the log (route, args, beat, reason, seq, confirm result). The MIDI device picker and message editors are gone.
- Without Music Director: idle, one log line, manual events still send.

## 5. OSC routes the defaults and tests cover
`/playlist/next`, `/playlist/previous`, `/playlist/position i`, `/playlist/select s`, `/scenes/{name} [s]`, `/presets s`, `/favslots/{n}`, `/controls/meta/{name} f f f`, `/controls/meta/{name}/{r|g|b} f`, `/controls/global/{type}/{n} f`, `/controls/{bank}/{name}/random`.

## 6. Music Director: MIDI Director removed
- Removed: `src/dock/midi-director-dock.*`, the dock registration, `mdhotkey::list` / `mdhotkey::trigger` (subject to open question A), `common/midi/` (MIDI in and out, `midi_map`, `midi_msg`), `winmm` links, `tests/xplugin/test_midi_map.cpp` / `test_midi_msg.cpp`, the locale keys, and the collision-audit entries.
- Kept: md_* hotkeys and their bindings, the Music dock, the clock.
- Settings: the scene collection's `midi_director` value is read on load and written back verbatim on save (OBS rebuilds save data from callbacks, so an unread key would be lost). When it holds mappings, log once per collection: `[music-director] MIDI Director was removed in 2.1: <N> mappings kept in the scene collection (inactive), copy at <path>`, and write the same JSON once to `plugin_config/music-director/midi-director-<collection>.json`.
- OBS's saved dock layout naming `midi-director-dock` is harmless (Qt ignores unknown docks).
- Archive: tag `2.0.0` (cc5bd6b) is the last state with the dock; `docs/archive/midi-director.md` names the tag and the removed paths. An `archive/midi-director` branch at cc5bd6b is created at release time if wanted. No new tag before the release.

## 7. Clip Director and Palette Director
- CD: no change. It stays the clip engine and sends its picture to Synesthesia over Spout; no OSC.
- PD: no behavioural change; the palette feed (3.2) is additive.

## 8. Tests and gates
- Unit: `common/osc` (2), MD `phrase_beat` (3.1), PD palette snapshot (3.2), SD event map parse/validate/defaults/seeding and a byte-fuzz that never crashes, engine quantize/offset/lead/cooldown/forced bypass/debounce, confirmation tracker (confirm, timeout, mismatch, external, latency stats), SD settings migration (syn_* seeding, MIDI keys preserved).
- `tests/xplugin`: (a) the 2.0 scripted clock drives the SD engine with the default map: exactly one `/playlist/next` per breakdown/drop `section_seq`, aimed at `section_beat`, identical decisions to 2.0's `SwitchScheduler` (equivalence); (b) N=3 SD engines behind one guard send each event once; (c) palette seq -> exactly one colour push per seq; (d) PD, CD and SD land on the same `section_beat` for every section edge; (e) the PD 1.2.0 equivalence golden is unchanged.
- Collision audit covers the new proc (`palette_director_palette`), file names and log prefixes.
- **G-OSC (show rig, after GO):** stage the 2.0.0 rollback; install the 2.1 CI suite zip; Synesthesia Settings > OSC input + output on, ports matched; (1) round trip: 10 x `/playlist/next`, all confirmed, median/p90 latency -> Scene lead default; (2) forced breakdown and forced drop: MD = PD = CD = SD `section_beat`, OSC Next confirmed; (3) dump Synesthesia's meta control names from its OSC output and set the colour defaults; colours land and survive the scene load (reassert works); (4) bad `event-map.json` (syntax error, bad entry) loads without a crash and logs the rejects; (5) unplug the network path (block the port) and restore: no crash, link state follows; (6) 12-minute soak, 0 mismatches, 0 dropped; (7) MD with a 2.0 collection that holds `midi_director` mappings: kept verbatim after save, log line seen. On failure, roll back to 2.0.0.
- **REL (after GO):** merge `d21/integration` into `main`, tag `2.1.0`, release the CI zip, optional archive branch.

## 9. Open questions
- A. Manual override hardware once MIDI Director is gone.
- B. SD follows `director_clock` (no own drop detection). Recommended and assumed.
- C. Scene launches with presets: named preset inside `/scenes/{name}`; default map uses the playlist. Recommended and assumed.
- D. Synesthesia-side colours under Palette Lock: Palette Lock recolours the Synesthesia feed in OBS by luminance, so Synesthesia's own colour changes may barely show. Keep the colour push on by default (as ratified) and judge it at G-OSC step 3, or ship it disabled.
