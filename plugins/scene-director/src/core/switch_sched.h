#pragma once
// Scene Director core: beat-synced switch scheduler. Pure C++17, deterministic, no libobs/WinMM.
//
// Music Director reports the section; the scheduler turns the clock and the section into
// "send Next at beat B" decisions, so a playlist switch lands on the beat:
//   - BREAKDOWN starts: Next on the section's landing beat (clk.section_beat), the same beat the
//     palette change and the clip cut land on
//   - DROP downbeat:    Next on the section's landing beat (clk.section_beat)
// A section change is acted on exactly once: when clk.section_seq differs from the sequence the
// scheduler last handled (never on a clk.section comparison, which repeats every poll).
//
// The cooldown applies to AUTOMATIC section edges only. A FORCED section (clk.section_forced:
// Force breakdown / Drop now) bypasses it. Manual Next / Prev always fire, immediately, and
// restart the cooldown.
//
// The source layer sends the message when the clock reaches `send_at_beat`; Scene lead moves
// that beat earlier by lead_ms, so the message leaves a little before the target beat.
#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

namespace cd {

struct SwitchConfig {
	bool trigger_breakdown = true;
	bool trigger_drop = true;
	int cooldown_beats = 16; // an automatic switch this close to the last one is suppressed
	double lead_ms = 0.0;    // 0..500: send the message this early
};

struct SwitchEvent {
	enum class Reason { Breakdown, Drop, ManualNext, ManualPrev };

	Reason reason = Reason::Breakdown;
	double at_beat = 0.0;      // the beat the switch is aimed at
	double send_at_beat = 0.0; // the beat the message actually goes out (lead applied)
	// Set for a section edge (breakdown / drop): the sequence and landing beat it came from, so
	// the source can log "MIDI Next seq=<S> beat=<B>" and the process-wide guard can key on seq.
	bool section_edge = false;
	int64_t section_seq = 0;
	int64_t section_beat = 0;

	const char *reason_name() const;
	bool manual() const { return reason == Reason::ManualNext || reason == Reason::ManualPrev; }
};

class SwitchScheduler {
public:
	explicit SwitchScheduler(const SwitchConfig &cfg = SwitchConfig());

	void set_config(const SwitchConfig &cfg);
	const SwitchConfig &config() const { return cfg_; }

	// Feeds one clock sample. Returns the events whose send time has arrived. A sample with
	// valid == false (Music Director missing) sends nothing.
	std::vector<SwitchEvent> tick(const ClockSample &clk);

	// Re-anchors on a clock resync (first sample, Music Director found again, or an epoch change):
	// the section sequence and the beat phase are re-taken from `clk` and no section edge is
	// reported for it. Pending automatic plans are dropped.
	void resync(const ClockSample &clk);

	// Manual switches: always fire (immediately, at the current beat) and restart the cooldown.
	SwitchEvent manual_next(const ClockSample &clk);
	SwitchEvent manual_prev(const ClockSample &clk);

	Section section() const { return section_; }
	int64_t section_seq() const { return section_seq_; }
	double last_switch_beat() const { return last_switch_beat_; }
	// Text log of every decision (determinism oracle for the tests).
	const std::vector<std::string> &log() const { return log_; }

private:
	struct Pending {
		SwitchEvent::Reason reason = SwitchEvent::Reason::Breakdown;
		double at_beat = 0.0;
		double send_at_beat = 0.0;
		bool section_edge = false;
		int64_t section_seq = 0;
		int64_t section_beat = 0;
		bool forced = false; /* a forced section edge: a manual switch never drops it */
	};

	double lead_beats(double bpm) const;
	// The cooldown window, ignoring whether the edge is forced (used to decide a bypass log).
	bool in_cooldown_window(double at_beat) const;
	// The effective decision: a forced edge never sits in a cooldown.
	bool within_cooldown(double at_beat, bool forced) const;
	void schedule(SwitchEvent::Reason reason, double at_beat, double bpm, const char *why, bool section_edge,
		      int64_t section_seq, int64_t section_beat, bool forced);
	SwitchEvent emit_event(const Pending &p);
	SwitchEvent manual(SwitchEvent::Reason reason, const ClockSample &clk);
	void note(const std::string &line);

	SwitchConfig cfg_;
	bool started_ = false;
	Section section_ = Section::Groove;
	int64_t section_seq_ = 0;
	double last_beats_ = 0.0;
	double last_switch_beat_ = 0.0;
	bool switched_ = false; // a switch has happened: the cooldown anchor is valid
	std::vector<Pending> pending_;
	std::vector<std::string> log_;
};

} // namespace cd
