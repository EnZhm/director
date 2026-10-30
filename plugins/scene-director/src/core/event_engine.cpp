#include "event_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cd {

namespace {

const char *section_name(Section s)
{
	return s == Section::Breakdown ? "breakdown" : (s == Section::Drop ? "drop" : "groove");
}

// Floor division that rounds toward negative infinity (bar index must stay correct for beats that
// fall before phrase_beat after a re-anchor).
int64_t floor_div(int64_t a, int64_t b)
{
	if (b <= 0)
		b = 1;
	const int64_t q = a / b;
	const int64_t r = a % b;
	return r < 0 ? q - 1 : q;
}

bool is_section_event(const std::string &s)
{
	return s == "breakdown" || s == "drop" || s == "groove" || s == "section";
}

} // namespace

EventEngine::EventEngine(const EventMap &map, double lead_ms) : map_(map), lead_ms_(lead_ms) {}

void EventEngine::set_map(const EventMap &map)
{
	map_ = map;
}

void EventEngine::set_lead_ms(double lead_ms)
{
	lead_ms_ = lead_ms;
}

void EventEngine::set_release_horizon_ms(double ms)
{
	horizon_ms_ = std::clamp(ms, 0.0, 500.0);
}

void EventEngine::note(const std::string &line)
{
	// The log is a determinism oracle for tests; in a long set it must not grow without bound.
	constexpr size_t kMaxLog = 8192;
	if (log_.size() >= kMaxLog)
		log_.erase(log_.begin(), log_.begin() + static_cast<long>(kMaxLog / 2));
	log_.push_back(line);
}

double EventEngine::lead_beats(double bpm) const
{
	const double lead = std::clamp(lead_ms_, 0.0, 500.0) / 1000.0;
	return lead * std::max(1.0, bpm) / 60.0;
}

double EventEngine::offset_beats(int offset_ms, double bpm) const
{
	const double off = std::clamp(offset_ms, -500, 500) / 1000.0;
	return off * std::max(1.0, bpm) / 60.0;
}

int64_t EventEngine::bar_of(int64_t beat, const ClockSample &clk) const
{
	// Absolute bar index within the epoch. Bar lines sit on phrase_beat + 4k and phrase lengths
	// are multiples of 4, so the grid offset (phrase_beat mod 4) is stable across phrase edges.
	// Counting from that offset rather than from phrase_beat keeps the index (and the
	// exactly-once key) unique across phrases instead of restarting at 0 on every phrase.
	const int64_t grid = clk.phrase_beat - 4 * floor_div(clk.phrase_beat, 4);
	return floor_div(beat - grid, 4);
}

double EventEngine::next_boundary(Quantize q, double event_beat, const ClockSample &clk) const
{
	switch (q) {
	case Quantize::Beat:
		return std::ceil(event_beat - 1e-9);
	case Quantize::Bar: {
		const double pb = static_cast<double>(clk.phrase_beat);
		const double k = std::ceil((event_beat - pb) / 4.0 - 1e-9);
		return pb + 4.0 * k;
	}
	case Quantize::Phrase: {
		const double pb = static_cast<double>(clk.phrase_beat);
		const double len = clk.phrase_beats > 0 ? static_cast<double>(clk.phrase_beats) : 32.0;
		const double k = std::ceil((event_beat - pb) / len - 1e-9);
		return pb + len * k;
	}
	case Quantize::Now:
	case Quantize::SectionBeat:
		return event_beat;
	}
	return event_beat;
}

bool EventEngine::is_placeholder(const std::string &s) const
{
	return s == "{section}" || s == "{scene}" || s == "{beat}" || s == "{phrase}" || s == "{bar}" ||
	       s == "{section_seq}" || s == "{bpm}";
}

void EventEngine::expand_placeholders(const std::string &s, const ClockSample &clk, const std::string &scene,
				      std::vector<EventArg> &out) const
{
	EventArg a;
	if (s == "{section}") {
		a.type = EventArg::Type::String;
		a.s = section_name(clk.section);
	} else if (s == "{scene}") {
		a.type = EventArg::Type::String;
		a.s = scene;
	} else if (s == "{beat}") {
		a.type = EventArg::Type::Int;
		a.i = static_cast<int32_t>(clk.beat);
	} else if (s == "{phrase}") {
		a.type = EventArg::Type::Int;
		a.i = static_cast<int32_t>(clk.phrase);
	} else if (s == "{bar}") {
		a.type = EventArg::Type::Int;
		a.i = static_cast<int32_t>(bar_of(clk.beat, clk));
	} else if (s == "{section_seq}") {
		a.type = EventArg::Type::Int;
		a.i = static_cast<int32_t>(clk.section_seq);
	} else if (s == "{bpm}") {
		a.type = EventArg::Type::Float;
		a.f = static_cast<float>(clk.bpm);
	} else {
		a.type = EventArg::Type::String;
		a.s = s; // not a known placeholder: keep the literal
	}
	out.push_back(std::move(a));
}

void EventEngine::plan(const Mapping &m, const std::string &event, const std::string &key, double event_beat,
		       double section_beat, const ClockSample &clk, const std::string &scene, int64_t now_ns,
		       bool forced)
{
	const double bpm = clk.bpm > 1.0 ? clk.bpm : 128.0;
	const double b = clk.beats;

	// Aiming: section_beat quantizes at the landing beat, now at the current beat, everything else
	// at the next boundary at or after the event.
	double target = event_beat;
	if (m.quantize == Quantize::SectionBeat)
		target = section_beat;
	else if (m.quantize == Quantize::Now)
		target = b;
	else
		target = next_boundary(m.quantize, event_beat, clk);

	MappingState &st = state_[m.id];
	const std::string group = m.cooldown_group.empty() ? m.id : m.cooldown_group;
	CooldownState &cs = cooldown_[group];

	// Cooldown (min_interval_beats): automatic section edges only, forced edges bypass when
	// forced_bypass is set. The anchor is shared by every mapping in the same cooldown_group.
	if (is_section_event(m.event) && m.min_interval_beats > 0 && cs.fired) {
		const double since = target - cs.last_fire_beat;
		if (since < static_cast<double>(m.min_interval_beats) - 1e-9) {
			if (forced && m.forced_bypass) {
				char buf[192];
				std::snprintf(buf, sizeof(buf),
					      "%9.3f bypass   %-15s (forced: cooldown %d beats skipped)", b,
					      m.id.c_str(), m.min_interval_beats);
				note(buf);
			} else {
				char buf[192];
				std::snprintf(buf, sizeof(buf), "%9.3f suppress %-15s (cooldown %d beats)", b,
					      m.id.c_str(), m.min_interval_beats);
				note(buf);
				return;
			}
		}
	}

	// Debounce: wall-clock minimum between fires, any event type.
	if (m.debounce_ms > 0 && now_ns > 0 && st.last_fire_ns != 0 &&
	    now_ns - st.last_fire_ns < static_cast<int64_t>(m.debounce_ms) * 1000000) {
		char buf[160];
		std::snprintf(buf, sizeof(buf), "%9.3f suppress %-15s (debounce %d ms)", b, m.id.c_str(),
			      m.debounce_ms);
		note(buf);
		return;
	}

	double send_at = target;
	if (m.lead)
		send_at -= lead_beats(bpm);
	send_at += offset_beats(m.offset_ms, bpm);

	for (const EventAction &action : m.actions) {
		std::vector<EventArg> args;
		for (const EventArg &arg : action.args) {
			if (arg.type == EventArg::Type::String && is_placeholder(arg.s))
				expand_placeholders(arg.s, clk, scene, args);
			else
				args.push_back(arg);
		}
		Pending p;
		p.mapping_id = m.id;
		p.cooldown_group = group;
		p.event = event;
		p.key = key;
		p.route = action.route;
		p.args = std::move(args);
		p.target_beat = target;
		p.send_at_beats = send_at;
		p.quantize = m.quantize;
		pending_.push_back(std::move(p));
	}
	char buf[192];
	std::snprintf(buf, sizeof(buf), "%9.3f plan     %-15s %s at %.3f (send %.3f)", b, m.id.c_str(), event.c_str(),
		      target, send_at);
	note(buf);
}

std::vector<PlannedSend> EventEngine::tick(const ClockSample &clk, const director_follow::Edges &edges,
					   const std::vector<QueuedEvent> &queued, int64_t now_ns)
{
	std::vector<PlannedSend> out;
	const double b = clk.beats;

	if (!started_) {
		started_ = true;
		last_epoch_ = clk.epoch;
		last_beat_ = clk.beat;
		last_phrase_ = clk.phrase;
		char buf[128];
		std::snprintf(buf, sizeof(buf), "%9.3f start    section %s seq %lld", b, section_name(clk.section),
			      static_cast<long long>(clk.section_seq));
		note(buf);
		// fall through: a section edge may accompany the first sample only via resync (the
		// follower reports none on the first sample), but manual events still fire below.
	} else if (edges.resync) {
		// Re-anchored grid: drop pending beat/bar/phrase plans, no catch-up edges. Section plans
		// are kept (section_seq is authoritative across epochs).
		pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
					      [](const Pending &p) {
						      return p.quantize == Quantize::Beat ||
							     p.quantize == Quantize::Bar ||
							     p.quantize == Quantize::Phrase;
					      }),
			       pending_.end());
		// The beat grid was re-anchored: an old cooldown anchor is meaningless in the new grid (a
		// reset that lands behind it would suppress every automatic switch until the clock caught
		// up), so it is dropped, as 2.0's SwitchScheduler::resync does.
		cooldown_.clear();
		last_epoch_ = clk.epoch;
		last_beat_ = clk.beat;
		last_phrase_ = clk.phrase;
		char buf[128];
		std::snprintf(buf, sizeof(buf), "%9.3f resync   section %s seq %lld", b, section_name(clk.section),
			      static_cast<long long>(clk.section_seq));
		note(buf);
	}

	// Section edges: the section-name event (breakdown / drop / groove) plus the generic
	// "section" event, both keyed on section_seq.
	if (edges.section) {
		const std::string sec = section_name(clk.section);
		const std::string key = std::to_string(clk.section_seq);
		const double sb = static_cast<double>(clk.section_beat);
		for (const Mapping &m : map_.mappings()) {
			if (!m.enabled)
				continue;
			if (m.event == sec)
				plan(m, sec, key, sb, sb, clk, "", now_ns, clk.section_forced);
			else if (m.event == "section")
				plan(m, "section", key, sb, sb, clk, "", now_ns, clk.section_forced);
		}
	}

	// Phrase edge (suppressed by the follower on resync).
	if (edges.phrase) {
		const double ev_beat = clk.phrase_beat != 0 ? static_cast<double>(clk.phrase_beat)
							    : static_cast<double>(clk.phrase) *
								      (clk.phrase_beats > 0 ? clk.phrase_beats : 32);
		const std::string key = "e" + std::to_string(clk.epoch) + ":p" + std::to_string(clk.phrase);
		for (const Mapping &m : map_.mappings())
			if (m.enabled && m.event == "phrase")
				plan(m, "phrase", key, ev_beat, ev_beat, clk, "", now_ns, false);
	}

	// Bar: a beat edge that lands on phrase_beat + 4k.
	if (edges.beat) {
		const int64_t beat_no = edges.beat_no;
		if ((beat_no - clk.phrase_beat) % 4 == 0) {
			const std::string key =
				"e" + std::to_string(clk.epoch) + ":b" + std::to_string(bar_of(beat_no, clk));
			for (const Mapping &m : map_.mappings())
				if (m.enabled && m.event == "bar")
					plan(m, "bar", key, static_cast<double>(beat_no), static_cast<double>(beat_no),
					     clk, "", now_ns, false);
		}
	}

	// Queued manual / scene_loaded events.
	for (const QueuedEvent &q : queued) {
		if (q.kind == QueuedEvent::Kind::ManualNext || q.kind == QueuedEvent::Kind::ManualPrev) {
			const std::string ev = q.kind == QueuedEvent::Kind::ManualNext ? "manual_next" : "manual_prev";
			for (const Mapping &m : map_.mappings())
				if (m.enabled && m.event == ev)
					plan(m, ev, "", b, b, clk, "", now_ns, false);
		} else {
			// scene_loaded fires once per feedback datagram. Its key is empty: every source has
			// its own feedback port, so it never goes through the process-wide guard.
			for (const Mapping &m : map_.mappings())
				if (m.enabled && m.event == "scene_loaded")
					plan(m, "scene_loaded", "", b, b, clk, q.scene, now_ns, false);
		}
	}

	// Release the plans whose send time has arrived, or is within the release horizon: those carry
	// an absolute deadline so the transport sends them on time instead of on the next frame.
	const double bpm = clk.bpm > 1.0 ? clk.bpm : 128.0;
	const double horizon_beats = horizon_ms_ / 1000.0 * bpm / 60.0;
	for (size_t i = 0; i < pending_.size();) {
		if (pending_[i].send_at_beats <= b + horizon_beats + 1e-9) {
			Pending p = std::move(pending_[i]);
			pending_.erase(pending_.begin() + static_cast<long>(i));

			MappingState &st = state_[p.mapping_id];
			CooldownState &cs = cooldown_[p.cooldown_group];
			cs.last_fire_beat = p.target_beat;
			cs.fired = true;
			if (now_ns > 0)
				st.last_fire_ns = now_ns;

			PlannedSend s;
			s.mapping_id = std::move(p.mapping_id);
			s.event = std::move(p.event);
			s.key = std::move(p.key);
			s.route = std::move(p.route);
			s.args = std::move(p.args);
			s.target_beat = p.target_beat;
			s.send_at_beats = p.send_at_beats;
			if (now_ns > 0) {
				const double ahead_beats = std::max(0.0, p.send_at_beats - b);
				s.has_send_at_ns = true;
				s.send_at_ns =
					now_ns + static_cast<int64_t>(std::llround(ahead_beats * 60.0 / bpm * 1e9));
			}
			char buf[160];
			std::snprintf(buf, sizeof(buf), "%9.3f fire     %-15s %s at %.3f", b, s.mapping_id.c_str(),
				      s.event.c_str(), s.target_beat);
			note(buf);
			out.push_back(std::move(s));
		} else {
			++i;
		}
	}
	return out;
}

} // namespace cd
