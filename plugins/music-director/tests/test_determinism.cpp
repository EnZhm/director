#include "doctest.h"

#include <string>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

TEST_CASE("same feed -> identical clock event log")
{
	const std::string dir = mdtest::fixture_dir();

	auto run = [&]() {
		md::MusicClock d(mdtest::make_cfg(32, 128.0f));
		mdtest::feed_roundtrip(d, mdtest::gen_breakdown(128.0, 8, 4, 4), dir, "det.wav");
		return d;
	};

	md::MusicClock a = run();
	md::MusicClock b = run();
	REQUIRE(a.events().size() == b.events().size());
	CHECK(mdtest::summarize(a.events()) == mdtest::summarize(b.events()));
}

// Golden guard: the clock's event log for a canonical feed must be byte-identical (compact FNV-1a
// digest captured from the 2.0.0 clock). No clock command is sent, so a follower replaying the log
// gets the same sections, phrases and beats as Palette Director 1.2.0's Director did.
TEST_CASE("v2.0.0 digest is stable for a canonical feed")
{
	const std::string dir = mdtest::fixture_dir();

	md::MusicClock a(mdtest::make_cfg(32, 128.0f));
	mdtest::feed_roundtrip(a, mdtest::gen_breakdown(128.0, 8, 4, 4), dir, "det.wav");
	REQUIRE_FALSE(a.events().empty());
	CHECK(mdtest::digest(a.events()) == 0x761dd0386e00aebaull);
}
