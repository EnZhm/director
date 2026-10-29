#pragma once
// Scene Director: in-process API between the source and the docks (same module).
// Every call takes the source by obs_source_t (the caller holds a strong reference) and is
// thread-safe. Edits are written into the source's settings, so they are saved with the scene
// collection and survive restarts.
#include <obs.h>

#include <cstdint>
#include <string>
#include <vector>

#include "midi_msg.h"
#include "core/switch_sched.h"

namespace sdapi {

struct Status {
	bool clock_following = false; // following Music Director (else idle, waiting for it)
	double bpm = 0.0, beats = 0.0;
	int section = 0; // cd::Section

	// MIDI out
	bool midi_available = false; // this build has a MIDI backend
	bool midi_open = false;      // a port is open
	std::string midi_device;     // the device the settings ask for
	std::string midi_open_name;  // the device currently open
	uint64_t midi_failed = 0;    // name lookups that failed since the last open
	uint64_t midi_sent = 0;

	// scheduler
	bool trigger_breakdown = true, trigger_drop = true, trigger_groove_phrase = false;
	int cooldown_beats = 16;
	double lead_ms = 0.0;
	double last_switch_beat = 0.0;
	std::string last_reason;
	uint64_t switches = 0;

	// the Next / Prev messages
	cd::midi::MsgSpec next, prev;

	uint64_t log_seq = 0;         // number of log lines ever written
	std::vector<std::string> log; // the newest lines (up to 200), oldest first
};

// True for the Scene Director source id.
bool is_scene_director(obs_source_t *src);
bool status(obs_source_t *src, Status &out);

/* settings (saved with the scene collection) */
void set_midi_out(obs_source_t *src, const std::string &device);
void set_messages(obs_source_t *src, const cd::midi::MsgSpec &next, const cd::midi::MsgSpec &prev);
void set_triggers(obs_source_t *src, bool breakdown, bool drop, bool groove_phrase, int cooldown_beats, double lead_ms);

/* manual switches: always fire, and restart the cooldown */
void manual_next(obs_source_t *src);
void manual_prev(obs_source_t *src);

// Fallback driver: runs the scheduler on the caller's thread when the source's own video tick has
// not run for a while (the dock calls it from its refresh timer, so the feature keeps working when
// the source sits outside the active scene).
void tick(obs_source_t *src);

} // namespace sdapi
