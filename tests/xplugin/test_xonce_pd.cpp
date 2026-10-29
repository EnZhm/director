// Exactly-once, Palette Director: the scripted clock drives pd::PaletteDirector (the follower core
// the Palette Lock worker runs). Every section_seq gives exactly one SectionChange, on section_beat.
#include <map>

#include "clock_script.h"
#include "doctest.h"
#include "palette-director/src/director/palette-director.h"

TEST_CASE("xplugin: Palette Director handles every section edge exactly once on section_beat")
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
		now += 1000000; // 1 ms per poll
		c.now_ns = now;
		d.on_clock(c);
	}

	std::map<int64_t, int> count; // section_beat -> SectionChange count
	std::map<int64_t, int> section;
	for (const pd::Event &e : d.events()) {
		if (e.type != pd::Event::Type::SectionChange)
			continue;
		count[static_cast<int64_t>(e.beat)]++;
		section[static_cast<int64_t>(e.beat)] = static_cast<int>(e.section);
	}
	CHECK(count.size() == xscript::edges().size());
	for (const xscript::Edge &e : xscript::edges()) {
		CAPTURE(e.seq);
		CHECK(count[e.section_beat] == 1);
		CHECK(section[e.section_beat] == e.section);
	}
}
