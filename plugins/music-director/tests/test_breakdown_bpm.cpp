#include "doctest.h"

#include <cmath>
#include <vector>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

// P4 regression (found in the OBS sandbox, kept for the clock core): during a breakdown the onset
// envelope is only numerical residue of the pad; its periodic pattern used to win the
// autocorrelation (181 BPM at conf 0.96), so the beat grid ran fast and the drop landed off the
// phrase grid.
TEST_CASE("breakdown with no bass holds the locked BPM and the drop lands on the beat grid")
{
	for (uint32_t sr : {44100u, 48000u}) {
		md::MusicClock d(mdtest::make_cfg(32, 128.0f));
		const WavMono w = mdtest::gen_breakdown(128.0, 32, 8, 16, sr);
		const std::vector<float> mono = w.to_float();
		const std::size_t bd_start = static_cast<std::size_t>(32 * 4 * 60.0 / 128.0 * sr);
		const std::size_t bd_end = static_cast<std::size_t>(40 * 4 * 60.0 / 128.0 * sr);
		float worst = 0.0f;
		for (std::size_t i = 0; i < mono.size(); i += 1024) {
			const std::size_t n = std::min<std::size_t>(1024, mono.size() - i);
			d.push_audio(mono.data() + i, n, sr);
			if (i > bd_start && i < bd_end)
				worst = std::max(worst, std::fabs(d.snapshot().bpm - 128.0f));
		}
		CHECK(worst <= 1.0f);
		const int64_t drop = mdtest::section_beat(d.events(), md::Section::Drop);
		CHECK(drop >= 159);
		CHECK(drop <= 161);
	}
}
