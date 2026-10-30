#pragma once
// Scene Director: in-process API between the source and the docks (same module).
// Every call takes the source by obs_source_t (the caller holds a strong reference) and is
// thread-safe. Edits are written into the source's settings, so they are saved with the scene
// collection and survive restarts. The shared event map lives outside any one source and is
// edited through reload_map / set_mapping_enabled / set_min_interval.
#include <obs.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sdapi {

struct Status {
	bool clock_following = false; // following Music Director (else idle, waiting for it)
	double bpm = 0.0, beats = 0.0;
	int section = 0; // cd::Section

	// OSC link (spec 4.6)
	int link_state = 0; // cd::LinkState
	std::string osc_host;
	int osc_send_port = 0, osc_feedback_port = 0;
	bool feedback_bound = false; // the feedback socket bound (false when 0 = no feedback, or in use)
	bool port_conflict = false;  // feedback port == send port: running without feedback (spec 4.6)
	uint64_t sent = 0, received = 0, dropped = 0;

	// confirmation counters + latency (spec 4.6)
	int64_t confirmed = 0, missed = 0, mismatched = 0, external = 0;
	int64_t last_latency_ns = -1, median_latency_ns = -1, p90_latency_ns = -1;

	// source config
	double lead_ms = 0.0; // Scene lead (syn_lead_ms)
	int confirm_timeout_ms = 2000;
	double last_switch_beat = 0.0;
	std::string last_reason;
	uint64_t switches = 0;

	// the shared event map
	std::string map_path;
	std::string map_summary;
	struct MappingInfo {
		std::string id;
		std::string event;
		bool enabled = true;
		int min_interval_beats = 0;
	};
	std::vector<MappingInfo> mappings; // every loaded mapping, in file order (the dock's toggles)

	uint64_t log_seq = 0;         // number of log lines ever written
	std::vector<std::string> log; // the newest lines (up to 200), oldest first
};

// True for the Scene Director source id.
bool is_scene_director(obs_source_t *src);
bool status(obs_source_t *src, Status &out);

// The shared event map (spec 4.3): re-read event-map.json from disk, keeping the last good map
// on a syntax error or a missing file (the defaults on first load). Returns false when the file
// could not be loaded and the previous map was kept.
bool reload_map(std::string *error);
// Dock toggles: rewrite only `enabled` / `min_interval_beats` of one mapping, preserving
// everything else (including unknown keys) verbatim. False if the id is not present.
bool set_mapping_enabled(const std::string &id, bool enabled);
bool set_min_interval(const std::string &id, int beats);

// Scene lead (syn_lead_ms), 0..500: the source's own setting (used by the engine for `lead`).
void set_scene_lead(obs_source_t *src, double lead_ms);

/* manual switches: always fire, and restart the cooldown */
void manual_next(obs_source_t *src);
void manual_prev(obs_source_t *src);

// Fallback driver: runs the scheduler on the caller's thread when the source's own video tick has
// not run for a while (the dock calls it from its refresh timer, so the feature keeps working when
// the source sits outside the active scene).
void tick(obs_source_t *src);

} // namespace sdapi
