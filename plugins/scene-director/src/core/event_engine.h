#pragma once
// Scene Director core: the event engine. Pure C++17, no libobs, no sockets.
//
// The engine turns musical events into planned OSC sends. Per tick it takes the clock sample and
// the director-follow edges, plus any queued manual / scene_loaded events, and returns the sends
// whose time has arrived (to be encoded and enqueued by the source). It does NOT depend on
// common/osc: each send carries a route string plus typed args (int32 / float32 / string) in the
// neutral EventArg.
//
// Exactly-once: a (mapping id, event key) pair yields at most one plan per process. The engine
// itself keys section events on section_seq and beat/bar/phrase on (epoch, index) so a repeated
// poll never repeats a plan; across several sources a process-wide EventGuard (event_guard.h) keys
// the sends on (event kind, key) and lets the first source claim each event.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "director-follow.h"
#include "event_map.h"
#include "types.h"

namespace cd {

// A queued event the source hands the engine (manual buttons / hotkeys, scene_loaded feedback).
struct QueuedEvent {
	enum class Kind { ManualNext, ManualPrev, SceneLoaded };

	Kind kind = Kind::ManualNext;
	std::string scene; // scene name for SceneLoaded
};

// A planned send. `event` is the event name that produced it and `key` the exactly-once key (empty
// for manual events, which the guard does not deduplicate).
struct PlannedSend {
	std::string mapping_id;
	std::string event;
	std::string key;
	std::string route;
	std::vector<EventArg> args;
	double target_beat = 0.0;    // the beat the send is aimed at
	double send_at_beats = 0.0;  // the beat the send actually goes out (lead/offset applied)
	bool has_send_at_ns = false; // true when now_ns was provided and a wall-clock deadline was set
	int64_t send_at_ns = 0;      // absolute monotonic ns deadline
};

class EventEngine {
public:
	explicit EventEngine(const EventMap &map, double lead_ms = 0.0);

	void set_map(const EventMap &map);
	void set_lead_ms(double lead_ms);

	// One tick. `now_ns` is the monotonic wall-clock at this tick (used for debounce and the
	// send_at_ns deadline; 0 disables both). Returns the sends whose time has arrived.
	std::vector<PlannedSend> tick(const ClockSample &clk, const director_follow::Edges &edges,
				      const std::vector<QueuedEvent> &queued, int64_t now_ns);

	// Text log of every decision (the determinism oracle for tests, as SwitchScheduler::log()).
	const std::vector<std::string> &log() const { return log_; }

private:
	struct Pending {
		std::string mapping_id;
		std::string event;
		std::string key;
		std::string route;
		std::vector<EventArg> args;
		double target_beat = 0.0;
		double send_at_beats = 0.0;
		Quantize quantize = Quantize::Now; // to drop beat/bar/phrase plans on resync
	};

	struct MappingState {
		double last_fire_beat = 0.0; // cooldown anchor (min_interval_beats)
		bool fired = false;
		int64_t last_fire_ns = 0; // debounce anchor (debounce_ms)
	};

	double lead_beats(double bpm) const;
	double offset_beats(int offset_ms, double bpm) const;
	double next_boundary(Quantize q, double event_beat, const ClockSample &clk) const;
	int64_t bar_of(int64_t beat, const ClockSample &clk) const;
	void note(const std::string &line);
	void plan(const Mapping &m, const std::string &event, const std::string &key, double event_beat,
		  double section_beat, const ClockSample &clk, const std::string &scene, int64_t now_ns, bool forced);
	void expand_placeholders(const std::string &s, const ClockSample &clk, const std::string &scene,
				 std::vector<EventArg> &out) const;
	bool is_placeholder(const std::string &s) const;

	EventMap map_;
	double lead_ms_ = 0.0;
	bool started_ = false;
	int64_t last_epoch_ = 0;
	int64_t last_beat_ = 0;
	int64_t last_phrase_ = 0;
	std::vector<Pending> pending_;
	std::map<std::string, MappingState> state_;
	std::vector<std::string> log_;
};

} // namespace cd
