// Exactly-once, Scene Director event map + engine: the scripted clock drives cd::EventEngine with
// the default map. Decisions match cd::SwitchScheduler with the default SwitchConfig (same
// section_seq set, same section_beat), the default map plans only /playlist/next and
// /playlist/previous, and N=3 engines behind one process-wide guard send each event once.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <map>
#include <set>
#include <string>
#include <vector>

#include "clock_script.h"
#include "doctest.h"
#include "director-follow.h"
#include "scene-director/src/core/event_engine.h"
#include "scene-director/src/core/event_guard.h"
#include "scene-director/src/core/event_map.h"
#include "scene-director/src/core/switch_sched.h"

namespace {

cd::ClockSample to_clock(const xscript::Step &st)
{
	cd::ClockSample c;
	c.valid = st.ok;
	c.beats = st.beats;
	c.bpm = 128.0;
	c.section = static_cast<cd::Section>(st.section);
	c.phrase_beats = st.phrase_beats;
	c.section_seq = st.seq;
	c.section_beat = st.section_beat;
	c.section_forced = st.forced;
	c.epoch = st.epoch;
	c.beat = st.beat();
	c.phrase = st.phrase();
	return c;
}

director_follow::Sample to_follow(const xscript::Step &st)
{
	director_follow::Sample s;
	s.ok = st.ok;
	s.beat = st.beat();
	s.phrase = st.phrase();
	s.section = st.section;
	s.section_seq = st.seq;
	s.section_beat = st.section_beat;
	s.section_forced = st.forced;
	s.epoch = st.epoch;
	return s;
}

// Drives one engine over the whole script, returning every planned send.
std::vector<cd::PlannedSend> drive(cd::EventEngine &eng)
{
	director_follow::Follower follower;
	std::vector<cd::PlannedSend> all;
	for (const xscript::Step &st : xscript::script()) {
		const cd::ClockSample c = to_clock(st);
		const director_follow::Edges e = follower.update(to_follow(st));
		for (cd::PlannedSend &s : eng.tick(c, e, {}, 0))
			all.push_back(std::move(s));
	}
	return all;
}

} // namespace

TEST_CASE("xplugin: the default map reproduces 2.0's SwitchScheduler decisions")
{
	// The equivalence oracle: 2.0's SwitchScheduler, default SwitchConfig, driven directly.
	cd::SwitchScheduler sched;
	std::map<int64_t, double> expected; // section_seq -> at_beat (section_beat)
	for (const xscript::Step &st : xscript::script()) {
		const cd::ClockSample c = to_clock(st);
		for (const cd::SwitchEvent &e : sched.tick(c))
			if (e.section_edge)
				expected[e.section_seq] = e.at_beat;
	}
	REQUIRE_FALSE(expected.empty());

	// The engine, driven with the default map through the follower edges.
	cd::EventMap map = cd::EventMap::defaults();
	cd::EventEngine eng(map);
	const std::vector<cd::PlannedSend> sends = drive(eng);

	std::map<int64_t, double> actual; // section_seq -> target_beat for section events
	for (const cd::PlannedSend &s : sends) {
		if (s.event != "breakdown" && s.event != "drop")
			continue;
		actual[std::stoll(s.key)] = s.target_beat;
	}
	REQUIRE(actual.size() == expected.size());
	for (const auto &kv : expected) {
		CAPTURE(kv.first);
		REQUIRE(actual.count(kv.first) == 1);
		CHECK(actual[kv.first] == doctest::Approx(kv.second));
	}

	// The default map plans only /playlist/next and /playlist/previous.
	std::set<std::string> routes;
	for (const cd::PlannedSend &s : sends)
		routes.insert(s.route);
	for (const std::string &r : routes)
		CHECK((r == "/playlist/next" || r == "/playlist/previous"));
	CHECK(routes.count("/playlist/next") == 1);
}

TEST_CASE("xplugin: N=3 engines behind one guard send each section event exactly once")
{
	cd::EventMap map = cd::EventMap::defaults();
	std::vector<cd::EventEngine> engines;
	for (int i = 0; i < 3; ++i)
		engines.emplace_back(map);

	cd::EventGuard guard;
	std::map<std::string, int> claimed; // (event, key) -> count
	std::set<std::string> routes;
	for (cd::EventEngine &eng : engines) {
		for (const cd::PlannedSend &s : drive(eng)) {
			if (s.event != "breakdown" && s.event != "drop")
				continue;
			if (guard.claim(s.event, s.key))
				claimed[s.event + ":" + s.key]++;
			routes.insert(s.route);
		}
	}

	// Four section events in the script (seq 1, 2, 4, 5): each claimed exactly once.
	REQUIRE(claimed.size() == 4);
	for (const auto &kv : claimed)
		CHECK(kv.second == 1);
	for (const std::string &r : routes)
		CHECK(r == "/playlist/next");
}
