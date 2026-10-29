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

} // namespace

const char *SwitchEvent::reason_name() const
{
	switch (reason) {
	case Reason::Breakdown:
		return "breakdown start";
	case Reason::Drop:
		return "drop downbeat";
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

bool SwitchScheduler::in_cooldown_window(double at_beat) const
{
	if (!switched_ || cfg_.cooldown_beats <= 0)
		return false;
	return at_beat - last_switch_beat_ < static_cast<double>(cfg_.cooldown_beats) - 1e-9;
}

bool SwitchScheduler::within_cooldown(double at_beat, bool forced) const
{
	if (forced)
		return false; /* a forced section (Force breakdown / Drop now) bypasses the cooldown */
	return in_cooldown_window(at_beat);
}

void SwitchScheduler::schedule(SwitchEvent::Reason reason, double at_beat, double bpm, const char *why,
			       bool section_edge, int64_t section_seq, int64_t section_beat, bool forced)
{
	if (within_cooldown(at_beat, forced)) {
		char buf[192];
		std::snprintf(buf, sizeof(buf), "%9.3f suppress %-15s at %.3f (cooldown %.0f beats, last %.3f) | %s",
			      last_beats_, SwitchEvent{reason, at_beat, at_beat}.reason_name(), at_beat,
			      static_cast<double>(cfg_.cooldown_beats), last_switch_beat_, why);
		note(buf);
		return;
	}
	if (forced && in_cooldown_window(at_beat)) {
		char buf[192];
		std::snprintf(buf, sizeof(buf),
			      "%9.3f bypass   %-15s at %.3f (forced: cooldown %.0f beats skipped) | %s", last_beats_,
			      SwitchEvent{reason, at_beat, at_beat}.reason_name(), at_beat,
			      static_cast<double>(cfg_.cooldown_beats), why);
		note(buf);
	}
	const double send_at = at_beat - lead_beats(bpm);
	for (const Pending &p : pending_) {
		if (p.reason == reason && std::fabs(p.at_beat - at_beat) < 1e-6)
			return; /* already planned */
	}
	pending_.push_back(Pending{reason, at_beat, send_at, section_edge, section_seq, section_beat, forced});
	char buf[192];
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
	e.section_edge = p.section_edge;
	e.section_seq = p.section_seq;
	e.section_beat = p.section_beat;
	last_switch_beat_ = p.at_beat;
	switched_ = true;
	return e;
}

void SwitchScheduler::resync(const ClockSample &clk)
{
	started_ = true;
	section_ = clk.section;
	section_seq_ = clk.section_seq;
	last_beats_ = clk.beats;
	pending_.clear();
	/* the beat grid was re-anchored: the old switch beat is meaningless in the new grid, so the
	 * cooldown anchor is dropped (otherwise a reset that lands behind it would suppress every
	 * automatic switch until the clock caught up) */
	switched_ = false;
	last_switch_beat_ = clk.beats;
	char buf[112];
	std::snprintf(buf, sizeof(buf), "%9.3f resync   section %s seq %lld, beat %.3f", clk.beats, sec_name(section_),
		      static_cast<long long>(section_seq_), clk.beats);
	note(buf);
}

std::vector<SwitchEvent> SwitchScheduler::tick(const ClockSample &clk)
{
	std::vector<SwitchEvent> out;
	if (!clk.valid)
		return out; /* Music Director missing: the scheduler idles */

	const double b = clk.beats;
	const double bpm = clk.bpm > 1.0 ? clk.bpm : 128.0;

	if (!started_) {
		started_ = true;
		section_ = clk.section;
		section_seq_ = clk.section_seq;
		last_beats_ = b;
		char buf[112];
		std::snprintf(buf, sizeof(buf), "%9.3f start    section %s seq %lld, beat %.3f", b, sec_name(section_),
			      static_cast<long long>(section_seq_), b);
		note(buf);
		return out;
	}

	/* a clock jump (Music Director reset, big correction): re-plan from here */
	if (b < last_beats_ - 1.0 || b > last_beats_ + 8.0) {
		pending_.clear();
		char buf[112];
		std::snprintf(buf, sizeof(buf), "%9.3f replan   clock jump %.3f -> %.3f", b, last_beats_, b);
		note(buf);
	}
	last_beats_ = b;

	/* section changes: act on a change of section_seq, exactly once per sequence */
	if (clk.section_seq != section_seq_) {
		section_ = clk.section;
		section_seq_ = clk.section_seq;
		const int64_t seq = clk.section_seq;
		const int64_t sb = clk.section_beat;
		const bool forced = clk.section_forced;
		if (section_ == Section::Breakdown && cfg_.trigger_breakdown)
			/* landing beat: the beat the section started on, as the palette and clip do */
			schedule(SwitchEvent::Reason::Breakdown, static_cast<double>(sb), bpm, "breakdown start", true,
				 seq, sb, forced);
		else if (section_ == Section::Drop && cfg_.trigger_drop)
			schedule(SwitchEvent::Reason::Drop, static_cast<double>(sb), bpm, "drop downbeat", true, seq,
				 sb, forced);
	}

	/* fire the events that are due */
	for (size_t i = 0; i < pending_.size();) {
		if (pending_[i].send_at_beat <= b + 1e-9) {
			const Pending p = pending_[i];
			pending_.erase(pending_.begin() + static_cast<long>(i));
			out.push_back(emit_event(p));
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
	/* restart the cooldown: anything automatic that now falls inside it is dropped. A manual
	 * switch itself always fires: it bypasses the cooldown by construction. A pending FORCED
	 * section edge (Force breakdown / Drop now) also bypasses it, so it is kept. */
	const double until = clk.beats + std::max(0, cfg_.cooldown_beats);
	pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
				      [until](const Pending &p) { return !p.forced && p.at_beat < until - 1e-9; }),
		       pending_.end());
	char buf[160];
	std::snprintf(buf, sizeof(buf), "%9.3f manual   %-15s (bypass cooldown, restart %.0f beats)", clk.beats,
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
