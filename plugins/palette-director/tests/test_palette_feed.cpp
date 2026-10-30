#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "director/palette-snapshot.h"
#include "palette-director.h"
#include "palette/palette.h"
#include "test_helpers.h"

using namespace pdtest;

/*
 * Spec 3.2: the palette feed. pd::palette_snapshot() is the pure formatter behind the proc's
 * `colours` string; pd::DirectorState::palette_seq / cycling are the counters the proc publishes
 * (seq +1 on every live palette change of the filter, including each drop-cycle step; cycling says
 * the last change came from one).
 */

namespace {

/* The pl_set half of the feed: n palettes, each with the built-in 4 colours. */
struct pl_set make_pl_set(int n_palettes)
{
	struct pl_set s;
	pl_set_default(&s);
	s.n_palettes = n_palettes;
	for (int i = 0; i < n_palettes; ++i) {
		char name[PL_NAME_MAX];
		std::snprintf(name, sizeof(name), "Pal %d", i);
		pl_palette_default(&s.palettes[i], name);
	}
	return s;
}

std::vector<std::string> split(const std::string &s, char sep)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) {
			out.push_back(cur);
			cur.clear();
		} else {
			cur += c;
		}
	}
	out.push_back(cur);
	return out;
}

/* "d.dddd": exactly four decimals, no sign, no exponent. */
bool is_4dp(const std::string &v)
{
	if (v.size() != 6 || v[1] != '.')
		return false;
	for (std::size_t i = 0; i < v.size(); ++i) {
		if (i == 1)
			continue;
		if (v[i] < '0' || v[i] > '9')
			return false;
	}
	return true;
}

std::size_t colour_count(const std::string &colours)
{
	return colours.empty() ? 0 : split(colours, ';').size();
}

// The drop colour cycle needs at least two normal palettes (test_drop_cycle.cpp drives the same
// set through the same scripted clock).
pd::SetFile cycle_set()
{
	pd::SetFile s = pdtest::make_set(6, "Pal 5");
	s.director.random_off = false;
	s.director.seed = 7;
	s.director.drop_cycle = true;
	s.director.drop_cycle_chance = 1.0f;
	s.director.drop_cycle_bars = 1;
	s.director.drop_cycle_steps_per_beat = 2;
	return s;
}

std::vector<pd::ClockSample> cycle_track()
{
	// groove 8 bars (0..31) -> breakdown 4 bars (32..47) -> drop lands on beat 48, cycle 4 bars
	return pdtest::script(80, 32, {{0, 0}, {32, 1}, {48, 2}}, 0.25);
}

} // namespace

TEST_CASE("palette_snapshot: one colour, four decimals per component")
{
	struct pl_set s = make_pl_set(1);
	s.palettes[0].n_colours = 1;
	s.palettes[0].colours[0] = {0.25f, 0.5f, 0.75f};

	CHECK(pd::palette_snapshot(s, 0) == "0.2500,0.5000,0.7500");
	CHECK_FALSE(pd::palette_snapshot(s, 0).find(';') != std::string::npos);
}

TEST_CASE("palette_snapshot: eight colours, four decimals each, in the palette's order")
{
	struct pl_set s = make_pl_set(1);
	s.palettes[0].n_colours = 8;
	for (int i = 0; i < 8; ++i)
		s.palettes[0].colours[i] = {static_cast<float>(i) / 8.0f, 0.5f, 1.0f};

	const std::string out = pd::palette_snapshot(s, 0);
	const std::vector<std::string> groups = split(out, ';');
	REQUIRE(groups.size() == 8);
	CHECK(groups.front() == "0.0000,0.5000,1.0000");
	CHECK(groups.back() == "0.8750,0.5000,1.0000");

	static const char *kExpectedR[8] = {"0.0000", "0.1250", "0.2500", "0.3750",
					    "0.5000", "0.6250", "0.7500", "0.8750"};
	for (std::size_t i = 0; i < groups.size(); ++i) {
		const std::vector<std::string> parts = split(groups[i], ',');
		REQUIRE(parts.size() == 3);
		for (const auto &p : parts)
			CHECK(is_4dp(p));
		CHECK(parts[0] == kExpectedR[i]); // dark -> bright: the stored order, never reordered
		CHECK(parts[1] == "0.5000");
		CHECK(parts[2] == "1.0000");
	}
}

TEST_CASE("palette_snapshot: out-of-range input is sanitised, not formatted")
{
	struct pl_set s = make_pl_set(3);

	// a colour count below 1 becomes the palette's first colour
	s.palettes[1].n_colours = 0;
	s.palettes[1].colours[0] = {0.5f, 0.25f, 0.125f};
	CHECK(pd::palette_snapshot(s, 1) == "0.5000,0.2500,0.1250");

	// a colour count above PL_MAX_COLOURS is clamped
	s.palettes[1].n_colours = 99;
	CHECK(colour_count(pd::palette_snapshot(s, 1)) == static_cast<std::size_t>(PL_MAX_COLOURS));

	// a live index outside the set is clamped into it
	s.palettes[2].n_colours = 1;
	s.palettes[2].colours[0] = {1.0f, 1.0f, 1.0f};
	CHECK(pd::palette_snapshot(s, 42) == "1.0000,1.0000,1.0000");
	// a negative live index clamps to palette 0 (its built-in 4 colours, black first)
	CHECK(colour_count(pd::palette_snapshot(s, -7)) == 4);
	CHECK(split(pd::palette_snapshot(s, -7), ';').front() == "0.0000,0.0000,0.0000");

	// colour components outside 0..1 are clamped to the feed's range
	s.palettes[0].n_colours = 1;
	s.palettes[0].colours[0] = {-0.5f, 1.5f, 0.0f};
	CHECK(pd::palette_snapshot(s, 0) == "0.0000,1.0000,0.0000");
}

TEST_CASE("palette_snapshot: an empty set falls back to the built-in palette")
{
	struct pl_set s = make_pl_set(1);
	s.n_palettes = 0;

	const std::string out = pd::palette_snapshot(s, 0);
	REQUIRE_FALSE(out.empty());
	CHECK(colour_count(out) == 4); // the built-in palette: black, grey, light grey, white
	for (const auto &g : split(out, ';'))
		CHECK(split(g, ',').size() == 3);
}

TEST_CASE("palette_colours clamps a palette that never went through the set sanitiser")
{
	struct pl_palette p;
	pl_palette_default(&p, "Pal");
	p.n_colours = 0;
	p.colours[0] = {0.0f, 0.1f, 1.0f};
	CHECK(pd::palette_colours(p) == "0.0000,0.1000,1.0000");

	p.n_colours = 12;
	CHECK(colour_count(pd::palette_colours(p)) == static_cast<std::size_t>(PL_MAX_COLOURS));
}

TEST_CASE("palette feed: seq counts every live palette change; cycling marks a drop-cycle step")
{
	pd::PaletteDirector d(cycle_set());
	const std::vector<pd::ClockSample> samples = cycle_track();

	uint64_t seq = d.snapshot().palette_seq;
	REQUIRE(seq == 0);
	int cycling_changes = 0;
	int plain_changes = 0;

	// Poll after every scripted sample, exactly as the host worker does every 5 ms, and count the
	// seq increments the feed would report.
	for (const auto &c : samples) {
		d.on_clock(c);
		const pd::DirectorState st = d.snapshot();
		while (seq < st.palette_seq) {
			++seq;
			if (st.cycling)
				++cycling_changes;
			else
				++plain_changes;
		}
	}

	int cut_events = 0;
	int other_changes = 0;
	for (const auto &e : d.events()) {
		if (e.type != pd::Event::Type::PaletteChange)
			continue;
		if (e.detail.find("drop cycle") != std::string::npos)
			++cut_events;
		else
			++other_changes;
	}

	REQUIRE(cut_events > 0);               // chance 1.0 with 5 normal palettes: the cycle runs
	CHECK(cycling_changes == cut_events);  // one seq per hard cut, flagged cycling
	CHECK(plain_changes == other_changes); // one seq per ordinary change, not flagged
	CHECK(seq == static_cast<uint64_t>(cut_events + other_changes));
	CHECK_FALSE(d.snapshot().cycling); // the cycle is over: the landing change was a crossfade
	CHECK_FALSE(d.snapshot().drop_cycle);
}

TEST_CASE("palette feed: manual go live and a forced live palette bump seq; a no-op pick does not")
{
	pd::SetFile s = pdtest::make_set(4);
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	const std::vector<pd::ClockSample> samples = pdtest::script(8, 32);

	pdtest::run(d, samples, 0, 4);
	REQUIRE(d.snapshot().palette_seq == 0);
	REQUIRE(d.snapshot().live_palette == 0);

	d.push_command(pd::Command::GoLive, 2);
	pdtest::run(d, samples, 4, 8);
	CHECK(d.snapshot().live_palette == 2);
	CHECK(d.snapshot().palette_seq == 1);
	CHECK_FALSE(d.snapshot().cycling);

	// going live on the palette that is already live is not a change
	d.push_command(pd::Command::GoLive, 2);
	pdtest::run(d, samples, 8, 12);
	CHECK(d.snapshot().palette_seq == 1);

	// a forced live palette (set restored / update_set) is a change too
	d.update_set(s, 3);
	pdtest::run(d, samples, 12, 16);
	CHECK(d.snapshot().live_palette == 3);
	CHECK(d.snapshot().palette_seq == 2);
	CHECK_FALSE(d.snapshot().cycling);
}
