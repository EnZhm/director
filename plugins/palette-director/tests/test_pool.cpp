#include "doctest.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "palette-director.h"
#include "test_helpers.h"

using namespace pdtest;

// Palette pool (spec 7.1): each palette is in or out of the automatic pool. Out-of-pool
// palettes are never chosen automatically, a manual pad press still plays them, and the
// guards (one-left / empty) hold the live palette without ever blanking.

namespace {

// A set with n palettes where the given indices are out of the automatic pool.
pd::SetFile pooled_set(int n, const std::vector<int> &out_of_pool, const std::string &breakdown = "")
{
	pd::SetFile s = pdtest::make_set(n, breakdown);
	for (int i : out_of_pool)
		if (i >= 0 && i < n)
			s.palettes[static_cast<std::size_t>(i)].in_pool = false;
	return s;
}

bool has(const std::vector<pd::Event> &evs, const std::string &needle)
{
	for (const auto &e : evs)
		if (e.detail.find(needle) != std::string::npos)
			return true;
	return false;
}

int count_has(const std::vector<pd::Event> &evs, const std::string &needle)
{
	int n = 0;
	for (const auto &e : evs)
		if (e.detail.find(needle) != std::string::npos)
			++n;
	return n;
}

// Drop-colour-cycle fixture (see test_drop_cycle.cpp): 6 palettes, breakdown = "Pal 5".
pd::SetFile cycle_set(bool on)
{
	pd::SetFile s = pdtest::make_set(6, "Pal 5");
	s.director.random_off = false;
	s.director.seed = 7;
	s.director.drop_cycle = on;
	s.director.drop_cycle_chance = 1.0f;
	s.director.drop_cycle_bars = 1;
	s.director.drop_cycle_steps_per_beat = 2;
	return s;
}

// groove 8 bars -> breakdown 4 bars -> drop; sub-beat resolution drives the cycle.
std::vector<pd::ClockSample> track()
{
	return pdtest::script(80, 32, {{0, 0}, {32, 1}, {48, 2}}, 0.25);
}

} // namespace

TEST_CASE("pool: out-of-pool palettes are never chosen automatically (long scripted run)")
{
	pd::SetFile s = pooled_set(6, {2, 4}, "Pal 5");
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.palette_changes.order = pd::ChangeOrder::Shuffle;
	s.director.phrase_beats = 16;
	s.director.random_off = false;
	s.director.seed = 7;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(512, 16)); // 32 phrase boundaries

	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() >= 20); // the pool is not empty: rotation is happening
	for (const auto &e : pcs) {
		CAPTURE(e.palette);
		CAPTURE(e.detail);
		CHECK(e.palette != 2);
		CHECK(e.palette != 4);
		CHECK(e.palette != 5); // breakdown palette is not part of rotation
	}
}

TEST_CASE("pool: exclusion also holds through breakdown, drop and the drop colour cycle")
{
	pd::SetFile s = cycle_set(true);
	s.palettes[2].in_pool = false;
	s.palettes[4].in_pool = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, track());

	bool saw_cycle = false, saw_bd = false;
	for (const auto &e : d.events()) {
		if (e.detail.find("drop cycle") != std::string::npos)
			saw_cycle = true;
		if (e.type == pd::Event::Type::PaletteChange && e.palette == 5)
			saw_bd = true; // the breakdown palette only appears on the breakdown edge
		if (e.type == pd::Event::Type::PaletteChange) {
			CAPTURE(e.palette);
			CAPTURE(e.detail);
			CHECK(e.palette != 2);
			CHECK(e.palette != 4);
		}
	}
	CHECK(saw_cycle);
	CHECK(saw_bd);
}

TEST_CASE("pool: Next / Prev step through in-pool palettes only")
{
	pd::SetFile s = pooled_set(4, {1, 3}); // in-pool = {0, 2}
	s.palette_changes.trigger = pd::ChangeTrigger::Manual;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(4, 32)); // establish following

	d.push_command(pd::Command::Next);
	pdtest::run(d, pdtest::script(4, 32));
	CHECK(d.snapshot().live_palette == 2); // skips 1

	d.push_command(pd::Command::Next);
	pdtest::run(d, pdtest::script(4, 32));
	CHECK(d.snapshot().live_palette == 0); // skips 3, wraps

	d.push_command(pd::Command::Prev);
	pdtest::run(d, pdtest::script(4, 32));
	CHECK(d.snapshot().live_palette == 2); // skips 1 and 3
}

TEST_CASE("pool: a pad press on an out-of-pool palette still plays and logs")
{
	pd::SetFile s = pooled_set(3, {2});
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(4, 32));

	d.push_command(pd::Command::GoLive, 2);
	pdtest::run(d, pdtest::script(4, 32));

	CHECK(d.snapshot().live_palette == 2);
	CHECK(has(d.events(), "manual 'Pal 2' (out of the automatic pool)"));
}

TEST_CASE("pool: one palette in the pool holds on it (no repeat; returns at the next change)")
{
	pd::SetFile s = pooled_set(3, {1, 2}); // only Pal 0 in the pool
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	pd::PaletteDirector d(s);

	// live starts on the only in-pool palette: rotation holds, no repeat change event
	pdtest::run(d, pdtest::script(160, 32));
	CHECK(pdtest::palette_changes(d.events()).empty());

	// move live to an out-of-pool palette by hand; the next automatic change returns to 0
	d.push_command(pd::Command::GoLive, 1);
	pdtest::run(d, pdtest::script(192, 32)); // extends past the first run: one more phrase edge
	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() == 2);
	CHECK(pcs[0].palette == 1); // the manual pick
	CHECK(pcs[1].palette == 0); // the next automatic change lands on the only in-pool palette
}

TEST_CASE("pool: empty pool holds the live palette and logs once per transition")
{
	pd::SetFile s = pooled_set(3, {0, 1, 2});
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	pd::PaletteDirector d(s);

	pdtest::run(d, pdtest::script(160, 32));                               // many phrase boundaries
	CHECK(pdtest::palette_changes(d.events()).empty());                    // holds
	CHECK(count_has(d.events(), "automatic pool is empty; holding") == 1); // once

	// return a palette to the pool -> one "back" log, then changes resume
	pd::SetFile s2 = pooled_set(3, {0, 1, 2});
	s2.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s2.palette_changes.every_phrases = 1;
	s2.palette_changes.order = pd::ChangeOrder::Sequence;
	s2.director.phrase_beats = 32;
	s2.director.random_off = false;
	pd::PaletteDirector d2(s2);
	pdtest::run(d2, pdtest::script(64, 32)); // enters the empty state (logged)
	CHECK(count_has(d2.events(), "automatic pool is empty; holding") == 1);

	s2.palettes[1].in_pool = true;
	d2.update_set(s2); // a palette returns to the pool; the live palette is untouched
	CHECK(d2.snapshot().live_palette == 0);

	pdtest::run(d2, pdtest::script(160, 32));
	CHECK(count_has(d2.events(), "automatic pool has a palette again") == 1);
	CHECK(pdtest::palette_changes(d2.events()).size() >= 1);
}

TEST_CASE("pool: toggling mid-crossfade changes nothing until the next automatic change")
{
	pd::SetFile s = pooled_set(3, {});
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.palette_changes.crossfade_beats = 16.0f;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	pd::PaletteDirector d(s);

	pdtest::run(d, pdtest::script(40, 32)); // change at beat 32, fade still running at 40
	CHECK(d.snapshot().live_palette == 1);
	CHECK(d.snapshot().prev_palette == 0);
	CHECK(d.snapshot().pfade < 1.0f);

	s.palettes[2].in_pool = false;
	d.update_set(s);                       // a toggle is not a structural reconfigure
	CHECK(d.snapshot().live_palette == 1); // live unchanged
	CHECK(d.snapshot().prev_palette == 0); // the crossfade is not cancelled
	CHECK(d.snapshot().pfade < 1.0f);      // and keeps running
}

TEST_CASE("pool: toggling mid-drop-cycle changes nothing until the cycle ends")
{
	pd::SetFile s = cycle_set(true);
	pd::PaletteDirector d(s);
	const auto samples = track();
	std::size_t i = 0;
	for (; i < samples.size(); ++i) {
		d.on_clock(samples[i]);
		if (d.snapshot().drop_cycle)
			break;
	}
	REQUIRE(d.snapshot().drop_cycle);
	const int live = d.snapshot().live_palette;

	s.palettes[0].in_pool = false;
	d.update_set(s); // a toggle must never cancel a running drop cycle
	CHECK(d.snapshot().drop_cycle);
	CHECK(d.snapshot().live_palette == live);

	for (++i; i < samples.size(); ++i)
		d.on_clock(samples[i]);
	CHECK_FALSE(d.snapshot().drop_cycle); // the cycle still lands
	CHECK(has(d.events(), "drop cycle over -> "));
}

TEST_CASE("pool: breakdown palette out of the pool holds the current palette and logs")
{
	pd::SetFile s = pooled_set(4, {3}, "Pal 3"); // the breakdown palette is out of the pool
	s.palette_changes.trigger = pd::ChangeTrigger::Drop;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.off_on_breakdown = false;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(56, 32, {{0, 0}, {32, 1}, {48, 2}}));

	// the breakdown edge must NOT switch to Pal 3; it holds and logs once per breakdown
	for (const auto &e : pdtest::palette_changes(d.events()))
		CHECK(e.palette != 3); // never switches to the out-of-pool breakdown palette
	CHECK(count_has(d.events(), "breakdown palette 'Pal 3' is out of the pool; holding") == 1);
}
