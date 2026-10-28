#include "doctest.h"

#include "director.h"
#include "test_helpers.h"

TEST_CASE("same seed -> identical event log")
{
	const std::string dir = pdtest::fixture_dir();

	auto run = [&]() {
		pd::SetFile s = pdtest::make_set(4, "Pal 3");
		s.palette_changes.trigger = pd::ChangeTrigger::Random;
		s.palette_changes.chance = 0.5f;
		s.palette_changes.order = pd::ChangeOrder::Shuffle;
		s.director.random_off = true;
		s.director.random_chance = 0.3f;
		s.director.seed = 12345;
		pd::Director d(s);
		pdtest::feed_roundtrip(d, pdtest::gen_breakdown(128.0, 8, 4, 4), dir, "det.wav");
		return d;
	};

	pd::Director a = run();
	pd::Director b = run();
	REQUIRE(a.events().size() == b.events().size());
	CHECK(pdtest::summarize(a.events()) == pdtest::summarize(b.events()));
}

// v1.2 guard: the event log for a canonical feed must be byte-identical (compact FNV-1a digest).
// The v1.2 clock commands (Phrase is now, Drop now, Force breakdown, Tap tempo, Lock BPM,
// Nudge) must not perturb the log at all when none of them is sent.
TEST_CASE("v1.2 digest unchanged when no v1.2 command is sent")
{
	const std::string dir = pdtest::fixture_dir();

	pd::SetFile s = pdtest::make_set(4, "Pal 3");
	s.palette_changes.trigger = pd::ChangeTrigger::Random;
	s.palette_changes.chance = 0.5f;
	s.palette_changes.order = pd::ChangeOrder::Shuffle;
	s.director.random_off = true;
	s.director.random_chance = 0.3f;
	s.director.seed = 12345;
	pd::Director a(s);
	pdtest::feed_roundtrip(a, pdtest::gen_breakdown(128.0, 8, 4, 4), dir, "det.wav");
	REQUIRE_FALSE(a.events().empty());
	CHECK(pdtest::digest(a.events()) == 0x916ccbc77a474d90ull);

	pd::SetFile s2 = pdtest::make_set(4, "Pal 3");
	s2.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s2.palette_changes.every_phrases = 2;
	s2.director.random_off = false;
	s2.director.drop_cycle = true;
	s2.director.drop_cycle_chance = 1.0f;
	s2.director.seed = 7;
	pd::Director b(s2);
	pdtest::feed_roundtrip(b, pdtest::gen_breakdown(128.0, 8, 4, 4), dir, "det2.wav");
	REQUIRE_FALSE(b.events().empty());
	CHECK(pdtest::digest(b.events()) == 0x433c32f7bd371a47ull);
}
