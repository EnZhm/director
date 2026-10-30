/*
Palette Director
Copyright (C) 2026 EnZhm

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The Palette Director 2.0.0 follower core: pure C++17, no libobs/Qt.
//
// Music Director is the suite's clock. Palette Director reads it ~every 5 ms
// (director_clock_read(), include/director-clock.h), runs the sample through
// director_follow::Follower (common/director-follow.h) and, on each edge, drives
// pd::Scheduler in Palette Director 1.2.0's order: section, then phrase, then beat,
// then on_tick(beats_now) every poll. advance_pfade() runs once per time step.
//
// The core keeps an event log identical to PD 1.2.0's Director for the same
// section / phrase / beat / tick sequence, so the cross-plugin audit can link it with
// Music Director's core and compare against the 1.2.0 golden.

#include "command_queue.h"
#include "palette.h"

namespace pd {

// ---- Sections / scheduling enums (spec 4.2) ----
enum class Section { Groove, Breakdown, Drop };
enum class ChangeTrigger { Drop, Phrases, Random, Manual };
enum class ChangeOrder { Sequence, Shuffle };

// ---- Config structs (spec 4.2) ----
struct ColourSettings {
	float strength = 1.0f;
	float contrast = 1.1f;
	float keep_brightness = 0.0f;
};

struct PaletteChanges {
	ChangeTrigger trigger = ChangeTrigger::Drop;
	int every_phrases = 4; // Phrases trigger
	float chance = 0.25f;  // Random trigger
	ChangeOrder order = ChangeOrder::Sequence;
	float crossfade_beats = 2.0f;  // 0 = cut
	std::string breakdown_palette; // "" = none; resolved BY NAME
};

struct DirectorConfig {
	int phrase_beats = 32;
	float fade_beats = 1.0f;
	float fallback_bpm = 128.0f;
	bool random_off = true;
	float random_chance = 0.15f;
	int random_bars_min = 1;
	int random_bars_max = 4;
	int cooldown_phrases = 2;
	bool off_on_breakdown = false;
	bool off_on_drop = false;
	int drop_raw_bars = 2;
	float breathing = 0.0f;
	bool rotation = false;
	uint32_t seed = 7;
	// Added beyond spec 4.2 (present in the set schema 5.1 and properties 3.4):
	bool enabled = true;
	// Retired audio keys (spec 4.1): PD no longer uses them but preserves them verbatim.
	float input_gain_db = 0.0f;
	float kick_sensitivity = 0.6f;
	// v1.0.1 drop colour cycle (lock behaviour): on a drop after a breakdown, maybe hard-cut through
	// the normal palettes for a few bars, then land on the palette the drop would have picked.
	bool drop_cycle = false;           // set-file key missing = disabled (old files unchanged)
	float drop_cycle_chance = 0.5f;    // 0..1
	int drop_cycle_bars = 1;           // 1..4
	int drop_cycle_steps_per_beat = 2; // 1 ("1/4"), 2 ("1/8"), 4 ("1/16")
};

struct SetFile {
	std::string name;
	std::string synesthesia_playlist;
	std::vector<PaletteSpec> palettes; // 1..16
	ColourSettings colour;
	PaletteChanges palette_changes;
	DirectorConfig director;
};

// ---- Lock-free snapshot for the render thread (spec 4.2) ----
struct DirectorState {
	float bpm = 0.0f;
	float confidence = 0.0f;
	float bass = 0.0f;
	float mix_target = 1.0f; // scheduler target in [0,1]; the render thread fades toward it
	int rotation_offset = 0;
	Section section = Section::Groove;
	uint64_t beat = 0;
	uint64_t phrase = 0;
	int live_palette = 0;  // index into SetFile::palettes
	int prev_palette = -1; // outgoing palette, -1 if none
	float pfade = 1.0f;    // 0..1 crossfade progress
	int cooldown_phrases_left = 0;
	bool lock_on = true;
	// Derived from the same state (filter + dock).
	float beat_frac = 0.0f; // fractional part of the beat clock (beat lights)
	int next_palette = -1;  // pickNextPreview: next palette the scheduler would pick, -1 if none
	bool paused = false;    // director paused (Pause / Panic)
	bool panic = false;     // PANIC latched (v1.0.1 toggle); implies paused + lock held on
	bool forced_on = false; // Force on toggle
	bool force_off = false; // Force off (hold)
	bool enabled = true;    // DirectorConfig::enabled
	bool silent = true;     // clock feed silent (fallback BPM in use)
	// Audio meter fields are retired (spec 4.1); they keep their defaults so the dock still
	// compiles. Nothing feeds them any more.
	float input_db = -120.0f;
	bool clip = false;
	float bass_level = 0.0f;
	float kick_threshold = 0.4f;
	uint32_t kicks = 0;
	bool drop_cycle = false; // a drop colour cycle is running (lock held on)
	bool bpm_locked = false; // retired with the Music dock; kept for the struct shape
};

// ---- Director decision log (feeds the dock log and the offline tests) ----
struct Event {
	enum class Type { SectionChange, PaletteChange, LockOff, LockOn, Command };
	Type type = Type::Command;
	uint64_t beat = 0;
	uint64_t phrase = 0;
	Section section = Section::Groove; // SectionChange
	int palette = -1;                  // PaletteChange: new live index
	int prev_palette = -1;             // PaletteChange: outgoing index
	std::string detail;                // human-readable reason
	int64_t section_seq = -1;          // SectionChange: Music Director's section_seq (the OBS log line)
};

// One clock sample (a subset of struct director_clock, include/director-clock.h). Filled by the
// worker from director_clock_read(), or by a scripted sequence in tests. Pure, no libobs.
struct ClockSample {
	bool ok = false;
	int64_t beat = 0;
	int64_t phrase = 0;
	int phrase_beats = 32;
	int section = 0; // DIRECTOR_CLOCK_SECTION_*
	int64_t section_seq = 0;
	int64_t section_beat = 0;
	int64_t epoch = 0;
	double beats_now = 0.0; // beat position extrapolated to the call time (drives on_tick)
	double bpm = 0.0;       // fallback tempo the grid runs at
	double confidence = 0.0;
	double bass = 0.0; // 0..1 bass envelope (breathing)
	bool silent = true;
	bool paused = false;
	int64_t now_ns = 0; // os_gettime_ns() at the call (advance_pfade time step)
};

class Scheduler; // implementation detail (scheduler.h/.cpp)

// The Palette Director follower core. Pure C++17, no libobs/Qt.
class PaletteDirector {
public:
	explicit PaletteDirector(const SetFile &);
	~PaletteDirector();
	PaletteDirector(PaletteDirector &&) noexcept;
	PaletteDirector &operator=(PaletteDirector &&) noexcept;
	PaletteDirector(const PaletteDirector &) = delete;
	PaletteDirector &operator=(const PaletteDirector &) = delete;

	// Feed one clock sample. Safe to call from the worker thread ~every 5 ms.
	void on_clock(const ClockSample &);
	DirectorState snapshot() const;
	void push_command(Command cmd, int palette_index = -1);
	void apply_set(const SetFile &);
	const std::vector<Event> &events() const;

	// update_set re-configures a running Director without resetting the clock.
	// live_palette >= 0 also sets the live palette (filter restores a saved live palette).
	void update_set(const SetFile &, int live_palette = -1);
	// Moves the events produced since the last call into `out` (appended).
	void drain_events(std::vector<Event> &out);
	// When false, events() keeps no history (long-running host; use drain_events).
	void set_keep_history(bool keep);

	// true while Music Director is being followed (director_clock_read() succeeded recently).
	bool following() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace pd
