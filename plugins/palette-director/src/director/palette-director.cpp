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

#include "palette-director.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "director-follow.h"
#include "scheduler.h"

namespace pd {

namespace {
const char *section_name(Section s)
{
	switch (s) {
	case Section::Groove:
		return "GROOVE";
	case Section::Breakdown:
		return "BREAKDOWN";
	case Section::Drop:
		return "DROP";
	}
	return "?";
}

int resolve_breakdown(const SetFile &s)
{
	const std::string &name = s.palette_changes.breakdown_palette;
	if (name.empty())
		return -1;
	for (std::size_t i = 0; i < s.palettes.size(); ++i) {
		if (s.palettes[i].name == name)
			return static_cast<int>(i);
	}
	return -1;
}

SchedulerConfig make_sched_cfg(const SetFile &s)
{
	SchedulerConfig c;
	c.palette_count = static_cast<int>(s.palettes.size());
	c.breakdown_index = resolve_breakdown(s);
	for (const auto &p : s.palettes) {
		c.names.push_back(p.name);
		c.colour_counts.push_back(static_cast<int>(p.colours.size()));
	}
	c.enabled = s.director.enabled;
	c.trigger = s.palette_changes.trigger;
	c.every_phrases = s.palette_changes.every_phrases;
	c.chance = s.palette_changes.chance;
	c.order = s.palette_changes.order;
	c.crossfade_beats = s.palette_changes.crossfade_beats;
	c.random_off = s.director.random_off;
	c.random_chance = s.director.random_chance;
	c.random_bars_min = s.director.random_bars_min;
	c.random_bars_max = s.director.random_bars_max;
	c.cooldown_phrases = s.director.cooldown_phrases;
	c.off_on_breakdown = s.director.off_on_breakdown;
	c.off_on_drop = s.director.off_on_drop;
	c.drop_raw_bars = s.director.drop_raw_bars;
	c.rotation = s.director.rotation;
	c.seed = s.director.seed;
	c.drop_cycle = s.director.drop_cycle;
	c.drop_cycle_chance = s.director.drop_cycle_chance;
	c.drop_cycle_bars = std::max(1, std::min(4, s.director.drop_cycle_bars));
	const int spb = s.director.drop_cycle_steps_per_beat;
	c.drop_cycle_steps_per_beat = (spb == 1 || spb == 4) ? spb : 2;
	return c;
}
} // namespace

struct PaletteDirector::Impl {
	explicit Impl(const SetFile &s) : scheduler_(make_sched_cfg(s)) { apply(s); }

	void apply(const SetFile &s)
	{
		set_ = s;
		breakdown_index_ = resolve_breakdown(s);
		phrase_beats_ = s.director.phrase_beats;
		if (phrase_beats_ < 1)
			phrase_beats_ = 32;

		scheduler_.reset(make_sched_cfg(s));
		md_found_ = false;
		have_clock_ = false;
		last_now_ns_ = 0;
		have_now_ = false;
		last_ = ClockSample{};
		CommandMsg m;
		while (queue_.pop(m)) {
		}
	}

	void update(const SetFile &s, int live)
	{
		set_ = s;
		breakdown_index_ = resolve_breakdown(s);
		phrase_beats_ = s.director.phrase_beats;
		if (phrase_beats_ < 1)
			phrase_beats_ = 32;
		scheduler_.reconfigure(make_sched_cfg(s), live);
	}

	void append_events(const std::vector<Event> &evs)
	{
		for (Event e : evs) {
			e.phrase = (e.beat) / static_cast<uint64_t>(phrase_beats_);
			if (drain_enabled_)
				out_.push_back(e);
			if (keep_history_)
				log_.push_back(std::move(e));
		}
	}

	// One command-trace event (command decisions share this shape).
	void append_note(uint64_t beat, const std::string &detail)
	{
		Event e;
		e.type = Event::Type::Command;
		e.beat = beat;
		e.detail = detail;
		append_events({e});
	}

	void drain_commands(const ClockSample &c)
	{
		const uint64_t beat = c.beat > 0 ? static_cast<uint64_t>(c.beat) : 0;
		CommandMsg m;
		while (queue_.pop(m)) {
			switch (m.cmd) {
			case Command::ForceOff:
				/* handled by the scheduler below */
				break;
			default:
				break;
			}
			auto evs = scheduler_.command(m.cmd, m.palette_index, beat);
			append_events(evs);
		}
	}

	void on_clock(const ClockSample &c)
	{
		have_clock_ = true;
		last_ = c;

		director_follow::Sample s;
		s.ok = c.ok;
		s.beat = c.beat;
		s.phrase = c.phrase;
		s.section = c.section;
		s.section_seq = c.section_seq;
		s.section_beat = c.section_beat;
		s.epoch = c.epoch;

		const director_follow::Edges e = follower_.update(s);

		if (e.lost) {
			md_found_ = false;
			return;
		}
		const bool was_found = md_found_;
		md_found_ = true;

		// Palette commands are drained every poll (nothing here touches the beat grid).
		drain_commands(c);

		if (e.resync) {
			// First sample / Music Director (re)found / epoch change: re-anchor WITHOUT
			// firing beat or phrase edges (spec 2). A section change that arrives together
			// with a resync IS reported, except on the very first sample after start/found.
			if (e.section && was_found) {
				const Section sec = static_cast<Section>(e.new_section);
				cur_section_ = sec;
				Event ev;
				ev.type = Event::Type::SectionChange;
				ev.beat = static_cast<uint64_t>(e.section_beat);
				ev.section = sec;
				ev.section_seq = e.section_seq;
				ev.detail = std::string("section -> ") + section_name(sec);
				append_events({ev});
				append_events(scheduler_.on_section_change(sec, static_cast<uint64_t>(e.section_beat)));
			}
		} else {
			if (e.section) {
				const Section sec = static_cast<Section>(e.new_section);
				cur_section_ = sec;
				Event ev;
				ev.type = Event::Type::SectionChange;
				ev.beat = static_cast<uint64_t>(e.section_beat);
				ev.section = sec;
				ev.section_seq = e.section_seq;
				ev.detail = std::string("section -> ") + section_name(sec);
				append_events({ev});
				append_events(scheduler_.on_section_change(sec, static_cast<uint64_t>(e.section_beat)));
			}
			// The clock only advances the phrase counter on a real boundary; while the feed is
			// silent PD 1.2.0 never fired a phrase decision, so mirror that here.
			if (e.phrase && !c.silent)
				append_events(scheduler_.on_phrase(static_cast<uint64_t>(e.phrase_no),
								   static_cast<uint64_t>(c.beat)));
			if (e.beat) {
				cur_section_ = static_cast<Section>(c.section);
				append_events(scheduler_.on_beat(static_cast<uint64_t>(e.beat_no)));
			}
		}

		// Sub-beat clock drives the drop colour cycle; no-op when no cycle is running.
		if (scheduler_.drop_cycle_active())
			append_events(scheduler_.on_tick(c.beats_now));

		// Crossfade advances once per time step, exactly as PD 1.2.0 did per analysis hop.
		if (have_now_ && c.now_ns > last_now_ns_) {
			const double dt = static_cast<double>(c.now_ns - last_now_ns_) * 1e-9;
			scheduler_.advance_pfade(dt, c.bpm);
		}
		last_now_ns_ = c.now_ns;
		have_now_ = true;
	}

	DirectorState snapshot_state() const
	{
		DirectorState st;
		st.bpm = static_cast<float>(last_.bpm);
		st.confidence = static_cast<float>(last_.confidence);
		st.bass = static_cast<float>(std::min(1.0, std::max(0.0, last_.bass)));
		st.bass_level = st.bass;
		st.silent = last_.silent;
		const uint64_t beat = last_.beat > 0 ? static_cast<uint64_t>(last_.beat) : 0;
		st.beat = beat;
		st.phrase = beat / static_cast<uint64_t>(phrase_beats_);
		st.section = cur_section_;
		st.mix_target = static_cast<float>(scheduler_.mix_target(beat));
		st.rotation_offset = scheduler_.rotation_offset();
		st.live_palette = scheduler_.live_palette();
		st.prev_palette = scheduler_.prev_palette();
		st.pfade = scheduler_.pfade();
		st.cooldown_phrases_left = scheduler_.cooldown_left();
		st.lock_on = st.mix_target >= 0.5f;
		st.beat_frac = static_cast<float>(last_.beats_now - std::floor(last_.beats_now));
		st.next_palette = scheduler_.peek_next();
		st.paused = scheduler_.paused() || last_.paused;
		st.panic = scheduler_.panic();
		st.forced_on = scheduler_.forced_on();
		st.force_off = scheduler_.force_off();
		st.enabled = set_.director.enabled;
		st.drop_cycle = scheduler_.drop_cycle_active();
		st.palette_seq = scheduler_.palette_seq();
		st.cycling = scheduler_.cycling();
		return st;
	}

	SetFile set_;
	int breakdown_index_ = -1;

	Scheduler scheduler_;
	CommandQueue queue_;
	std::vector<Event> log_;
	std::vector<Event> out_;
	bool keep_history_ = true;
	bool drain_enabled_ = false;

	director_follow::Follower follower_;
	bool md_found_ = false;
	bool have_clock_ = false;
	ClockSample last_;
	int64_t last_now_ns_ = 0;
	bool have_now_ = false;

	int phrase_beats_ = 32;
	Section cur_section_ = Section::Groove;
};

PaletteDirector::PaletteDirector(const SetFile &s) : impl_(new Impl(s)) {}
PaletteDirector::~PaletteDirector() = default;
PaletteDirector::PaletteDirector(PaletteDirector &&) noexcept = default;
PaletteDirector &PaletteDirector::operator=(PaletteDirector &&) noexcept = default;

void PaletteDirector::on_clock(const ClockSample &c)
{
	if (impl_)
		impl_->on_clock(c);
}

DirectorState PaletteDirector::snapshot() const
{
	return impl_ ? impl_->snapshot_state() : DirectorState{};
}

void PaletteDirector::push_command(Command cmd, int palette_index)
{
	if (impl_)
		impl_->queue_.push(CommandMsg{cmd, palette_index});
}

void PaletteDirector::apply_set(const SetFile &s)
{
	if (impl_)
		impl_->apply(s);
}

void PaletteDirector::update_set(const SetFile &s, int live_palette)
{
	if (impl_)
		impl_->update(s, live_palette);
}

void PaletteDirector::drain_events(std::vector<Event> &out)
{
	if (!impl_)
		return;
	impl_->drain_enabled_ = true;
	out.insert(out.end(), impl_->out_.begin(), impl_->out_.end());
	impl_->out_.clear();
}

void PaletteDirector::set_keep_history(bool keep)
{
	if (!impl_)
		return;
	impl_->keep_history_ = keep;
	if (!keep)
		impl_->log_.clear();
}

const std::vector<Event> &PaletteDirector::events() const
{
	static const std::vector<Event> kEmpty;
	return impl_ ? impl_->log_ : kEmpty;
}

bool PaletteDirector::following() const
{
	return impl_ && impl_->md_found_;
}

} // namespace pd
