// Scripted director_follow-style clock samples drive the Scene Director scheduler the way the OBS
// layer does (through director_follow::Follower), so the whole edge path is covered libobs-free:
// groove -> breakdown -> drop -> groove, an epoch change, Music Director lost and found, and
// repeated polls inside one beat. Exactly one Next per section_seq, and breakdown / drop land on
// the section_beat.
#include "doctest.h"

#include <map>
#include <vector>

#include "director-follow.h"
#include "switch_sched.h"
#include "types.h"

using namespace cd;

namespace {

ClockSample sample(double beat, Section section, int64_t seq, int64_t section_beat, bool valid = true,
		   bool forced = false, int64_t epoch = 0, int phrase_beats = 16)
{
	ClockSample c;
	c.valid = valid;
	c.beats = beat;
	c.bpm = 128.0;
	c.section = section;
	c.phrase_beats = phrase_beats;
	c.beat = (int64_t)beat;
	c.phrase = (int64_t)(beat / phrase_beats);
	c.section_seq = seq;
	c.section_beat = section_beat;
	c.section_forced = forced;
	c.epoch = epoch;
	return c;
}

// Mirrors drive_locked(): the follower decides resync / lost / found, the scheduler keys section
// edges on section_seq.
struct Driver {
	director_follow::Follower follower;
	SwitchScheduler sched;
	SwitchConfig cfg;

	Driver()
	{
		cfg.cooldown_beats = 0; /* this test is about the edges, not the cooldown */
		sched.set_config(cfg);
	}

	std::vector<SwitchEvent> poll(const ClockSample &clk)
	{
		director_follow::Sample s;
		s.ok = clk.valid;
		s.beat = clk.beat;
		s.phrase = clk.phrase;
		s.section = (int)clk.section;
		s.section_seq = clk.section_seq;
		s.section_beat = clk.section_beat;
		s.section_forced = clk.section_forced;
		s.epoch = clk.epoch;
		const director_follow::Edges e = follower.update(s);
		if (!clk.valid)
			return {};
		if (e.resync) {
			sched.resync(clk);
			return {};
		}
		return sched.tick(clk);
	}
};

} // namespace

TEST_CASE("a scripted set sends exactly one Next per section_seq")
{
	Driver d;
	std::vector<SwitchEvent> events;
	auto feed = [&](const ClockSample &c) {
		for (const SwitchEvent &e : d.poll(c))
			events.push_back(e);
	};

	feed(sample(0.0, Section::Groove, 0, 0)); /* first sample: resync, no edge */
	feed(sample(8.0, Section::Groove, 0, 0)); /* still groove */
	feed(sample(16.0, Section::Breakdown, 1, 16));
	feed(sample(16.2, Section::Breakdown, 1, 16)); /* repeated poll inside the beat */
	feed(sample(23.9, Section::Breakdown, 1, 16));
	feed(sample(24.0, Section::Drop, 2, 24));
	feed(sample(32.0, Section::Groove, 3, 32));                           /* groove edge: no automatic Next */
	feed(sample(40.0, Section::Groove, 3, 32, true, false, /*epoch=*/1)); /* epoch change: resync */
	feed(sample(41.0, Section::Groove, 3, 32, true, false, 1));
	feed(sample(48.0, Section::Groove, 3, 32, /*valid=*/false)); /* Music Director lost */
	feed(sample(48.5, Section::Groove, 3, 32, false));
	feed(sample(56.0, Section::Groove, 0, 0, true, false, /*epoch=*/2)); /* found again: resync only */
	feed(sample(64.0, Section::Drop, 1, 64, true, false, 2));            /* a real new section */

	/* one Next per sequence that carried a section edge: 1 (breakdown), 2 (drop), 1 (drop) */
	REQUIRE(events.size() == 3);
	CHECK(events[0].section_seq == 1);
	CHECK(events[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(events[0].at_beat == doctest::Approx(16.0));
	CHECK(events[0].section_beat == 16);
	CHECK(events[1].section_seq == 2);
	CHECK(events[1].reason == SwitchEvent::Reason::Drop);
	CHECK(events[1].at_beat == doctest::Approx(24.0));
	CHECK(events[1].section_beat == 24);
	CHECK(events[2].section_seq == 1);
	CHECK(events[2].reason == SwitchEvent::Reason::Drop);
	CHECK(events[2].at_beat == doctest::Approx(64.0));
	CHECK(events[2].section_beat == 64);

	/* every section-triggered Next landed on its section_beat */
	for (const SwitchEvent &e : events) {
		CHECK(e.section_edge);
		CHECK(e.at_beat == doctest::Approx((double)e.section_beat));
	}
}

TEST_CASE("repeated polls inside one beat never repeat a section Next")
{
	Driver d;
	d.poll(sample(0.0, Section::Groove, 0, 0));
	int nexts = 0;
	for (int i = 0; i < 20; ++i) {
		for (const SwitchEvent &e : d.poll(sample(16.0, Section::Breakdown, 1, 16)))
			if (e.section_edge)
				++nexts;
	}
	CHECK(nexts == 1);
}
