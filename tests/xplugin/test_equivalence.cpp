// Equivalence: Music Director's clock core + Palette Director's follower core reproduce the
// palette decisions Palette Director 1.2.0 made on its own (audio analysis and palettes in one
// plugin). Golden: tests/xplugin/golden/pd-1.2.0-events.txt, generated once by PD 1.2.0's Director
// (commit 468e73f) on the same synthetic fixture: section, palette, lock-off and lock-on events
// as "<scenario> <type> <beat> <section> <palette> <prev>".
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "music-director/src/director/clock.h"
#include "music-director/tests/fixtures/gen_fixtures.h"
#include "palette-director/src/director/palette-director.h"

#ifndef XPLUGIN_GOLDEN_DIR
#error "XPLUGIN_GOLDEN_DIR must be defined by CMake"
#endif

namespace {

pd::SetFile make_set(int scenario)
{
	pd::SetFile s;
	s.name = "Test Set";
	for (int i = 0; i < 4; ++i) {
		pd::PaletteSpec p;
		p.name = "Pal " + std::to_string(i);
		p.colours = {{0.0f, 0.0f, 0.0f}, {0.3f, 0.3f, 0.3f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}};
		s.palettes.push_back(p);
	}
	s.palette_changes.breakdown_palette = "Pal 3";
	if (scenario == 0) {
		s.palette_changes.trigger = pd::ChangeTrigger::Random;
		s.palette_changes.chance = 0.5f;
		s.palette_changes.order = pd::ChangeOrder::Shuffle;
		s.director.random_off = true;
		s.director.random_chance = 0.3f;
		s.director.seed = 12345;
	} else {
		s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
		s.palette_changes.every_phrases = 2;
		s.director.random_off = false;
		s.director.drop_cycle = true;
		s.director.drop_cycle_chance = 1.0f;
		s.director.seed = 7;
	}
	return s;
}

std::vector<std::string> run(int scenario)
{
	const auto w = mdtest::gen_breakdown(128.0, 8, 4, 4);
	const std::vector<float> mono = w.to_float();
	md::ClockConfig cfg;
	cfg.phrase_beats = 32;
	cfg.fallback_bpm = 128.0f;
	md::MusicClock clock(cfg);
	pd::PaletteDirector pal(make_set(scenario));

	const std::size_t hop = 256;
	for (std::size_t i = 0; i + hop <= mono.size(); i += hop) {
		clock.push_hop(mono.data() + i, hop, w.sample_rate);
		const md::ClockState st = clock.snapshot();
		pd::ClockSample c;
		c.ok = true;
		c.beat = static_cast<int64_t>(st.beat);
		c.phrase = static_cast<int64_t>(st.phrase);
		c.phrase_beats = st.phrase_beats;
		c.section = static_cast<int>(st.section);
		c.section_seq = static_cast<int64_t>(st.section_seq);
		c.section_beat = static_cast<int64_t>(st.section_beat);
		c.epoch = static_cast<int64_t>(st.epoch);
		c.beats_now = static_cast<double>(st.beat) + st.beat_frac;
		c.bpm = st.bpm;
		c.confidence = st.confidence;
		c.bass = st.bass;
		c.silent = st.silent;
		c.paused = st.paused;
		c.now_ns = static_cast<int64_t>((i + hop) * 1000000000ull / w.sample_rate);
		pal.on_clock(c);
	}
	std::vector<std::string> out;
	const char *tag = scenario == 0 ? "A" : "B";
	for (const pd::Event &e : pal.events()) {
		if (e.type == pd::Event::Type::Command)
			continue;
		char buf[96];
		std::snprintf(buf, sizeof(buf), "%s %d %llu %d %d %d", tag, static_cast<int>(e.type),
			      static_cast<unsigned long long>(e.beat), static_cast<int>(e.section), e.palette,
			      e.prev_palette);
		out.push_back(buf);
	}
	return out;
}

} // namespace

TEST_CASE("xplugin: Music Director + Palette Director 2.0 == Palette Director 1.2.0 (golden)")
{
	std::ifstream f(std::string(XPLUGIN_GOLDEN_DIR) + "/pd-1.2.0-events.txt");
	REQUIRE(f.good());
	std::vector<std::string> golden;
	for (std::string line; std::getline(f, line);)
		if (!line.empty())
			golden.push_back(line);

	std::vector<std::string> got = run(0);
	const std::vector<std::string> b = run(1);
	got.insert(got.end(), b.begin(), b.end());

	std::ostringstream g, n;
	for (const auto &l : golden)
		g << l << '\n';
	for (const auto &l : got)
		n << l << '\n';
	CHECK(n.str() == g.str());
}
