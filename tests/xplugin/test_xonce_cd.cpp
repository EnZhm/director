// Exactly-once, Clip Director: the scripted clock drives cd::CutScheduler. Every breakdown and
// drop edge gives exactly one section cut from its armed deck, on section_beat. (Its own
// executable: Clip and Scene Director both keep their core in namespace cd.)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <map>

#include "clock_script.h"
#include "doctest.h"
#include "clip-director/src/core/scheduler.h"

TEST_CASE("xplugin: Clip Director cuts once per breakdown/drop edge, on section_beat, from its armed deck")
{
	cd::SchedulerConfig cfg;
	cfg.preroll_beats = 2.0;
	cd::CutScheduler s(cfg);

	std::map<int64_t, std::vector<cd::SchedEvent>> cuts; // section_seq -> section cuts
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
		for (const cd::SchedEvent &e : s.tick(c))
			if (e.type == cd::SchedEvent::Type::Cut && e.section_seq > 0)
				cuts[e.section_seq].push_back(e);
	}
	for (const xscript::Edge &e : xscript::edges()) {
		CAPTURE(e.seq);
		if (e.section == 0) // return to groove: bar-line rule, not an armed-deck cut
			continue;
		REQUIRE(cuts[e.seq].size() == 1);
		const cd::SchedEvent &c = cuts[e.seq][0];
		CHECK(c.at_beat == doctest::Approx(static_cast<double>(e.section_beat)));
		CHECK_FALSE(c.section_fallback);
		CHECK(c.slot == (e.section == 1 ? cd::CutScheduler::kBreakdown : cd::CutScheduler::kDrop));
	}
}
