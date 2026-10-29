#include "doctest.h"

#include <vector>

#include "palette-director.h"
#include "test_helpers.h"

using namespace pdtest;

// Host-facing follower-core API: live reconfiguration and event streaming.

TEST_CASE("update_set keeps the beat clock and can force the live palette")
{
	pd::SetFile s = pdtest::make_set(3);
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	const auto samples = pdtest::script(120, 32);

	pdtest::run(d, samples, 0, 40); // beats 0..39
	const uint64_t beat = d.snapshot().beat;
	CHECK(beat >= 30);

	s.colour.strength = 0.5f;
	d.update_set(s, 2);
	pdtest::run(d, samples, 40, samples.size());
	CHECK(d.snapshot().beat >= 40);
	CHECK(d.snapshot().live_palette == 2);
	CHECK(d.snapshot().prev_palette == -1);

	// shrinking the palette list clamps the live palette
	s.palettes.resize(2);
	d.update_set(s);
	pdtest::run(d, samples, 40, samples.size());
	CHECK(d.snapshot().live_palette == 1);
}

TEST_CASE("drain_events streams the decision log")
{
	pd::SetFile s = pdtest::make_set(3);
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	std::vector<pd::Event> out;
	d.drain_events(out);
	CHECK(out.empty());
	d.set_keep_history(false);
	d.push_command(pd::Command::GoLive, 2);
	pdtest::run(d, pdtest::script(4, 32));
	d.drain_events(out);
	REQUIRE(out.size() == 1);
	CHECK(out[0].detail == "palette -> Pal 2 (manual, 2-beat crossfade)");
	CHECK(d.events().empty());
	CHECK(d.snapshot().next_palette == 0);
}
