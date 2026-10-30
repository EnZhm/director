# Director <-> Synesthesia control map (2.0.0 current, 2.1.0 target)

Reviewed on `main` at cc5bd6b (tag 2.0.0), plus Clip Director 1.1.0 in history (`68cf066^`), the 2.0 planning and gate records, and Synesthesia's OSC manual (app.synesthesia.live/docs/manual/osc.html, read 2026-10-30).
The design that follows from this map is `docs/spec/director-2.1.0.md`.

## 1. Signal chain on the show rig (2.0.0)

```
DJ mix -> OBS audio (Mic/Aux) -> Music Director (THE clock: beat, phrase, section, section_seq, section_beat, section_forced, epoch)
   |-> Palette Director : Palette Lock filter on "Tech Apoc Out" (Synesthesia's Spout feed in OBS) recolours the picture in OBS
   |-> Clip Director    : clip source -> Spout filter -> Synesthesia media input (picture only, no control messages)
   |-> Scene Director   : MIDI Note ch16 #60 (Next) / #59 (Prev) -> virtual MIDI port "Director" -> Synesthesia playlist (MIDI-learned)
MIDI controller -> Music Director's MIDI Director dock -> any OBS hotkey (md_*, pd_*, cd_*, scene_director_*)
```

## 2. Every control path that touches Synesthesia today

| # | Path | Where | Transport | Trigger | State |
|---|---|---|---|---|---|
| 1 | Playlist Next on breakdown / drop edge | SD `sd-source.cpp` `drive_locked` -> `send_event` | WinMM MIDI out (`common/midi/director-midi.cpp`), note-off after 50 ms on a worker thread | MD `section_seq` edge via `director_follow::Follower` -> `cd::SwitchScheduler` (lands on `section_beat` minus Scene lead; 16-beat cooldown for automatic edges; forced edges bypass) | Live, gate 3 v3 PASS (bytes `9F 3C 7F`) |
| 2 | Manual Next / Prev | SD dock buttons, hotkeys `scene_director_next` / `scene_director_prev` (first SD source in the collection) | same MIDI out | press | Live |
| 3 | Controller -> any of the above | MD `midi-director-dock.cpp` (MIDI in, learn, `mdhotkey::trigger`) | WinMM MIDI in -> OBS hotkey | controller note / CC | Live in code; never exercised with a real controller |
| 4 | Synesthesia picture -> palette | PD Palette Lock filter on the Synesthesia Spout source | Spout (Synesthesia -> OBS) | PD scheduler on clock edges | Live. Recolours downstream in OBS; Synesthesia's own colours are not touched |
| 5 | Clips -> Synesthesia media | CD source + Spout filter | Spout (OBS -> Synesthesia) | CD decks on `section_beat` | Live. No control messages |
| 6 | Synesthesia OSC (scene pools, `/scenes/{name}`, `/playlist/next`, bang, meta) | CD 1.1.0 `src/core/osc.*`, `src/net/osc-transport.*`, `cd-syn-hotkeys.cpp` (`cd_syn_next_scene`, `cd_syn_hold_scene`, `cd_syn_bang`) | UDP | CD scheduler | Removed in 2.0 (`68cf066`, `b33d941`). History only. The round trip with Synesthesia was never verified on the show rig |

Facts that shape 2.1:
- The CD OSC code and its tests (`test_osc.cpp`, `test_osc_transport.cpp`) exist only in history (`68cf066^`); a checkout of `mono/import` still shows them in the working tree. They are the starting point for `common/osc`: codec (int32/float32/string, 4-byte padding), UDP transport with a sender thread and a receiver thread, bounded queue (256), `SIO_UDP_CONNRESET` handling on Windows, `stop()` joins within ~100 ms.
- The clock proc is `director_clock` (Music Director). `palette_director_clock` no longer exists (removed from PD in 2.0).
- SD decides and sends on `video_tick` (graphics thread) under its source mutex; WinMM `midiOutShortMsg` is the only blocking call. The dock timer drives the same scheduler when the source is outside the active scene.
- PD exports one proc, `palette_director_drop_cycle(out bool active, out int steps_per_beat)`, used by CD's stutter.
- The clock does not carry the phrase anchor (`phrase = (beat + phrase_offset) / phrase_beats` inside MD), so a follower cannot place bar lines on the phrase grid from one sample.
- The show collection's SD source settings still carry CD 1.1.0's OSC keys (`syn_enabled`, `syn_host`, `syn_send_port`, `syn_feedback_port`, `syn_lead_ms`, `syn_pools`, ...). SD's `Settings` store keeps every unknown key verbatim.
- CD 1.1.0 defaulted to send 7000 / feedback 7001; the show-rig setup notes used Synesthesia input 6000 / output 7000. The real values are whatever the show collection holds.
- 2.0 scope decisions (2026-10-29): no scene pools in OBS (the playlist lives in Synesthesia); Synesthesia moves on breakdowns, drops and manual Next/Prev only.
- Palette Lock's gradient map and duotone modes map the input's brightness (Rec. 709 luma by default, or max / average of r g b) onto the palette, so the Synesthesia feed's own hues do not reach the picture.
- Synesthesia OSC: Pro only, "still in development and subject to change".
- Synesthesia OSC output on a scene launch: `/scenes/{sceneName}`, the scene's control values, global control names, dropdown labels. Audio variables stream continuously when enabled. Preset names are not echoed.

## 3. OBS hotkeys in the suite and their relation to Synesthesia

| Hotkey | Owner | Effect on Synesthesia |
|---|---|---|
| `scene_director_next` / `scene_director_prev` | SD | Direct: playlist Next / Prev |
| `md_drop_now` / `md_force_breakdown` | MD | Indirect: forced section edge -> SD Next on the next beat (cooldown bypass) |
| `md_phrase_now`, `md_tap_tempo`, `md_lock_bpm`, `md_nudge_back`, `md_nudge_fwd` | MD | Indirect: re-anchor the grid (`epoch`), shift future edges |
| PD palette hotkeys (per Palette Lock filter) | PD | None on Synesthesia (OBS-side recolour) |
| `cd_skip`, `cd_hold`, `cd_ban` | CD | None (Spout picture only) |
| `cd_syn_next_scene`, `cd_syn_hold_scene`, `cd_syn_bang` | CD 1.1.0 | Retired in 2.0; MIDI Director mappings to them are inert |

## 4. Target (2.1.0)

| # | Path | 2.1.0 |
|---|---|---|
| 1 | Section edges -> Synesthesia | SD over OSC: event map default = `/playlist/next` on breakdown and drop, landing on `section_beat` minus Scene lead; cooldown and forced bypass as 2.0 |
| 2 | Manual Next / Prev | Same hotkeys and dock buttons -> `/playlist/next` / `/playlist/previous` through the event map |
| 3 | Controller | MIDI Director removed. The controller is mapped in Synesthesia's own MIDI panel for Synesthesia actions (SD logs those scene changes as `external`); Drop now (`md_drop_now`) and the other Director hotkeys go on keyboard / Stream Deck |
| 4 | Palette | Unchanged: Palette Lock recolours the Synesthesia feed in OBS on `section_beat`. No colours are pushed into Synesthesia |
| 5 | Clips | Unchanged. CD stays the clip engine, Spout only, no OSC |
| 6 | OSC transport | `common/osc` (promoted from CD 1.1.0 history), used by SD only |
| 7 | MIDI out / loopMIDI | Removed from SD. `common/midi` removed with MIDI Director |
| 8 | Verification | SD listens on Synesthesia's OSC output: confirms each scene change, measures latency, logs mismatches and external changes |
| 9 | Tuning | `event-map.json` in SD's plugin config dir: musical event -> OSC actions, per-event enable, quantize, offset, cooldown, debounce. Defaults send only `/playlist/next` and `/playlist/previous` |

## 5. Decisions

| Item | Decision |
|---|---|
| SD MIDI out | Removed, no deprecated fallback. The rollback is the 2.0.0 release zip, staged on the show rig before the 2.1 gate as in 2.0 |
| CD's Synesthesia hooks | Stay as they are: CD has none on `main`; the Spout picture path needs no control messages |
| Drop detection in SD | None. SD follows `director_clock` exactly as 2.0; one detector keeps the one-landing-beat guarantee |
| Preset at scene launch | Default stays playlist Next (presets ride in Synesthesia's playlist). A mapping that launches a scene names the preset inside the same `/scenes/{name} "Preset"` message; omitted means gold. No `/favslots/{n}` after a launch |
| Colours in Synesthesia | None pushed. Palette Lock in OBS recolours by brightness, so Synesthesia-side colours add nothing to the picture |
| Controller | Synesthesia's MIDI panel for Synesthesia; `md_drop_now` stays an OBS hotkey for keyboard / Stream Deck; rebind documented in INSTALL, README and the migration log |
| MIDI Director archive | Tag `2.0.0` (cc5bd6b) is the archive. `docs/archive/midi-director.md` records the tag, the removed paths and the rebind table. No other tag or branch |
