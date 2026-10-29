#pragma once
// Scene Director core: beat-synced switch scheduler. Pure C++17, deterministic, no libobs/WinMM.
//
// Palette Director reports the section; the scheduler turns the clock and the section into
// "send Next at beat B" decisions, so a playlist switch lands on the beat:
//   - BREAKDOWN starts: Next at the start of the breakdown crossfade (the next bar line)
//   - DROP downbeat:    Next on the drop downbeat itself
//   - during GROOVE:    optionally Next on every phrase downbeat
// Manual Next / Prev always fire, immediately, and restart the cooldown.
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
	bool trigger_groove_phrase = false;
	int cooldown_beats = 16; // an automatic switch this close to the last one is suppressed
	double lead_ms = 0.0;    // 0..500: send the message this early
};

struct SwitchEvent {
	enum class Reason { Breakdown, Drop, GroovePhrase, ManualNext, ManualPrev };

	Reason reason = Reason::Breakdown;
	double at_beat = 0.0;      // the beat the switch is aimed at
	double send_at_beat = 0.0; // the beat the message actually goes out (lead applied)

	const char *reason_name() const;
	bool manual() const { return reason == Reason::ManualNext || reason == Reason::ManualPrev; }
};

class SwitchScheduler {
public:
	explicit SwitchScheduler(const SwitchConfig &cfg = SwitchConfig());

	void set_config(const SwitchConfig &cfg);
	const SwitchConfig &config() const { return cfg_; }

	// Feeds one clock sample. Returns the events whose send time has arrived.
	std::vector<SwitchEvent> tick(const ClockSample &clk);

	// Manual switches: always fire (immediately, at the current beat) and restart the cooldown.
	SwitchEvent manual_next(const ClockSample &clk);
	SwitchEvent manual_prev(const ClockSample &clk);

	Section section() const { return section_; }
	double last_switch_beat() const { return last_switch_beat_; }
	// Text log of every decision (determinism oracle for the tests).
	const std::vector<std::string> &log() const { return log_; }

private:
	struct Pending {
		SwitchEvent::Reason reason = SwitchEvent::Reason::Breakdown;
		double at_beat = 0.0;
		double send_at_beat = 0.0;
	};

	double lead_beats(double bpm) const;
	bool within_cooldown(double at_beat) const;
	void schedule(SwitchEvent::Reason reason, double at_beat, double bpm, const char *why);
	SwitchEvent emit_event(const Pending &p);
	SwitchEvent manual(SwitchEvent::Reason reason, const ClockSample &clk);
	void note(const std::string &line);

	SwitchConfig cfg_;
	bool started_ = false;
	Section section_ = Section::Groove;
	double last_beats_ = 0.0;
	double last_switch_beat_ = 0.0;
	bool switched_ = false; // a switch has happened: the cooldown anchor is valid
	std::vector<Pending> pending_;
	std::vector<std::string> log_;
};

} // namespace cd
