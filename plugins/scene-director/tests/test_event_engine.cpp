// Event engine: quantize modes, lead + offset, cooldown + forced bypass, debounce, placeholder
// expansion, resync, and exactly-once under repeated polls.
#include "doctest.h"

#include <string>
#include <vector>

#include "event_engine.h"

using namespace cd;

namespace {

ClockSample at(double beat, Section section = Section::Groove, double bpm = 128.0, int phrase_beats = 32)
{
	ClockSample c;
	c.valid = true;
	c.beats = beat;
	c.bpm = bpm;
	c.section = section;
	c.phrase_beats = phrase_beats;
	c.beat = static_cast<int64_t>(beat);
	c.phrase = static_cast<int64_t>(beat / phrase_beats);
	return c;
}

ClockSample edge(double beat, Section section, int64_t seq, int64_t section_beat, bool forced = false,
		 double bpm = 128.0)
{
	ClockSample c = at(beat, section, bpm);
	c.section_seq = seq;
	c.section_beat = section_beat;
	c.section_forced = forced;
	return c;
}

director_follow::Edges sec_edge(bool forced = false)
{
	director_follow::Edges e;
	e.section = true;
	e.section_forced = forced;
	return e;
}

director_follow::Edges beat_edge(int64_t no)
{
	director_follow::Edges e;
	e.beat = true;
	e.beat_no = no;
	return e;
}

director_follow::Edges phrase_edge()
{
	director_follow::Edges e;
	e.phrase = true;
	return e;
}

director_follow::Edges resync()
{
	director_follow::Edges e;
	e.resync = true;
	return e;
}

QueuedEvent next()
{
	QueuedEvent q;
	q.kind = QueuedEvent::Kind::ManualNext;
	return q;
}

QueuedEvent prev()
{
	QueuedEvent q;
	q.kind = QueuedEvent::Kind::ManualPrev;
	return q;
}

QueuedEvent scene(const std::string &name)
{
	QueuedEvent q;
	q.kind = QueuedEvent::Kind::SceneLoaded;
	q.scene = name;
	return q;
}

EventMap map_from(const char *json)
{
	EventMap m;
	const EventMap::ParseResult r = m.parse(json);
	REQUIRE(r.syntax_ok);
	return m;
}

} // namespace

TEST_CASE("a breakdown edge plans one /playlist/next aimed at section_beat")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(30.0, Section::Groove), {}, {}, 0); // anchor

	std::vector<PlannedSend> sends;
	for (int i = 0; i < 5; ++i) { // repeated polls inside the same beat: one plan only
		for (PlannedSend &s : eng.tick(edge(32.0, Section::Breakdown, 1, 32), sec_edge(), {}, 0))
			sends.push_back(std::move(s));
	}
	REQUIRE(sends.size() == 1);
	CHECK(sends[0].mapping_id == "breakdown-next");
	CHECK(sends[0].event == "breakdown");
	CHECK(sends[0].key == "1");
	CHECK(sends[0].route == "/playlist/next");
	CHECK(sends[0].target_beat == doctest::Approx(32.0));
	CHECK(sends[0].send_at_beats == doctest::Approx(32.0));
}

TEST_CASE("a drop edge plans one /playlist/next aimed at the drop downbeat")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(60.0, Section::Groove), {}, {}, 0);
	std::vector<PlannedSend> sends;
	for (PlannedSend &s : eng.tick(edge(64.4, Section::Drop, 1, 64), sec_edge(), {}, 0))
		sends.push_back(std::move(s));
	REQUIRE(sends.size() == 1);
	CHECK(sends[0].event == "drop");
	CHECK(sends[0].target_beat == doctest::Approx(64.0)); // a late poll still aims at section_beat
}

TEST_CASE("manual next / prev fire immediately with quantize now")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(12.0, Section::Groove), {}, {}, 0);
	{
		const std::vector<PlannedSend> s = eng.tick(at(12.5, Section::Groove), {}, {next()}, 0);
		REQUIRE(s.size() == 1);
		CHECK(s[0].event == "manual_next");
		CHECK(s[0].route == "/playlist/next");
		CHECK(s[0].target_beat == doctest::Approx(12.5));
	}
	{
		const std::vector<PlannedSend> s = eng.tick(at(13.0, Section::Groove), {}, {prev()}, 0);
		REQUIRE(s.size() == 1);
		CHECK(s[0].event == "manual_prev");
		CHECK(s[0].route == "/playlist/previous");
	}
}

TEST_CASE("lead and offset arithmetic at 128 and 174 BPM")
{
	const EventMap map = map_from("{\"mappings\": [{\"id\":\"x\",\"event\":\"breakdown\",\"enabled\":true,"
				      "\"quantize\":\"section_beat\",\"lead\":true,\"offset_ms\":50,"
				      "\"actions\":[{\"route\":\"/playlist/next\"}]}]}");

	auto run = [&](double bpm, double lead_ms, double expected_send_at) {
		EventEngine eng(map, lead_ms);
		eng.tick(at(30.0, Section::Groove, bpm), {}, {}, 0);
		const std::vector<PlannedSend> s =
			eng.tick(edge(32.0, Section::Breakdown, 1, 32, false, bpm), sec_edge(), {}, 0);
		REQUIRE(s.size() == 1);
		CHECK(s[0].target_beat == doctest::Approx(32.0)); // the target never moves
		CHECK(s[0].send_at_beats == doctest::Approx(expected_send_at));
	};

	// lead 100 ms, offset +50 ms. beats = ms/1000 * bpm/60.
	const double lead128 = 0.1 * 128.0 / 60.0;
	const double off128 = 0.05 * 128.0 / 60.0;
	run(128.0, 100.0, 32.0 - lead128 + off128);

	const double lead174 = 0.1 * 174.0 / 60.0;
	const double off174 = 0.05 * 174.0 / 60.0;
	run(174.0, 100.0, 32.0 - lead174 + off174);
}

TEST_CASE("lead_ms is clamped to 0..500")
{
	const EventMap map =
		map_from("{\"mappings\": [{\"id\":\"x\",\"event\":\"breakdown\",\"enabled\":true,"
			 "\"quantize\":\"section_beat\",\"lead\":true,\"actions\":[{\"route\":\"/x\"}]}]}");
	EventEngine eng(map, 5000.0);
	eng.tick(at(30.0, Section::Groove, 120.0), {}, {}, 0);
	const std::vector<PlannedSend> s =
		eng.tick(edge(32.0, Section::Breakdown, 1, 32, false, 120.0), sec_edge(), {}, 0);
	REQUIRE(s.size() == 1);
	CHECK(s[0].send_at_beats == doctest::Approx(31.0)); // 500 ms at 120 BPM = 1 beat
}

TEST_CASE("cooldown suppresses an automatic section edge within min_interval_beats")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(30.0, Section::Groove), {}, {}, 0);

	REQUIRE(eng.tick(edge(32.0, Section::Breakdown, 1, 32), sec_edge(), {}, 0).size() == 1);
	eng.tick(edge(40.0, Section::Groove, 2, 40), sec_edge(), {}, 0);
	CHECK(eng.tick(edge(44.0, Section::Breakdown, 3, 44), sec_edge(), {}, 0).empty());
	REQUIRE(eng.tick(edge(48.0, Section::Breakdown, 4, 48), sec_edge(), {}, 0).size() == 1);
}

TEST_CASE("a forced section edge bypasses the cooldown")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(30.0, Section::Groove), {}, {}, 0);

	REQUIRE(eng.tick(edge(32.0, Section::Breakdown, 1, 32), sec_edge(), {}, 0).size() == 1);
	eng.tick(edge(34.0, Section::Groove, 2, 34), sec_edge(), {}, 0);
	const std::vector<PlannedSend> forced =
		eng.tick(edge(36.0, Section::Breakdown, 3, 36, /*forced=*/true), sec_edge(true), {}, 0);
	REQUIRE(forced.size() == 1);
	CHECK(forced[0].event == "breakdown");
}

TEST_CASE("debounce suppresses a manual press within debounce_ms")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(0.0, Section::Groove), {}, {}, 0);
	const int64_t t0 = 1000000000; // 1 s

	REQUIRE(eng.tick(at(1.0, Section::Groove), {}, {next()}, t0).size() == 1);
	CHECK(eng.tick(at(1.0, Section::Groove), {}, {next()}, t0 + 50'000'000).empty());
	REQUIRE(eng.tick(at(1.0, Section::Groove), {}, {next()}, t0 + 200'000'000).size() == 1);
}

TEST_CASE("placeholder expansion: section, scene, beat, phrase, bar, section_seq, bpm")
{
	const EventMap map = map_from(
		"{\"mappings\": [{\"id\":\"s\",\"event\":\"scene_loaded\",\"enabled\":true,"
		"\"quantize\":\"now\",\"actions\":[{\"route\":\"/t\",\"args\":["
		"\"{section}\",\"{scene}\",\"{beat}\",\"{phrase}\",\"{bar}\",\"{section_seq}\",\"{bpm}\"]}]}]}");
	EventEngine eng(map);
	eng.tick(at(0.0, Section::Groove), {}, {}, 0);

	ClockSample c = at(40.0, Section::Drop, 128.0, 32);
	c.section_seq = 5;
	c.phrase_beat = 0;

	const std::vector<PlannedSend> s = eng.tick(c, {}, {scene("Foo")}, 0);
	REQUIRE(s.size() == 1);
	REQUIRE(s[0].args.size() == 7);
	CHECK(s[0].args[0].type == EventArg::Type::Int);
	CHECK(s[0].args[0].i == 2); // drop
	CHECK(s[0].args[1].type == EventArg::Type::String);
	CHECK(s[0].args[1].s == "Foo");
	CHECK(s[0].args[2].i == 40); // beat
	CHECK(s[0].args[3].i == 1);  // phrase
	CHECK(s[0].args[4].i == 10); // bar (40 / 4)
	CHECK(s[0].args[5].i == 5);  // section_seq
	CHECK(s[0].args[6].type == EventArg::Type::Float);
	CHECK(s[0].args[6].f == doctest::Approx(128.0f));
}

TEST_CASE("a literal string arg is not expanded")
{
	const EventMap map =
		map_from("{\"mappings\": [{\"id\":\"s\",\"event\":\"manual_next\",\"enabled\":true,"
			 "\"quantize\":\"now\",\"actions\":[{\"route\":\"/t\",\"args\":[\"hello {beat}\"]}]}]}");
	EventEngine eng(map);
	eng.tick(at(0.0, Section::Groove), {}, {}, 0);
	const std::vector<PlannedSend> s = eng.tick(at(5.0, Section::Groove), {}, {next()}, 0);
	REQUIRE(s.size() == 1);
	REQUIRE(s[0].args.size() == 1);
	CHECK(s[0].args[0].type == EventArg::Type::String);
	CHECK(s[0].args[0].s == "hello {beat}");
}

TEST_CASE("quantize bar / beat / phrase aim at the next boundary")
{
	// phrase_beat = 5, phrase_beats = 8: bar lines at 5, 9, 13; phrase boundaries at 5, 13, 21.
	const EventMap bar_map =
		map_from("{\"mappings\": [{\"id\":\"b\",\"event\":\"bar\",\"enabled\":true,\"quantize\":\"bar\","
			 "\"actions\":[{\"route\":\"/bar\"}]}]}");
	{
		EventEngine eng(bar_map);
		eng.tick(at(8.0, Section::Groove), {}, {}, 0);
		ClockSample c = at(9.0, Section::Groove);
		c.phrase_beat = 5;
		const std::vector<PlannedSend> s = eng.tick(c, beat_edge(9), {}, 0);
		REQUIRE(s.size() == 1);
		CHECK(s[0].target_beat == doctest::Approx(9.0)); // the bar line itself
	}
	const EventMap phrase_map =
		map_from("{\"mappings\": [{\"id\":\"p\",\"event\":\"bar\",\"enabled\":true,\"quantize\":\"phrase\","
			 "\"actions\":[{\"route\":\"/p\"}]}]}");
	{
		EventEngine eng(phrase_map);
		eng.tick(at(8.0, Section::Groove), {}, {}, 0);
		ClockSample c = at(9.0, Section::Groove);
		c.phrase_beat = 5;
		c.phrase_beats = 8;
		CHECK(eng.tick(c, beat_edge(9), {}, 0).empty()); // aimed at beat 13: still pending
		ClockSample c2 = at(13.0, Section::Groove);
		c2.phrase_beat = 5;
		c2.phrase_beats = 8;
		const std::vector<PlannedSend> s = eng.tick(c2, {}, {}, 0);
		REQUIRE(s.size() == 1);
		CHECK(s[0].target_beat == doctest::Approx(13.0)); // next phrase boundary after 9
	}
	const EventMap beat_map =
		map_from("{\"mappings\": [{\"id\":\"t\",\"event\":\"bar\",\"enabled\":true,\"quantize\":\"beat\","
			 "\"actions\":[{\"route\":\"/t\"}]}]}");
	{
		EventEngine eng(beat_map);
		eng.tick(at(8.0, Section::Groove), {}, {}, 0);
		ClockSample c = at(9.0, Section::Groove);
		c.phrase_beat = 5;
		const std::vector<PlannedSend> s = eng.tick(c, beat_edge(9), {}, 0);
		REQUIRE(s.size() == 1);
		CHECK(s[0].target_beat == doctest::Approx(9.0));
	}
}

TEST_CASE("resync drops pending beat/bar/phrase plans and fires no catch-up")
{
	const EventMap map =
		map_from("{\"mappings\": [{\"id\":\"p\",\"event\":\"phrase\",\"enabled\":true,\"quantize\":\"phrase\","
			 "\"actions\":[{\"route\":\"/p\"}]}]}");
	EventEngine eng(map);
	eng.tick(at(0.0, Section::Groove), {}, {}, 0);

	// A phrase edge at beat 32 plans a send aimed at the next phrase boundary (beat 64).
	ClockSample c = at(32.0, Section::Groove);
	c.phrase = 1;
	c.phrase_beat = 32;
	eng.tick(c, phrase_edge(), {}, 0); // planned, not yet due

	// Grid re-anchored: the pending phrase plan is dropped and nothing fires.
	ClockSample r = at(0.0, Section::Groove);
	r.epoch = 2;
	const std::vector<PlannedSend> s = eng.tick(r, resync(), {}, 0);
	CHECK(s.empty());
}

TEST_CASE("a pending section plan survives a manual press")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(30.0, Section::Groove), {}, {}, 0);

	// Drop now at 39.0 landing on beat 40: the drop Next is still pending.
	CHECK(eng.tick(edge(39.0, Section::Drop, 1, 40, /*forced=*/true), sec_edge(true), {}, 0).empty());

	// A manual press before the landing beat does not cancel it.
	eng.tick(edge(39.2, Section::Drop, 1, 40, true), {}, {next()}, 0);

	// The drop Next still fires when the clock reaches the landing beat.
	bool drop = false;
	for (double b = 39.25; b <= 40.5; b += 0.25) {
		for (const PlannedSend &s : eng.tick(edge(b, Section::Drop, 1, 40, true), {}, {}, 0))
			drop |= s.event == "drop";
	}
	CHECK(drop);
}

TEST_CASE("an invalid sample (Music Director missing) still sends manual events")
{
	EventEngine eng(EventMap::defaults());
	eng.tick(at(30.0, Section::Groove), {}, {}, 0);
	ClockSample missing;                         // valid == false
	CHECK(eng.tick(missing, {}, {}, 0).empty()); // no section sends
	const std::vector<PlannedSend> s = eng.tick(missing, {}, {next()}, 0);
	REQUIRE(s.size() == 1);
	CHECK(s[0].event == "manual_next");
}
