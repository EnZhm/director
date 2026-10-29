#include "doctest.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "palette-director.h"
#include "test_helpers.h"

using namespace pdtest;

// The palette scheduler driven by scripted clock samples (spec 7). Palette Director is a
// follower: the same section/phrase/beat sequence PD 1.2.0's Director saw is replayed here
// through pd::PaletteDirector::on_clock().

TEST_CASE("random off at chance 1.0 fires on the next phrase and respects cooldown")
{
	pd::SetFile s = pdtest::make_set(1);
	s.director.phrase_beats = 32;
	s.director.random_off = true;
	s.director.random_chance = 1.0f;
	s.director.random_bars_min = 1;
	s.director.random_bars_max = 1; // deterministic 1-bar off
	s.director.cooldown_phrases = 2;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(160, 32)); // phrase boundaries at 32, 64, 96, 128

	const auto offs = pdtest::lock_offs(d.events());
	const auto ons = pdtest::lock_ons(d.events());

	std::vector<uint64_t> off_beats;
	for (const auto &e : offs)
		off_beats.push_back(e.beat);
	std::vector<uint64_t> on_beats;
	for (const auto &e : ons)
		on_beats.push_back(e.beat);

	// First off at the very next phrase boundary (beat 32), then a 1-bar (4-beat) off,
	// then the next off after the 2-phrase cooldown (beat 128).
	CHECK(off_beats == std::vector<uint64_t>({32, 128}));
	CHECK(on_beats == std::vector<uint64_t>({36, 132}));

	CHECK(d.snapshot().lock_on == true);
}

TEST_CASE("shuffle plays every normal palette once before any repeat; breakdown excluded")
{
	constexpr int kNormal = 3; // palettes 0,1,2; breakdown = index 3
	pd::SetFile s = pdtest::make_set(4, "Pal 3");
	s.palette_changes.order = pd::ChangeOrder::Shuffle;
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	s.director.seed = 7;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(256, 32)); // 8 phrase boundaries = 8 palette changes

	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() >= 2 * kNormal);

	std::vector<int> seq;
	for (const auto &e : pcs)
		seq.push_back(e.palette);

	for (int v : seq)
		CHECK(v != 3);

	for (std::size_t i = 0; i + kNormal <= seq.size(); ++i) {
		std::vector<int> window(seq.begin() + static_cast<std::ptrdiff_t>(i),
					seq.begin() + static_cast<std::ptrdiff_t>(i + kNormal));
		std::sort(window.begin(), window.end());
		CAPTURE(i);
		for (int p = 0; p < kNormal; ++p)
			CHECK(window[static_cast<std::size_t>(p)] == p);
	}
}

TEST_CASE("breakdown palette goes live on BREAKDOWN; next normal on DROP")
{
	pd::SetFile s = pdtest::make_set(4, "Pal 3"); // breakdown = index 3
	s.palette_changes.trigger = pd::ChangeTrigger::Drop;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.palette_changes.crossfade_beats = 2.0f;
	s.director.off_on_breakdown = false;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	// groove 8 bars -> breakdown 4 bars -> drop 8 bars; the drop lands on beat 48
	pdtest::run(d, pdtest::script(56, 32, {{0, 0}, {32, 1}, {48, 2}}));

	const auto evs = d.events();
	const int64_t bd = pdtest::section_beat(evs, pd::Section::Breakdown);
	const int64_t dp = pdtest::section_beat(evs, pd::Section::Drop);
	CHECK(bd == 32);
	CHECK(dp == 48);

	const auto pcs = pdtest::palette_changes(evs);
	bool saw_breakdown_live = false;
	bool saw_next_normal = false;
	for (const auto &e : pcs) {
		if (e.palette == 3 && std::llabs(static_cast<int64_t>(e.beat) - bd) <= 2)
			saw_breakdown_live = true;
		if (e.palette == 1 && std::llabs(static_cast<int64_t>(e.beat) - dp) <= 2)
			saw_next_normal = true;
	}
	CHECK(saw_breakdown_live);
	CHECK(saw_next_normal);

	CHECK(d.snapshot().pfade == doctest::Approx(1.0f).epsilon(0.001f));
	CHECK(d.snapshot().live_palette == 1);
}

TEST_CASE("trigger: Every N phrases changes in list order")
{
	pd::SetFile s = pdtest::make_set(3);
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 2;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(160, 32));

	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() == 2);
	CHECK(pcs[0].beat == 64);
	CHECK(pcs[0].palette == 1);
	CHECK(pcs[1].beat == 128);
	CHECK(pcs[1].palette == 2);
}

TEST_CASE("trigger: Random per phrase (chance 1.0) changes every phrase")
{
	pd::SetFile s = pdtest::make_set(3);
	s.palette_changes.trigger = pd::ChangeTrigger::Random;
	s.palette_changes.chance = 1.0f;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(160, 32));

	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() == 5); // boundaries at 32, 64, 96, 128, 160
	const int expect[] = {1, 2, 0, 1, 2};
	for (std::size_t i = 0; i < pcs.size(); ++i) {
		CAPTURE(i);
		CHECK(pcs[i].beat == 32 * (i + 1));
		CHECK(pcs[i].palette == expect[i]);
	}
}

TEST_CASE("trigger: Drop changes only on drops")
{
	pd::SetFile s = pdtest::make_set(3);
	s.palette_changes.trigger = pd::ChangeTrigger::Drop;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(56, 32, {{0, 0}, {32, 1}, {48, 2}}));

	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() == 1);
	CHECK(pcs[0].palette == 1);
	CHECK(pcs[0].beat == 48);
}

TEST_CASE("trigger: Manual makes no automatic changes")
{
	pd::SetFile s = pdtest::make_set(3);
	s.palette_changes.trigger = pd::ChangeTrigger::Manual;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	const auto samples = pdtest::script(40, 32);
	pdtest::run(d, samples);

	CHECK(pdtest::palette_changes(d.events()).empty());

	d.push_command(pd::Command::GoLive, 2);
	pdtest::run(d, samples); // drain
	CHECK(d.snapshot().live_palette == 2);
}

TEST_CASE("16-palette set: shuffle plays all 15 normal palettes before any repeat")
{
	constexpr int kNormal = 15; // palettes 0..14; breakdown = index 15
	pd::SetFile s = pdtest::make_set(16, "Pal 15");
	s.palette_changes.order = pd::ChangeOrder::Shuffle;
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.director.phrase_beats = 16;
	s.director.random_off = false;
	s.director.seed = 2077;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(544, 16)); // 34 phrases -> 34 changes

	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() >= 2 * kNormal);
	std::vector<int> seq;
	for (const auto &e : pcs)
		seq.push_back(e.palette);
	for (int v : seq)
		CHECK(v != 15);
	for (std::size_t i = 0; i + kNormal <= seq.size(); ++i) {
		std::vector<int> window(seq.begin() + static_cast<std::ptrdiff_t>(i),
					seq.begin() + static_cast<std::ptrdiff_t>(i + kNormal));
		std::sort(window.begin(), window.end());
		CAPTURE(i);
		for (int p = 0; p < kNormal; ++p)
			CHECK(window[static_cast<std::size_t>(p)] == p);
	}
}
