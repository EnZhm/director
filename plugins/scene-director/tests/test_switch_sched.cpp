#include "doctest.h"

#include <string>
#include <vector>

#include "switch_sched.h"
#include "types.h"

using namespace cd;

namespace {

// A plain sample (no section edge: section_seq 0 throughout).
ClockSample at(double beat, Section section, double bpm = 128.0, int phrase_beats = 16)
{
	ClockSample c;
	c.valid = true;
	c.beats = beat;
	c.bpm = bpm;
	c.section = section;
	c.phrase_beats = phrase_beats;
	c.beat = (int64_t)beat;
	return c;
}

// A section edge: section_seq changed and the section landed on `section_beat`.
ClockSample edge(double beat, Section section, int64_t seq, int64_t section_beat, bool forced = false,
		 double bpm = 128.0, int phrase_beats = 16, int64_t epoch = 0)
{
	ClockSample c = at(beat, section, bpm, phrase_beats);
	c.section_seq = seq;
	c.section_beat = section_beat;
	c.section_forced = forced;
	c.epoch = epoch;
	return c;
}

} // namespace

TEST_CASE("the first clock sample only anchors the scheduler")
{
	SwitchScheduler s;
	CHECK(s.tick(at(0.0, Section::Groove)).empty());
	CHECK(s.tick(at(1.0, Section::Groove)).empty());
}

TEST_CASE("a section is acted on once per section_seq, not per poll")
{
	SwitchScheduler s;
	s.tick(at(30.0, Section::Groove));
	/* the same edge polled several times in the same beat fires once */
	std::vector<SwitchEvent> ev;
	for (int i = 0; i < 5; ++i)
		for (const auto &e : s.tick(edge(32.0, Section::Breakdown, 1, 32)))
			ev.push_back(e);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(ev[0].section_seq == 1);
	CHECK(ev[0].section_beat == 32);
}

TEST_CASE("breakdown trigger: Next on the section's landing beat")
{
	SwitchConfig cfg;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove)); /* anchor mid-groove */

	const std::vector<SwitchEvent> ev = s.tick(edge(32.0, Section::Breakdown, 1, 32));
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(ev[0].at_beat == doctest::Approx(32.0));
	CHECK(ev[0].send_at_beat == doctest::Approx(32.0));

	/* the landing beat is the section_beat, even when the poll arrives a little after it */
	SwitchScheduler s2(cfg);
	s2.tick(at(30.0, Section::Groove));
	const std::vector<SwitchEvent> ev2 = s2.tick(edge(33.5, Section::Breakdown, 1, 32));
	REQUIRE(ev2.size() == 1);
	CHECK(ev2[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(ev2[0].at_beat == doctest::Approx(32.0));
}

TEST_CASE("breakdown trigger off: nothing is sent")
{
	SwitchConfig cfg;
	cfg.trigger_breakdown = false;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	CHECK(s.tick(edge(32.0, Section::Breakdown, 1, 32)).empty());
	CHECK(s.tick(at(33.0, Section::Breakdown)).empty());
}

TEST_CASE("drop trigger: Next on the section's landing beat")
{
	SwitchScheduler s;
	s.tick(at(60.0, Section::Groove));
	const std::vector<SwitchEvent> ev = s.tick(edge(64.0, Section::Drop, 1, 64));
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].reason == SwitchEvent::Reason::Drop);
	CHECK(ev[0].at_beat == doctest::Approx(64.0));
	CHECK(ev[0].send_at_beat == doctest::Approx(64.0));
	CHECK(ev[0].section_beat == 64);

	/* the poll can land past the downbeat; the target stays on section_beat */
	SwitchScheduler s2;
	s2.tick(at(60.0, Section::Groove));
	const std::vector<SwitchEvent> ev2 = s2.tick(edge(64.4, Section::Drop, 1, 64));
	REQUIRE(ev2.size() == 1);
	CHECK(ev2[0].at_beat == doctest::Approx(64.0));
}

TEST_CASE("drop trigger off: nothing is sent")
{
	SwitchConfig cfg;
	cfg.trigger_drop = false;
	SwitchScheduler s(cfg);
	s.tick(at(60.0, Section::Groove));
	CHECK(s.tick(edge(64.0, Section::Drop, 1, 64)).empty());
}

TEST_CASE("cooldown suppresses an automatic switch too close to the previous one")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));

	const std::vector<SwitchEvent> first = s.tick(edge(32.0, Section::Breakdown, 1, 32));
	REQUIRE(first.size() == 1);
	CHECK(first[0].at_beat == doctest::Approx(32.0));

	/* back to groove, then another breakdown 12 beats later: inside the cooldown, suppressed */
	s.tick(edge(40.0, Section::Groove, 2, 40));
	CHECK(s.tick(edge(44.0, Section::Breakdown, 3, 44)).empty());

	/* a breakdown one full cooldown after the switch is allowed again */
	SwitchScheduler s2(cfg);
	s2.tick(at(30.0, Section::Groove));
	REQUIRE(s2.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1);
	s2.tick(edge(40.0, Section::Groove, 2, 40));
	const std::vector<SwitchEvent> later = s2.tick(edge(48.0, Section::Breakdown, 3, 48));
	REQUIRE(later.size() == 1);
	CHECK(later[0].at_beat == doctest::Approx(48.0));
}

TEST_CASE("cooldown: an automatic drop 8 beats after a breakdown is suppressed")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1);
	/* automatic drop, 8 beats later: inside the cooldown */
	CHECK(s.tick(edge(40.0, Section::Drop, 2, 40)).empty());
}

TEST_CASE("cooldown: a forced drop 8 beats after a breakdown bypasses it")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1);
	/* forced drop (Drop now), 8 beats later: the cooldown is bypassed */
	const std::vector<SwitchEvent> forced = s.tick(edge(40.0, Section::Drop, 2, 40, /*forced=*/true));
	REQUIRE(forced.size() == 1);
	CHECK(forced[0].reason == SwitchEvent::Reason::Drop);
	CHECK(forced[0].at_beat == doctest::Approx(40.0));

	bool bypass = false;
	for (const std::string &line : s.log())
		bypass |= line.find("bypass") != std::string::npos;
	CHECK(bypass);
}

TEST_CASE("cooldown: a forced breakdown 8 beats after a breakdown bypasses it")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1);
	s.tick(edge(34.0, Section::Groove, 2, 34));
	const std::vector<SwitchEvent> forced = s.tick(edge(36.0, Section::Breakdown, 3, 36, /*forced=*/true));
	REQUIRE(forced.size() == 1);
	CHECK(forced[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(forced[0].at_beat == doctest::Approx(36.0));
}

TEST_CASE("a manual switch always fires (bypassing the cooldown) and restarts it")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1);

	/* a manual Next inside the cooldown still fires */
	const SwitchEvent manual = s.manual_next(at(33.0, Section::Breakdown));
	CHECK(manual.reason == SwitchEvent::Reason::ManualNext);
	CHECK(manual.at_beat == doctest::Approx(33.0));

	/* ...and restarts the cooldown: an automatic section 15 beats later is now held back */
	s.tick(edge(40.0, Section::Groove, 2, 40));
	CHECK(s.tick(edge(48.0, Section::Breakdown, 3, 48)).empty());

	/* Prev is the same switch, the other way */
	const SwitchEvent prev = s.manual_prev(at(49.0, Section::Breakdown));
	CHECK(prev.reason == SwitchEvent::Reason::ManualPrev);
}

TEST_CASE("manual switch before any clock sample still reports the current beat")
{
	SwitchScheduler s;
	const SwitchEvent e = s.manual_next(at(12.5, Section::Groove));
	CHECK(e.reason == SwitchEvent::Reason::ManualNext);
	CHECK(e.at_beat == doctest::Approx(12.5));
	CHECK(e.send_at_beat == doctest::Approx(12.5));
}

TEST_CASE("Scene lead sends the message early, in beats at the clock BPM")
{
	SwitchConfig cfg;
	cfg.lead_ms = 100.0; /* 100 ms at 120 BPM = 0.2 beats */
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove, 120.0));

	/* the target beat does not move; only the send time does */
	std::vector<SwitchEvent> ev;
	for (const auto &e : s.tick(edge(31.9, Section::Breakdown, 1, 32, false, 120.0)))
		ev.push_back(e);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].at_beat == doctest::Approx(32.0));
	CHECK(ev[0].send_at_beat == doctest::Approx(31.8));

	/* a slower clock turns the same lead into more beats */
	SwitchScheduler slow(cfg);
	slow.tick(at(30.0, Section::Groove, 60.0));
	const std::vector<SwitchEvent> down = slow.tick(edge(64.2, Section::Drop, 1, 64, false, 60.0));
	REQUIRE(down.size() == 1);
	CHECK(down[0].at_beat == doctest::Approx(64.0));
	CHECK(down[0].send_at_beat == doctest::Approx(63.9)); /* 100 ms at 60 BPM = 0.1 beats */
}

TEST_CASE("lead_ms is clamped to 0..500")
{
	SwitchConfig cfg;
	cfg.lead_ms = 5000.0;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove, 120.0));
	const std::vector<SwitchEvent> ev = s.tick(edge(32.0, Section::Breakdown, 1, 32, false, 120.0));
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].send_at_beat == doctest::Approx(31.0)); /* 500 ms at 120 BPM = 1 beat */
}

TEST_CASE("a clock jump re-plans and does not burst")
{
	SwitchScheduler s;
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1);
	/* Music Director reset: the clock lands far behind, the section_seq is unchanged */
	ClockSample back = at(0.0, Section::Breakdown);
	back.section_seq = 1;
	back.section_beat = 32;
	s.tick(back);
	ClockSample later = at(40.0, Section::Breakdown);
	later.section_seq = 1;
	later.section_beat = 32;
	CHECK(s.tick(later).size() <= 1);
}

TEST_CASE("resync re-anchors without a section edge and drops the stale cooldown")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(edge(32.0, Section::Breakdown, 1, 32)).size() == 1); /* a switch at beat 32 */

	/* Music Director re-anchors the grid back to beat 0 (Phrase now / tap / reset) */
	s.resync(edge(0.0, Section::Groove, 0, 0, false, 128.0, 16, /*epoch=*/5));
	/* the section was re-anchored: the same sequence does not fire on the next poll */
	CHECK(s.tick(edge(0.0, Section::Groove, 0, 0)).empty());
	/* a new sequence right after the reset still fires: the old cooldown anchor is gone */
	const std::vector<SwitchEvent> ev = s.tick(edge(4.0, Section::Drop, 1, 4));
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].reason == SwitchEvent::Reason::Drop);
}

TEST_CASE("an invalid sample (Music Director missing) sends nothing")
{
	SwitchScheduler s;
	s.tick(at(30.0, Section::Groove));
	ClockSample missing; /* valid == false */
	CHECK(s.tick(missing).empty());
}

TEST_CASE("the log records every decision")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	s.tick(edge(32.0, Section::Breakdown, 1, 32));
	s.tick(edge(40.0, Section::Groove, 2, 40));
	s.tick(edge(44.0, Section::Breakdown, 3, 44)); /* suppressed */

	const std::vector<std::string> &log = s.log();
	REQUIRE(log.size() >= 3);
	bool planned = false, suppressed = false, start = false;
	for (const std::string &line : log) {
		planned |= line.find("plan") != std::string::npos && line.find("breakdown start") != std::string::npos;
		suppressed |= line.find("suppress") != std::string::npos;
		start |= line.find("start") != std::string::npos;
	}
	CHECK(start);
	CHECK(planned);
	CHECK(suppressed);
}

TEST_CASE("cooldown: a manual Next does not cancel a pending forced section")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	/* Drop now at beat 39.0: the forced drop lands on beat 40, so its Next is still pending */
	const std::vector<SwitchEvent> early = s.tick(edge(39.0, Section::Drop, 1, 40, /*forced=*/true));
	bool drop = false;
	for (const SwitchEvent &e : early)
		drop |= e.reason == SwitchEvent::Reason::Drop;
	/* a manual Next before the landing beat restarts the cooldown */
	s.manual_next(edge(39.2, Section::Drop, 1, 40, true));
	for (double b = 39.25; b <= 40.5; b += 0.25)
		for (const SwitchEvent &e : s.tick(edge(b, Section::Drop, 1, 40, true)))
			drop |= e.reason == SwitchEvent::Reason::Drop && e.section_seq == 1;
	CHECK(drop);
}
