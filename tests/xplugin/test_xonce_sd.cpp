// Exactly-once, Scene Director: the scripted clock drives cd::SwitchScheduler (Scene Director's
// core). Every breakdown and drop edge sends exactly one Next, aimed at section_beat. The forced
// drop 16 beats after a breakdown is not swallowed by the cooldown.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <map>

#include "clock_script.h"
#include "doctest.h"
#include "scene-director/src/core/switch_sched.h"

TEST_CASE("xplugin: Scene Director sends one Next per breakdown/drop edge, on section_beat")
{
	cd::SwitchConfig cfg; // defaults: breakdown + drop triggers, 16-beat cooldown, no lead
	cd::SwitchScheduler s(cfg);

	std::map<int64_t, std::vector<cd::SwitchEvent>> nexts; // section_seq -> section-edge Nexts
	for (const xscript::Step &st : xscript::script()) {
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
		for (const cd::SwitchEvent &e : s.tick(c))
			if (e.section_edge)
				nexts[e.section_seq].push_back(e);
	}
	for (const xscript::Edge &e : xscript::edges()) {
		CAPTURE(e.seq);
		if (e.section == 0)
			continue;
		REQUIRE(nexts[e.seq].size() == 1);
		CHECK(nexts[e.seq][0].at_beat == doctest::Approx(static_cast<double>(e.section_beat)));
	}
}
