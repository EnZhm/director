#include "doctest.h"

#include <string>
#include <vector>

#include "palette-director.h"
#include "test_helpers.h"

using namespace pdtest;

// Follower test (spec 7): the Palette Director scheduler driven by scripted clock samples.
// Covers natural and forced sections, epoch changes, and Music Director lost/found. Every
// section_seq is handled exactly once, in PD 1.2.0's order (section, then phrase, then beat).

namespace {
pd::ClockSample mk(int64_t beat, int64_t phrase, int section, int64_t seq, int64_t section_beat, int64_t epoch,
		   double beats_now = -1.0, bool ok = true)
{
	pd::ClockSample c;
	c.ok = ok;
	c.beat = beat;
	c.phrase = phrase;
	c.section = section;
	c.section_seq = seq;
	c.section_beat = section_beat;
	c.epoch = epoch;
	c.beats_now = beats_now < 0.0 ? static_cast<double>(beat) : beats_now;
	c.bpm = 128.0;
	c.confidence = 0.8;
	c.bass = 0.5;
	c.silent = false;
	c.now_ns = 1000000000 + beat * 468750000;
	return c;
}
} // namespace

TEST_CASE("follower: natural sections handled exactly once, logged as 'section -> NAME'")
{
	pd::SetFile s = pdtest::make_set(4, "Pal 3");
	s.palette_changes.trigger = pd::ChangeTrigger::Drop;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pdtest::run(d, pdtest::script(48, 16, {{0, 0}, {16, 1}, {32, 2}}));

	const auto secs = pdtest::section_changes(d.events());
	REQUIRE(secs.size() == 2); // BREAKDOWN, DROP (the initial Groove is the sync sample)
	CHECK(secs[0].section == pd::Section::Breakdown);
	CHECK(secs[0].beat == 16);
	CHECK(secs[0].detail == "section -> BREAKDOWN");
	CHECK(secs[1].section == pd::Section::Drop);
	CHECK(secs[1].beat == 32);
	CHECK(secs[1].detail == "section -> DROP");

	// the breakdown palette went live on BREAKDOWN, the drop pick on DROP
	const auto pcs = pdtest::palette_changes(d.events());
	bool bd_live = false, drop_live = false;
	for (const auto &e : pcs) {
		if (e.palette == 3 && e.beat == 16)
			bd_live = true;
		if (e.palette == 1 && e.beat == 32)
			drop_live = true;
	}
	CHECK(bd_live);
	CHECK(drop_live);
}

TEST_CASE("follower: a forced section (section_seq jump on one beat) fires exactly one edge")
{
	pd::SetFile s = pdtest::make_set(4, "Pal 3");
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	std::vector<pd::ClockSample> v;
	// groove for beats 0..3, then a forced DROP on beat 4 (section_seq 0 -> 1, section_beat 4)
	for (int64_t b = 0; b <= 6; ++b) {
		if (b < 4)
			v.push_back(mk(b, 0, 0, 0, 0, 0));
		else
			v.push_back(mk(b, 0, 2, 1, 4, 0));
	}
	pdtest::run(d, v);

	const auto secs = pdtest::section_changes(d.events());
	REQUIRE(secs.size() == 1);
	CHECK(secs[0].section == pd::Section::Drop);
	CHECK(secs[0].beat == 4);
	// DROP after no breakdown: trigger Drop -> the normal next palette goes live once
	const auto pcs = pdtest::palette_changes(d.events());
	REQUIRE(pcs.size() == 1);
	CHECK(pcs[0].beat == 4);
}

TEST_CASE("follower: epoch change re-syncs without catch-up beat/phrase edges")
{
	pd::SetFile s = pdtest::make_set(4, "Pal 3");
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	std::vector<pd::ClockSample> v;
	for (int64_t b = 0; b <= 4; ++b)
		v.push_back(mk(b, b / 4, 0, 0, 0, 0));
	// epoch bumps at beat 9 (grid re-anchored); the beat counter jumps ahead too
	for (int64_t b = 9; b <= 13; ++b)
		v.push_back(mk(b, 5, 0, 0, 0, 1));
	pdtest::run(d, v);

	const auto pcs = pdtest::palette_changes(d.events());
	// exactly one phrase boundary seen before the epoch change (phrase 1 at beat 4); after the
	// epoch change there must be no catch-up edge for the phrase jump 1 -> 5
	REQUIRE(pcs.size() == 1);
	CHECK(pcs[0].beat == 4);
	// the beat/phrase trackers re-anchored: still following, no resync section reported
	CHECK(d.following());
	CHECK(pdtest::section_changes(d.events()).empty());
}

TEST_CASE("follower: Music Director lost idles, found resumes without spurious edges")
{
	pd::SetFile s = pdtest::make_set(4, "Pal 3");
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	pd::ClockSample good = mk(0, 0, 0, 0, 0, 0);
	d.on_clock(good);
	CHECK(d.following());

	// Music Director disappears: ok=false -> idle, no events
	pd::ClockSample lost = good;
	lost.ok = false;
	d.on_clock(lost);
	CHECK_FALSE(d.following());
	const std::size_t n = d.events().size();

	// while lost, a section change is NOT reported
	lost.section = 2;
	lost.section_seq = 5;
	lost.section_beat = 40;
	lost.beat = 40;
	d.on_clock(lost);
	CHECK(d.events().size() == n);

	// found again: sync only, no section edge for the current section_seq
	pd::ClockSample back = mk(40, 2, 2, 5, 40, 0);
	d.on_clock(back);
	CHECK(d.following());
	CHECK(d.events().size() == n);

	// the NEXT section change (seq 6) IS reported
	d.on_clock(mk(48, 3, 0, 6, 48, 0));
	const auto secs = pdtest::section_changes(d.events());
	REQUIRE(secs.size() == 1);
	CHECK(secs[0].section == pd::Section::Groove);
	CHECK(secs[0].beat == 48);
}
