// CD follows Music Director's clock (spec 2, 5): a section change is acted on exactly once, when
// section_seq changes, and the breakdown / drop edge cuts its armed deck on section_beat.
//
// Clock samples are scripted directly (no cross-plugin build): groove -> forced breakdown -> drop
// -> groove, natural and forced, an epoch change, Music Director lost and found, and repeated
// polls in the same beat.
#include "doctest.h"

#include <algorithm>
#include <string>
#include <vector>

#include "scheduler.h"

using namespace cd;

namespace {

ClockSample smp(double beats, Section sec, int64_t seq, int64_t sbeat, bool forced = false, int64_t epoch = 1,
		bool valid = true)
{
	ClockSample c;
	c.valid = valid;
	c.beats = beats;
	c.bpm = 128.0;
	c.section = sec;
	c.section_seq = seq;
	c.section_beat = sbeat;
	c.section_forced = forced;
	c.epoch = epoch;
	c.drop_cycle = false;
	c.drop_cycle_steps_per_beat = 2;
	return c;
}

// Polls the clock at ~60 fps from `from` to `to` beats with the given section/seq/section_beat.
void poll(CutScheduler &s, std::vector<SchedEvent> &out, double from, double to, Section sec, int64_t seq, int64_t sb,
	  bool forced = false, int64_t epoch = 1)
{
	const double per_frame = 128.0 / 60.0 / 60.0;
	for (double b = from; b < to; b += per_frame)
		for (const auto &e : s.tick(smp(b, sec, seq, sb, forced, epoch)))
			out.push_back(e);
}

int edge_cuts(const std::vector<SchedEvent> &v, int64_t seq)
{
	int n = 0;
	for (const auto &e : v)
		if (e.type == SchedEvent::Type::Cut && e.section_edge && e.section_seq == seq)
			n++;
	return n;
}

const SchedEvent *first_edge(const std::vector<SchedEvent> &v, int64_t seq)
{
	for (const auto &e : v)
		if (e.type == SchedEvent::Type::Cut && e.section_edge && e.section_seq == seq)
			return &e;
	return nullptr;
}

int prepares(const std::vector<SchedEvent> &v, int slot)
{
	int n = 0;
	for (const auto &e : v)
		n += (e.type == SchedEvent::Type::Prepare && e.slot == slot) ? 1 : 0;
	return n;
}

SchedulerConfig cfg2()
{
	SchedulerConfig cfg;
	cfg.groove.crossfade_chance = 0.0;
	cfg.preroll_beats = 2.0;
	return cfg;
}

} // namespace

TEST_CASE("groove -> forced breakdown -> drop -> groove: exactly one section cut per section_seq")
{
	CutScheduler s(cfg2());
	std::vector<SchedEvent> ev;
	poll(s, ev, 0.0, 8.0, Section::Groove, 1, 0);
	poll(s, ev, 8.0, 20.0, Section::Breakdown, 2, 8, /*forced*/ true);
	poll(s, ev, 20.0, 30.0, Section::Drop, 3, 20);
	poll(s, ev, 30.0, 40.0, Section::Groove, 4, 30);

	CHECK(edge_cuts(ev, 2) == 1); // the breakdown edge cuts once
	CHECK(edge_cuts(ev, 3) == 1); // the drop edge cuts once
	CHECK(edge_cuts(ev, 4) == 0); // a return to groove keeps the bar-line rule
	CHECK(edge_cuts(ev, 1) == 0); // the first sample syncs silently

	const SchedEvent *bd = first_edge(ev, 2);
	REQUIRE(bd);
	CHECK(bd->slot == CutScheduler::kBreakdown);
	CHECK(bd->at_beat == 8.0); // section_beat
	CHECK(bd->transition == Transition::Crossfade);

	const SchedEvent *dr = first_edge(ev, 3);
	REQUIRE(dr);
	CHECK(dr->slot == CutScheduler::kDrop);
	CHECK(dr->at_beat == 20.0); // section_beat

	// re-arm events follow: the breakdown deck is armed while not in a breakdown, the drop deck
	// while not in a drop
	CHECK(prepares(ev, CutScheduler::kBreakdown) >= 2);
	CHECK(prepares(ev, CutScheduler::kDrop) >= 2);
}

TEST_CASE("repeated polls in the same beat do not repeat a section cut")
{
	CutScheduler s(cfg2());
	std::vector<SchedEvent> ev;
	poll(s, ev, 0.0, 4.0, Section::Groove, 1, 0);
	// the breakdown edge, then the very same sample again several times
	for (int i = 0; i < 5; ++i)
		for (const auto &e : s.tick(smp(4.0, Section::Breakdown, 2, 4)))
			ev.push_back(e);
	poll(s, ev, 4.0, 10.0, Section::Breakdown, 2, 4);
	CHECK(edge_cuts(ev, 2) == 1);
}

TEST_CASE("an epoch change re-anchors beat tracking without a repeated section cut")
{
	CutScheduler s(cfg2());
	std::vector<SchedEvent> ev;
	poll(s, ev, 0.0, 4.0, Section::Groove, 1, 0);
	poll(s, ev, 4.0, 8.0, Section::Groove, 1, 0, false, /*epoch*/ 2); // same section, new epoch
	CHECK(edge_cuts(ev, 1) == 0);
	// a section change that arrives with an epoch change IS reported
	poll(s, ev, 8.0, 12.0, Section::Breakdown, 2, 8, false, /*epoch*/ 3);
	CHECK(edge_cuts(ev, 2) == 1);
}

TEST_CASE("Music Director lost and found: the clips hold, and no section cut repeats")
{
	CutScheduler s(cfg2());
	std::vector<SchedEvent> ev;
	poll(s, ev, 0.0, 4.0, Section::Groove, 1, 0);
	const std::size_t before = ev.size();
	// lost: an invalid sample makes the scheduler emit nothing at all (clips hold)
	for (int i = 0; i < 20; ++i)
		for (const auto &e : s.tick(smp(4.0 + i * 0.1, Section::Groove, 1, 0, false, 1, /*valid*/ false)))
			ev.push_back(e);
	CHECK(ev.size() == before);
	// found again: same section_seq, so no section cut
	poll(s, ev, 6.0, 10.0, Section::Groove, 1, 0);
	CHECK(edge_cuts(ev, 1) == 0);
}

TEST_CASE("a forced breakdown and a detected breakdown behave the same on the section beat")
{
	for (const bool forced : {false, true}) {
		CutScheduler s(cfg2());
		std::vector<SchedEvent> ev;
		poll(s, ev, 0.0, 8.0, Section::Groove, 1, 0);
		poll(s, ev, 8.0, 20.0, Section::Breakdown, 2, 8, forced);
		const SchedEvent *bd = first_edge(ev, 2);
		REQUIRE(bd);
		CHECK(bd->at_beat == 8.0);
		CHECK(bd->slot == CutScheduler::kBreakdown);
		CHECK(edge_cuts(ev, 2) == 1);
	}
}
