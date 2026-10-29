#include "doctest.h"

#include <cmath>
#include <string>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

TEST_CASE("BPM estimate within +/-1 at 124/128/132/140")
{
	const double targets[] = {124.0, 128.0, 132.0, 140.0};
	const std::string dir = mdtest::fixture_dir();
	for (double bpm : targets) {
		CAPTURE(bpm);
		md::MusicClock d(mdtest::make_cfg(32, static_cast<float>(bpm)));
		const WavMono w = mdtest::gen_kick(bpm, 16);
		mdtest::feed_roundtrip(d, w, dir, "kick-" + std::to_string(static_cast<int>(bpm)) + ".wav");

		const md::ClockState st = d.snapshot();
		CAPTURE(st.bpm);
		CAPTURE(st.confidence);
		CHECK(std::abs(st.bpm - bpm) <= 1.0); // acceptance: BPM within +/-1
		CHECK(st.confidence > 0.3f);          // locked
	}
}
