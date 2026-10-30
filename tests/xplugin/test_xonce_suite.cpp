// Exactly-once across the suite with Scene Director on OSC (spec 8 (b)-(d)). One scripted Music
// Director clock (clock_script.h: natural and forced sections, an epoch change, Music Director lost
// and found) drives Palette Director's follower core and Scene Director's event engine here, and
// Clip Director's scheduler in xplugin_cd (its own executable: Clip and Scene Director both keep
// their core in namespace cd). All three assert against the same xscript::edges() section_beat.
#include <map>
#include <set>
#include <string>
#include <vector>

#include "clock_script.h"
#include "doctest.h"
#include "director-follow.h"
#include "palette-director/src/director/palette-director.h"
#include "scene-director/src/core/event_engine.h"
#include "scene-director/src/core/event_guard.h"
#include "scene-director/src/core/event_map.h"

namespace {

cd::ClockSample sd_clock(const xscript::Step &st)
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
	c.phrase_beat = st.phrase() * st.phrase_beats;
	return c;
}

director_follow::Sample follow(const xscript::Step &st)
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

cd::EventMap map_from(const char *json)
{
	cd::EventMap m;
	const cd::EventMap::ParseResult r = m.parse(json);
	REQUIRE(r.syntax_ok);
	return m;
}

// Drives one engine over the whole script and returns every planned send.
std::vector<cd::PlannedSend> drive(cd::EventEngine &eng)
{
	director_follow::Follower follower;
	std::vector<cd::PlannedSend> all;
	for (const xscript::Step &st : xscript::script()) {
		const director_follow::Edges e = follower.update(follow(st));
		for (cd::PlannedSend &s : eng.tick(sd_clock(st), e, {}, 0))
			all.push_back(std::move(s));
	}
	return all;
}

// Palette Director's SectionChange beats, per section_beat (the test_xonce_pd.cpp harness).
std::map<int64_t, int> pd_section_changes()
{
	pd::SetFile set;
	set.name = "x";
	for (int i = 0; i < 4; ++i) {
		pd::PaletteSpec p;
		p.name = "Pal " + std::to_string(i);
		p.colours = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
		set.palettes.push_back(p);
	}
	set.palette_changes.breakdown_palette = "Pal 3";
	pd::PaletteDirector d(set);
	int64_t now = 0;
	for (const xscript::Step &s : xscript::script()) {
		pd::ClockSample c;
		c.ok = s.ok;
		c.beat = s.beat();
		c.phrase = s.phrase();
		c.phrase_beats = s.phrase_beats;
		c.section = s.section;
		c.section_seq = s.seq;
		c.section_beat = s.section_beat;
		c.epoch = s.epoch;
		c.beats_now = s.beats;
		c.bpm = 128.0;
		c.silent = false;
		now += 1000000;
		c.now_ns = now;
		d.on_clock(c);
	}
	std::map<int64_t, int> count;
	for (const pd::Event &e : d.events())
		if (e.type == pd::Event::Type::SectionChange)
			count[static_cast<int64_t>(e.beat)]++;
	return count;
}

// Every section event mapped (breakdown, drop and groove), one action each.
const char *kAllSections = R"({"version":1,"mappings":[
  {"id":"bd","event":"breakdown","enabled":true,"quantize":"section_beat","actions":[{"route":"/playlist/next"}]},
  {"id":"dr","event":"drop","enabled":true,"quantize":"section_beat","actions":[{"route":"/playlist/next"}]},
  {"id":"gr","event":"groove","enabled":true,"quantize":"section_beat","actions":[{"route":"/playlist/previous"}]}
]})";

// Section, phrase and bar events, a two-action drop and a second mapping on the drop.
const char *kBusy = R"({"version":1,"mappings":[
  {"id":"bd","event":"breakdown","enabled":true,"quantize":"section_beat","actions":[{"route":"/playlist/next"}]},
  {"id":"dr","event":"drop","enabled":true,"quantize":"section_beat",
   "actions":[{"route":"/playlist/next"},{"route":"/controls/global/bang/1"}]},
  {"id":"dr2","event":"drop","enabled":true,"quantize":"section_beat","cooldown_group":"dr2",
   "actions":[{"route":"/controls/global/bang/2"}]},
  {"id":"ph","event":"phrase","enabled":true,"quantize":"now","actions":[{"route":"/controls/global/bang/3"}]},
  {"id":"bar","event":"bar","enabled":true,"quantize":"now","actions":[{"route":"/controls/global/bang/4"}]}
]})";

} // namespace

TEST_CASE("xplugin 8(d): PD, CD and SD act once per section_seq, all on section_beat")
{
	/* Palette Director: one SectionChange per edge, on section_beat */
	const std::map<int64_t, int> pd = pd_section_changes();
	REQUIRE(pd.size() == xscript::edges().size());

	/* Scene Director, every section event mapped: one send per section_seq on section_beat */
	cd::EventEngine eng(map_from(kAllSections));
	std::map<int64_t, std::vector<double>> sd; // section_seq -> target beats
	for (const cd::PlannedSend &s : drive(eng))
		sd[std::stoll(s.key)].push_back(s.target_beat);

	for (const xscript::Edge &e : xscript::edges()) {
		CAPTURE(e.seq);
		CHECK(pd.at(e.section_beat) == 1);
		REQUIRE(sd[e.seq].size() == 1u);
		CHECK(sd[e.seq][0] == doctest::Approx(static_cast<double>(e.section_beat)));
	}
	CHECK(sd.size() == xscript::edges().size());

	/* The default map: once per breakdown / drop section_seq, on the beat where Clip Director
	 * cuts (xplugin_cd asserts the CD side against the same edges). */
	cd::EventEngine def(cd::EventMap::defaults());
	std::map<int64_t, std::vector<double>> sd_def;
	for (const cd::PlannedSend &s : drive(def))
		if (s.event == "breakdown" || s.event == "drop")
			sd_def[std::stoll(s.key)].push_back(s.target_beat);
	for (const xscript::Edge &e : xscript::edges()) {
		CAPTURE(e.seq);
		if (e.section == 0) {
			CHECK(sd_def.count(e.seq) == 0u);
			continue;
		}
		REQUIRE(sd_def[e.seq].size() == 1u);
		CHECK(sd_def[e.seq][0] == doctest::Approx(static_cast<double>(e.section_beat)));
	}
}

TEST_CASE("xplugin 8(b): N=3 sources behind one guard: each section, phrase and bar event goes out once")
{
	const cd::EventMap map = map_from(kBusy);
	std::vector<std::string> ids;
	for (const cd::Mapping &m : map.mappings())
		ids.push_back(m.id);
	REQUIRE(ids.size() == 5u);

	/* The OBS layer: one process-wide guard, one SourceClaims per source (sd-source.cpp). */
	cd::EventGuard guard;
	struct Src {
		cd::EventEngine eng;
		cd::SourceClaims claims;
		explicit Src(const cd::EventMap &m) : eng(m) {}
	};
	std::vector<Src> sources;
	for (int i = 0; i < 3; ++i)
		sources.emplace_back(map);

	/* sent[(event, key)] -> (source index -> routes) */
	std::map<std::pair<std::string, std::string>, std::map<int, std::vector<std::string>>> sent;
	for (int i = 0; i < 3; ++i) {
		for (const cd::PlannedSend &p : drive(sources[i].eng)) {
			if (!sources[i].claims.may_send(guard, p.event, p.key))
				continue;
			sent[{p.event, p.key}][i].push_back(p.route);
		}
	}

	std::map<std::string, int> per_event; // events seen, by kind
	for (const auto &kv : sent) {
		CAPTURE(kv.first.first);
		CAPTURE(kv.first.second);
		REQUIRE(kv.second.size() == 1u); /* exactly one source acted */
		const std::vector<std::string> &routes = kv.second.begin()->second;
		per_event[kv.first.first]++;
		if (kv.first.first == "drop") {
			/* all three drop sends (two actions + the second mapping), once each */
			CHECK(routes.size() == 3u);
			CHECK(std::set<std::string>(routes.begin(), routes.end()).size() == 3u);
		} else {
			CHECK(routes.size() == 1u);
		}
	}
	CHECK(per_event["breakdown"] == 2); /* seq 1, 4 */
	CHECK(per_event["drop"] == 2);      /* seq 2, 5 */
	CHECK(per_event["phrase"] > 0);
	CHECK(per_event["bar"] > per_event["phrase"]);
}

TEST_CASE("xplugin 8(c): the default map plans only /playlist/next and /playlist/previous, N=3")
{
	const cd::EventMap map = cd::EventMap::defaults();
	cd::EventGuard guard;
	std::set<std::string> routes;
	size_t planned = 0;
	for (int i = 0; i < 3; ++i) {
		cd::EventEngine eng(map);
		cd::SourceClaims claims;
		for (const cd::PlannedSend &p : drive(eng)) {
			++planned;
			routes.insert(p.route);
			(void)claims.may_send(guard, p.event, p.key);
		}
	}
	REQUIRE(planned > 0u);
	for (const std::string &r : routes)
		CHECK((r == "/playlist/next" || r == "/playlist/previous"));
}
