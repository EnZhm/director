#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "palette-director.h"
#include "test_helpers.h"

using namespace pdtest;

// v1.0.1 drop colour cycle, now driven by scripted clock samples: on a DROP that follows a
// BREAKDOWN, maybe hard-cut through the normal palettes for N bars at 1/4, 1/8 or 1/16, then
// land on the palette the drop would have picked anyway.

namespace {
constexpr int kBd = 5; // breakdown palette index in the test set ("Pal 5")

pd::SetFile cycle_set(bool on, float chance = 1.0f, int bars = 1, int spb = 2, uint32_t seed = 7)
{
	pd::SetFile s = pdtest::make_set(6, "Pal 5");
	s.director.random_off = false;
	s.director.seed = seed;
	s.director.drop_cycle = on;
	s.director.drop_cycle_chance = chance;
	s.director.drop_cycle_bars = bars;
	s.director.drop_cycle_steps_per_beat = spb;
	return s;
}

// groove 8 bars (beats 0..31) -> breakdown 4 bars (32..47) -> drop 8 bars (48..79); the drop
// lands on beat 48. Sub-beat (1/4) resolution so on_tick drives the cycle's cuts.
std::vector<pd::ClockSample> track()
{
	return pdtest::script(80, 32, {{0, 0}, {32, 1}, {48, 2}}, 0.25);
}

std::vector<pd::Event> cycle_cuts(const std::vector<pd::Event> &evs)
{
	std::vector<pd::Event> out;
	for (const auto &e : evs)
		if (e.type == pd::Event::Type::PaletteChange && e.detail.find("drop cycle") != std::string::npos)
			out.push_back(e);
	return out;
}

bool has(const std::vector<pd::Event> &evs, const std::string &needle)
{
	for (const auto &e : evs)
		if (e.detail.find(needle) != std::string::npos)
			return true;
	return false;
}

// the palette a drop-after-breakdown picks with the cycle disabled (same seed, same clock)
int drop_pick(uint32_t seed)
{
	pd::PaletteDirector d(cycle_set(false, 1.0f, 1, 2, seed));
	pdtest::run(d, track());
	for (const auto &e : pdtest::palette_changes(d.events()))
		if (e.detail.find("drop after breakdown") != std::string::npos)
			return e.palette;
	return -1;
}
} // namespace

TEST_CASE("v1.0.1 drop cycle: exact cut count per rate and bars, lands on the drop pick")
{
	struct Case {
		int bars, spb, cuts;
	};
	for (const Case c : {Case{1, 2, 8}, Case{1, 1, 4}, Case{1, 4, 16}, Case{2, 2, 16}, Case{4, 1, 16}}) {
		INFO("bars " << c.bars << " steps/beat " << c.spb);
		pd::PaletteDirector d(cycle_set(true, 1.0f, c.bars, c.spb));
		pdtest::run(d, track());
		const auto evs = d.events();
		const auto cuts = cycle_cuts(evs);
		CHECK(static_cast<int>(cuts.size()) == c.cuts);
		CHECK(has(evs, "drop cycle (")); // the roll is logged
		const int64_t dp = pdtest::section_beat(evs, pd::Section::Drop);
		REQUIRE(dp == 48);
		// sub-beat steps: cut k lands in beat floor(k / spb) of the cycle
		for (std::size_t k = 0; k < cuts.size(); ++k)
			CHECK(static_cast<int64_t>(cuts[k].beat) == dp + static_cast<int64_t>(k) / c.spb);
		// never the breakdown palette, never the same palette twice in a row, all hard cuts
		for (std::size_t k = 0; k < cuts.size(); ++k) {
			CHECK(cuts[k].palette != kBd);
			CHECK(cuts[k].palette != cuts[k].prev_palette);
			CHECK(cuts[k].detail.find(", cut)") != std::string::npos);
		}
		// lands on the palette the drop would have chosen, with the normal crossfade, after N bars
		std::size_t over = evs.size();
		for (std::size_t i = 0; i < evs.size(); ++i)
			if (evs[i].detail.rfind("drop cycle over -> ", 0) == 0)
				over = i;
		REQUIRE(over + 1 < evs.size());
		const pd::Event &land = evs[over + 1];
		CHECK(land.type == pd::Event::Type::PaletteChange);
		CHECK(land.palette == drop_pick(7));
		CHECK(land.detail.find("drop after breakdown, 2-beat crossfade") != std::string::npos);
		CHECK(static_cast<int64_t>(land.beat) == dp + 4 * c.bars);
		CHECK_FALSE(d.snapshot().drop_cycle);
	}
}

TEST_CASE("v1.0.1 drop cycle: fires or not per seed and chance; lock held on during the cycle")
{
	int fired = 0, skipped = 0;
	for (uint32_t seed = 1; seed <= 12; ++seed) {
		pd::PaletteDirector d(cycle_set(true, 0.5f, 1, 2, seed));
		pdtest::run(d, track());
		const bool f = !cycle_cuts(d.events()).empty();
		CHECK(f == !has(d.events(), "no cycle"));
		if (!f) {
			// no cycle: the drop goes straight to its pick
			bool direct = false;
			for (const auto &e : pdtest::palette_changes(d.events()))
				direct = direct || e.detail.find("drop after breakdown") != std::string::npos;
			CHECK(direct);
		}
		(f ? fired : skipped)++;
	}
	CHECK(fired > 0);
	CHECK(skipped > 0);
	// same seed -> same decision
	pd::PaletteDirector a(cycle_set(true, 0.5f, 1, 2, 3)), b(cycle_set(true, 0.5f, 1, 2, 3));
	pdtest::run(a, track());
	pdtest::run(b, track());
	CHECK(pdtest::summarize(a.events()) == pdtest::summarize(b.events()));
	// chance 0 never fires, chance 1 always does
	pd::PaletteDirector z(cycle_set(true, 0.0f));
	pdtest::run(z, track());
	CHECK(cycle_cuts(z.events()).empty());

	// lock held on: random off at chance 1 + off on drop cannot take the lock away during the cycle
	pd::SetFile s = cycle_set(true);
	s.director.off_on_drop = true;
	s.director.drop_raw_bars = 2;
	pd::PaletteDirector d(s);
	bool seen_cycle = false;
	for (const auto &sample : track()) {
		d.on_clock(sample);
		if (d.snapshot().drop_cycle) {
			seen_cycle = true;
			CHECK(d.snapshot().mix_target == 1.0f);
		}
	}
	CHECK(seen_cycle);
	CHECK(has(d.events(), "drop cycle replaces the raw bars"));
	CHECK_FALSE(has(d.events(), "raw visuals"));
}

TEST_CASE("v1.0.1 drop cycle: off on drop behaves as before when the cycle does not fire")
{
	pd::SetFile s = cycle_set(true, 0.0f);
	s.director.off_on_drop = true;
	pd::PaletteDirector d(s);
	pdtest::run(d, track());
	CHECK(has(d.events(), "drop! raw visuals for 2 bars"));
	CHECK(cycle_cuts(d.events()).empty());
}

TEST_CASE("v1.0.1 drop cycle: cancelled by PANIC, pause, force off and manual picks (manual wins)")
{
	const auto samples = track();
	struct Case {
		pd::Command cmd;
		int idx;
		const char *why;
	};
	for (const Case c :
	     {Case{pd::Command::Panic, -1, "PANIC"}, Case{pd::Command::Pause, -1, "pause"},
	      Case{pd::Command::ForceOff, -1, "force off"}, Case{pd::Command::GoLive, 2, "manual pick wins"},
	      Case{pd::Command::Next, -1, "manual pick wins"}}) {
		INFO(c.why);
		pd::PaletteDirector d(cycle_set(true));
		std::size_t i = 0;
		for (; i < samples.size(); ++i) {
			d.on_clock(samples[i]);
			if (d.snapshot().drop_cycle)
				break;
		}
		REQUIRE(d.snapshot().drop_cycle);
		d.push_command(c.cmd, c.idx);
		d.on_clock(samples[++i]); // next sample drains the command
		CHECK_FALSE(d.snapshot().drop_cycle);
		CHECK(has(d.events(), std::string("drop cycle cancelled (") + c.why + ")"));
		const std::size_t cuts = cycle_cuts(d.events()).size();
		const int live = d.snapshot().live_palette;
		if (c.cmd == pd::Command::GoLive)
			CHECK(live == 2);
		for (++i; i < samples.size(); ++i)
			d.on_clock(samples[i]);
		CHECK(cycle_cuts(d.events()).size() == cuts); // no more cuts
		CHECK_FALSE(has(d.events(), "drop cycle over"));
	}
}

TEST_CASE("v1.0.1 drop cycle: manual-only trigger still cycles (lock behaviour), not on a plain drop")
{
	pd::SetFile s = cycle_set(true);
	s.palette_changes.trigger = pd::ChangeTrigger::Manual;
	pd::PaletteDirector d(s);
	pdtest::run(d, track());
	CHECK(cycle_cuts(d.events()).size() == 8u);
	// a drop with no breakdown before it (breakdown palette unset -> no bd_active_) never cycles
	pd::SetFile s2 = cycle_set(true);
	s2.palette_changes.breakdown_palette.clear();
	pd::PaletteDirector d2(s2);
	pdtest::run(d2, track());
	CHECK(cycle_cuts(d2.events()).empty());
	CHECK_FALSE(has(d2.events(), "drop cycle ("));
}

TEST_CASE("v1.0.1 drop cycle disabled: identical event log to v1.0.0 (no RNG roll)")
{
	// The same configuration with the behaviour off must replay exactly; and since the RNG is only
	// rolled when enabled, every later decision (random off, shuffle) is unchanged too.
	auto run = [](bool cycle_fields_set) {
		pd::SetFile s = pdtest::make_set(4, "Pal 3");
		s.palette_changes.trigger = pd::ChangeTrigger::Random;
		s.palette_changes.chance = 0.5f;
		s.palette_changes.order = pd::ChangeOrder::Shuffle;
		s.director.random_off = true;
		s.director.random_chance = 0.3f;
		s.director.seed = 12345;
		if (cycle_fields_set) { // disabled, but with non-default parameters
			s.director.drop_cycle = false;
			s.director.drop_cycle_chance = 1.0f;
			s.director.drop_cycle_bars = 4;
			s.director.drop_cycle_steps_per_beat = 4;
		}
		pd::PaletteDirector d(s);
		pdtest::run(d, pdtest::script(112, 32, {{0, 0}, {32, 1}, {48, 2}}, 0.5));
		return pdtest::summarize(d.events());
	};
	const std::string a = run(false), b = run(true);
	CHECK(a == b);
	CHECK(a.find("drop cycle") == std::string::npos);
}
