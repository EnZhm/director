#include "doctest.h"

#include <cmath>
#include <string>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

// Phrase boundaries land on downbeats (beat 32 = 8 bars = a downbeat), and the beat grid tracks the
// actual audio across the whole file.
TEST_CASE("beat grid tracks audio; phrase boundaries on downbeats")
{
	md::MusicClock d(mdtest::make_cfg(32, 128.0f));
	const WavMono w = mdtest::gen_kick(128.0, 16); // 64 beats
	mdtest::feed_roundtrip(d, w, mdtest::fixture_dir(), "phrase-kick.wav");

	const md::ClockState st = d.snapshot();
	CHECK(st.beat >= 63);
	CHECK(st.beat <= 65);
	CHECK(st.phrase == 2); // boundaries at beat 32 and 64, both downbeats (32 % 4 == 0)
}

TEST_CASE("breakdown detected within 2 bars; DROP on the return downbeat")
{
	md::MusicClock d(mdtest::make_cfg(32, 128.0f));
	// groove 8 bars -> breakdown 4 bars -> drop 4 bars; breakdown starts at beat 32,
	// drop returns on the downbeat at beat 48.
	const WavMono w = mdtest::gen_breakdown(128.0, 8, 4, 4);
	mdtest::feed_roundtrip(d, w, mdtest::fixture_dir(), "sections.wav");

	const auto evs = d.events();
	const int64_t bd = mdtest::section_beat(evs, md::Section::Breakdown);
	const int64_t dp = mdtest::section_beat(evs, md::Section::Drop);

	CHECK(bd > 32);
	CHECK(bd <= 40); // within 2 bars (8 beats) of the breakdown start
	CHECK(dp >= 47);
	CHECK(dp <= 49); // on the return downbeat (beat 48)
	CHECK(dp > bd);
}
