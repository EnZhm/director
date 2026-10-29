#include "doctest.h"

#include <string>
#include <vector>

#include "switch_sched.h"
#include "types.h"

using namespace cd;

namespace {

ClockSample at(double beat, Section section, double bpm = 128.0, int phrase_beats = 16)
{
	ClockSample c;
	c.valid = true;
	c.beats = beat;
	c.bpm = bpm;
	c.section = section;
	c.phrase_beats = phrase_beats;
	return c;
}

std::vector<SwitchEvent> run(SwitchScheduler &s, Section section, double from, double to, double step,
			     double bpm = 128.0)
{
	std::vector<SwitchEvent> out;
	for (double b = from; b <= to + 1e-9; b += step) {
		for (const SwitchEvent &e : s.tick(at(b, section, bpm)))
			out.push_back(e);
	}
	return out;
}

} // namespace

TEST_CASE("the first clock sample only anchors the scheduler")
{
	SwitchScheduler s;
	CHECK(s.tick(at(0.0, Section::Groove)).empty());
	CHECK(s.tick(at(1.0, Section::Groove)).empty());
}

TEST_CASE("breakdown trigger: Next on the breakdown crossfade start (the next bar line)")
{
	SwitchConfig cfg;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove)); /* anchor mid-groove */

	/* the section flips to BREAKDOWN at 32.0: the switch is aimed at the bar line at/after it */
	std::vector<SwitchEvent> ev;
	for (const auto &e : s.tick(at(32.0, Section::Breakdown)))
		ev.push_back(e);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(ev[0].at_beat == doctest::Approx(32.0));
	CHECK(ev[0].send_at_beat == doctest::Approx(32.0));

	/* mid-beat the crossfade start is the next bar line: planned at 33.5, sent on the bar line 36 */
	SwitchScheduler s2(cfg);
	s2.tick(at(30.0, Section::Groove));
	CHECK(s2.tick(at(33.5, Section::Breakdown)).empty());
	std::vector<SwitchEvent> ev2;
	for (const auto &e : s2.tick(at(36.0, Section::Breakdown)))
		ev2.push_back(e);
	REQUIRE(ev2.size() == 1);
	CHECK(ev2[0].reason == SwitchEvent::Reason::Breakdown);
	CHECK(ev2[0].at_beat == doctest::Approx(36.0));
}

TEST_CASE("breakdown trigger off: nothing is sent")
{
	SwitchConfig cfg;
	cfg.trigger_breakdown = false;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	CHECK(s.tick(at(32.0, Section::Breakdown)).empty());
	CHECK(s.tick(at(33.0, Section::Breakdown)).empty());
}

TEST_CASE("drop trigger: Next on the drop downbeat itself")
{
	SwitchScheduler s;
	s.tick(at(60.0, Section::Groove));
	const std::vector<SwitchEvent> ev = s.tick(at(64.0, Section::Drop));
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].reason == SwitchEvent::Reason::Drop);
	CHECK(ev[0].at_beat == doctest::Approx(64.0));
	CHECK(ev[0].send_at_beat == doctest::Approx(64.0));
}

TEST_CASE("drop trigger off: nothing is sent")
{
	SwitchConfig cfg;
	cfg.trigger_drop = false;
	SwitchScheduler s(cfg);
	s.tick(at(60.0, Section::Groove));
	CHECK(s.tick(at(64.0, Section::Drop)).empty());
}

TEST_CASE("groove phrases are off by default and fire on phrase downbeats when on")
{
	SwitchConfig off;
	SwitchScheduler s0(off);
	s0.tick(at(0.0, Section::Groove));
	CHECK(run(s0, Section::Groove, 1.0, 64.0, 1.0).empty());

	SwitchConfig on;
	on.trigger_groove_phrase = true;
	SwitchScheduler s(on);
	s.tick(at(0.0, Section::Groove));
	const std::vector<SwitchEvent> ev = run(s, Section::Groove, 1.0, 64.0, 1.0);
	std::vector<double> beats;
	for (const auto &e : ev) {
		CHECK(e.reason == SwitchEvent::Reason::GroovePhrase);
		beats.push_back(e.at_beat);
	}
	REQUIRE(beats.size() == 4); /* 16, 32, 48, 64 */
	CHECK(beats[0] == doctest::Approx(16.0));
	CHECK(beats[1] == doctest::Approx(32.0));
	CHECK(beats[2] == doctest::Approx(48.0));
	CHECK(beats[3] == doctest::Approx(64.0));
}

TEST_CASE("cooldown suppresses an automatic switch too close to the previous one")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));

	const std::vector<SwitchEvent> first = s.tick(at(32.0, Section::Breakdown));
	REQUIRE(first.size() == 1);
	CHECK(first[0].at_beat == doctest::Approx(32.0));

	/* back to groove, then another breakdown 12 beats later: inside the cooldown, suppressed */
	s.tick(at(40.0, Section::Groove));
	CHECK(s.tick(at(44.0, Section::Breakdown)).empty());

	/* a breakdown one full cooldown after the switch is allowed again */
	SwitchScheduler s2(cfg);
	s2.tick(at(30.0, Section::Groove));
	REQUIRE(s2.tick(at(32.0, Section::Breakdown)).size() == 1);
	s2.tick(at(40.0, Section::Groove));
	const std::vector<SwitchEvent> later = s2.tick(at(48.0, Section::Breakdown));
	REQUIRE(later.size() == 1);
	CHECK(later[0].at_beat == doctest::Approx(48.0));
}

TEST_CASE("a manual switch always fires and restarts the cooldown")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(at(32.0, Section::Breakdown)).size() == 1);

	/* a manual Next inside the cooldown still fires */
	const SwitchEvent manual = s.manual_next(at(33.0, Section::Breakdown));
	CHECK(manual.reason == SwitchEvent::Reason::ManualNext);
	CHECK(manual.at_beat == doctest::Approx(33.0));

	/* ...and restarts the cooldown: an automatic switch 15 beats later is now held back */
	s.tick(at(40.0, Section::Groove));
	CHECK(s.tick(at(48.0, Section::Breakdown)).empty());

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
	for (const auto &e : s.tick(at(31.5, Section::Breakdown, 120.0)))
		ev.push_back(e);
	CHECK(ev.empty()); /* 31.5 < 32.0 - 0.2 */
	for (const auto &e : s.tick(at(31.9, Section::Breakdown, 120.0)))
		ev.push_back(e);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].at_beat == doctest::Approx(32.0));
	CHECK(ev[0].send_at_beat == doctest::Approx(31.8));

	/* a slower clock turns the same lead into more beats */
	SwitchScheduler slow(cfg);
	slow.tick(at(30.0, Section::Groove, 60.0));
	const std::vector<SwitchEvent> down = slow.tick(at(64.2, Section::Drop, 60.0));
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
	const std::vector<SwitchEvent> ev = s.tick(at(32.0, Section::Breakdown, 120.0));
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].send_at_beat == doctest::Approx(31.0)); /* 500 ms at 120 BPM = 1 beat */
}

TEST_CASE("a clock jump re-plans and does not burst")
{
	SwitchScheduler s;
	s.tick(at(30.0, Section::Groove));
	REQUIRE(s.tick(at(32.0, Section::Breakdown)).size() == 1);
	/* Palette Director reset: the clock lands far behind, then catches up */
	s.tick(at(0.0, Section::Breakdown));
	const std::vector<SwitchEvent> ev = s.tick(at(40.0, Section::Breakdown));
	CHECK(ev.size() <= 1);
}

TEST_CASE("the log records every decision")
{
	SwitchConfig cfg;
	cfg.cooldown_beats = 16;
	SwitchScheduler s(cfg);
	s.tick(at(30.0, Section::Groove));
	s.tick(at(32.0, Section::Breakdown));
	s.tick(at(40.0, Section::Groove));
	s.tick(at(44.0, Section::Breakdown)); /* suppressed */

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
