#include "switch_sched.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cd {

namespace {

const char *sec_name(Section s)
{
	return s == Section::Breakdown ? "breakdown" : (s == Section::Drop ? "drop" : "groove");
}

// Next bar line (multiple of 4 beats) at or after `beat`.
double next_bar_at_least(double beat)
{
	return std::ceil(beat / 4.0 - 1e-9) * 4.0;
}

// First phrase downbeat at or after `beat`.
double next_phrase_after(double beat, int phrase_beats)
{
	const double pb = static_cast<double>(std::max(1, phrase_beats));
	return std::ceil(beat / pb - 1e-9) * pb;
}

} // namespace

const char *SwitchEvent::reason_name() const
{
	switch (reason) {
	case Reason::Breakdown:
		return "breakdown start";
	case Reason::Drop:
		return "drop downbeat";
	case Reason::GroovePhrase:
		return "groove phrase";
	case Reason::ManualNext:
		return "manual next";
	case Reason::ManualPrev:
		return "manual prev";
	}
	return "?";
}

SwitchScheduler::SwitchScheduler(const SwitchConfig &cfg) : cfg_(cfg) {}

void SwitchScheduler::set_config(const SwitchConfig &cfg)
{
	cfg_ = cfg;
}

void SwitchScheduler::note(const std::string &line)
{
	log_.push_back(line);
}

double SwitchScheduler::lead_beats(double bpm) const
{
	const double lead = std::clamp(cfg_.lead_ms, 0.0, 500.0) / 1000.0;
	return lead * std::max(1.0, bpm) / 60.0;
}

bool SwitchScheduler::within_cooldown(double at_beat) const
{
	if (!switched_ || cfg_.cooldown_beats <= 0)
		return false;
	return at_beat - last_switch_beat_ < static_cast<double>(cfg_.cooldown_beats) - 1e-9;
}

void SwitchScheduler::schedule(SwitchEvent::Reason reason, double at_beat, double bpm, const char *why)
{
	if (within_cooldown(at_beat)) {
		char buf[160];
		std::snprintf(buf, sizeof(buf), "%9.3f suppress %-15s at %.3f (cooldown %.0f beats, last %.3f) | %s",
			      last_beats_, SwitchEvent{reason, at_beat, at_beat}.reason_name(), at_beat,
			      static_cast<double>(cfg_.cooldown_beats), last_switch_beat_, why);
		note(buf);
		return;
	}
	const double send_at = at_beat - lead_beats(bpm);
	for (const Pending &p : pending_) {
		if (p.reason == reason && std::fabs(p.at_beat - at_beat) < 1e-6)
			return; /* already planned */
	}
	pending_.push_back(Pending{reason, at_beat, send_at});
	char buf[160];
	std::snprintf(buf, sizeof(buf), "%9.3f plan     %-15s at %.3f (send %.3f) | %s", last_beats_,
		      SwitchEvent{reason, at_beat, send_at}.reason_name(), at_beat, send_at, why);
	note(buf);
}

SwitchEvent SwitchScheduler::emit_event(const Pending &p)
{
	SwitchEvent e;
	e.reason = p.reason;
	e.at_beat = p.at_beat;
	e.send_at_beat = p.send_at_beat;
	last_switch_beat_ = p.at_beat;
	switched_ = true;
	return e;
}

std::vector<SwitchEvent> SwitchScheduler::tick(const ClockSample &clk)
{
	std::vector<SwitchEvent> out;
	if (!clk.valid)
		return out;

	const double b = clk.beats;
	const double bpm = clk.bpm > 1.0 ? clk.bpm : 128.0;

	if (!started_) {
		started_ = true;
		section_ = clk.section;
		last_beats_ = b;
		char buf[96];
		std::snprintf(buf, sizeof(buf), "%9.3f start    section %s, beat %.3f", b, sec_name(section_), b);
		note(buf);
		return out;
	}

	/* a clock jump (Palette Director reset, big correction): re-plan from here */
	if (b < last_beats_ - 1.0 || b > last_beats_ + 8.0) {
		pending_.clear();
		char buf[112];
		std::snprintf(buf, sizeof(buf), "%9.3f replan   clock jump %.3f -> %.3f", b, last_beats_, b);
		note(buf);
	}
	last_beats_ = b;

	/* section changes: plan the automatic switch */
	if (clk.section != section_) {
		section_ = clk.section;
		if (section_ == Section::Breakdown && cfg_.trigger_breakdown)
			schedule(SwitchEvent::Reason::Breakdown, next_bar_at_least(b), bpm, "breakdown start");
		else if (section_ == Section::Drop && cfg_.trigger_drop)
			schedule(SwitchEvent::Reason::Drop, std::floor(b + 1e-9), bpm, "drop downbeat");
		else if (section_ == Section::Groove && cfg_.trigger_groove_phrase)
			schedule(SwitchEvent::Reason::GroovePhrase, next_phrase_after(b, clk.phrase_beats), bpm,
				 "groove phrase");
	} else if (section_ == Section::Groove && cfg_.trigger_groove_phrase) {
		bool planned = false;
		for (const Pending &p : pending_)
			planned |= p.reason == SwitchEvent::Reason::GroovePhrase;
		if (!planned)
			schedule(SwitchEvent::Reason::GroovePhrase, next_phrase_after(b, clk.phrase_beats), bpm,
				 "groove phrase");
	}

	/* fire the events that are due */
	for (size_t i = 0; i < pending_.size();) {
		if (pending_[i].send_at_beat <= b + 1e-9) {
			const Pending p = pending_[i];
			pending_.erase(pending_.begin() + static_cast<long>(i));
			out.push_back(emit_event(p));
			if (p.reason == SwitchEvent::Reason::GroovePhrase && section_ == Section::Groove &&
			    cfg_.trigger_groove_phrase)
				schedule(SwitchEvent::Reason::GroovePhrase, p.at_beat + std::max(1, clk.phrase_beats),
					 bpm, "groove phrase");
		} else {
			++i;
		}
	}
	return out;
}

SwitchEvent SwitchScheduler::manual(SwitchEvent::Reason reason, const ClockSample &clk)
{
	SwitchEvent e;
	e.reason = reason;
	e.at_beat = clk.beats;
	e.send_at_beat = clk.beats;
	last_switch_beat_ = clk.beats;
	switched_ = true;
	/* restart the cooldown: anything automatic that now falls inside it is dropped */
	const double until = clk.beats + std::max(0, cfg_.cooldown_beats);
	pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
				      [until](const Pending &p) { return p.at_beat < until - 1e-9; }),
		       pending_.end());
	char buf[160];
	std::snprintf(buf, sizeof(buf), "%9.3f manual   %-15s (cooldown restarted, %.0f beats)", clk.beats,
		      e.reason_name(), static_cast<double>(std::max(0, cfg_.cooldown_beats)));
	note(buf);
	return e;
}

SwitchEvent SwitchScheduler::manual_next(const ClockSample &clk)
{
	return manual(SwitchEvent::Reason::ManualNext, clk);
}

SwitchEvent SwitchScheduler::manual_prev(const ClockSample &clk)
{
	return manual(SwitchEvent::Reason::ManualPrev, clk);
}

} // namespace cd
