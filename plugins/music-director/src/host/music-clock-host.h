/*
Music Director
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

/*
Music Director is THE clock of the Director suite (a singleton).

  audio thread   : capture callback -> mono downmix -> lock-free SPSC float ring
  worker thread  : ring -> md::MusicClock, drains commands, publishes the clock snapshot about every
                   5 ms, logs every section change
  hotkey/UI      : mdhost::command() -> SPSC command queue drained by the worker
  any thread     : the `director_clock` proc reads the published snapshot (one short mutex)

Settings live in the scene collection under "music_director" (frontend save/load callback). The
worker owns attaching/detaching the audio capture callback (weak source ref), so no other thread
takes a lock of ours.
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <obs.h>

#include "director/clock.h"

namespace mdhost {

// The audio keys Music Director stores (and the migration copies from a Palette Lock filter).
struct Settings {
	std::string audio_source;
	float input_gain_db = 0.0f;
	float kick_sensitivity = 0.6f;
	float fallback_bpm = 128.0f;
	int phrase_beats = 32;
	bool enabled = true;
	std::string migrated_from; // "<filter> on <source>" after a successful migration, else empty

	bool operator==(const Settings &o) const
	{
		return audio_source == o.audio_source && input_gain_db == o.input_gain_db &&
		       kick_sensitivity == o.kick_sensitivity && fallback_bpm == o.fallback_bpm &&
		       phrase_beats == o.phrase_beats && enabled == o.enabled;
	}
	bool operator!=(const Settings &o) const { return !(*this == o); }
};

struct TimelineSample {
	double beat = 0.0;
	float bass = 0.0f;
	int section = 0; // md::Section
};

// Everything the Music dock shows. A snapshot copy, filled on the UI thread.
struct View {
	md::ClockState st;
	double beats = 0.0; // extrapolated beat position right now
	double bpm_now = 0.0;
	std::string audio_name;
	bool audio_attached = false;
	bool has_clock = false; // at least one snapshot published
	Settings settings;
	std::vector<TimelineSample> timeline;
	std::vector<std::string> log; // recent clock lines, oldest first
};

// Commands the hotkeys and the dock send (mapped to md::Command by the worker).
enum class Cmd { PhraseNow, DropNow, ForceBreakdown, TapTempo, LockBpm, NudgeBack, NudgeFwd };

// ---- lifecycle (plugin-main) ----
void load();
void unload();

// ---- commands and settings (UI / hotkey thread) ----
void command(Cmd c);
void set_settings(const Settings &s, const char *why);

// ---- dock view ----
View view();
// Strong reference to the configured audio source (obs_source_release it), or nullptr.
obs_source_t *audio_source_ref();

} // namespace mdhost
