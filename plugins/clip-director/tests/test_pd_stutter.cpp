// Clip Director's stutter follows Palette Director's drop colour cycle (spec 4.4, 5).
//
// The drop colour cycle is a Palette Director signal, not a clock, so it is scripted here as
// `drop_cycle` / `drop_cycle_steps_per_beat` samples (no cross-plugin build). Checks, per cycle
// length and rate:
//   - one stutter flip per palette step of the cycle, each on its step beat
//   - stutter starts with the drop cut and ends when the cycle ends
//   - the drop clip cuts in on the section beat; no other cut fires under the stutter
#include "doctest.h"

#include <cmath>
#include <vector>

#include "scheduler.h"

using namespace cd;

namespace {

constexpr double kBpm = 128.0;

struct Run {
	std::vector<double> flips;    // at_beat of each StutterFlip
	std::vector<bool> flip_prev;  // which clip each flip shows
	double on = -1.0, off = -1.0; // tick beats the stutter turned on / off
	std::vector<double> cut_tick; // tick beat of every Cut
	std::vector<double> cut_beat; // at_beat of every Cut
	std::vector<int> cut_slot;
};

// groove 0..32, breakdown 32..64, drop from 64; the drop colour cycle runs from 64 for
// `cycle_beats` beats.
Run run(int cycle_beats, int spb)
{
	Run r;
	SchedulerConfig cfg;
	cfg.preroll_beats = 2.0;
	CutScheduler s(cfg);
	const double per_frame = kBpm / 60.0 / 60.0;
	const double limit = 64.0 + cycle_beats + 4.0;
	/* b = n * per_frame (not a running sum) so the cycle boundaries land exactly on frames */
	for (int n = 0;; ++n) {
		const double b = n * per_frame;
		if (b >= limit)
			break;
		ClockSample c;
		c.valid = true;
		c.beats = b;
		c.bpm = kBpm;
		if (b < 32.0) {
			c.section = Section::Groove;
			c.section_seq = 1;
			c.section_beat = 0;
		} else if (b < 64.0) {
			c.section = Section::Breakdown;
			c.section_seq = 2;
			c.section_beat = 32;
		} else {
			c.section = Section::Drop;
			c.section_seq = 3;
			c.section_beat = 64;
		}
		c.epoch = 1;
		c.drop_cycle = b >= 64.0 && b < 64.0 + cycle_beats;
		c.drop_cycle_steps_per_beat = spb;
		for (const auto &e : s.tick(c)) {
			using T = SchedEvent::Type;
			if (e.type == T::StutterOn)
				r.on = b;
			else if (e.type == T::StutterOff)
				r.off = b;
			else if (e.type == T::StutterFlip) {
				r.flips.push_back(e.at_beat);
				r.flip_prev.push_back(e.show_prev);
			} else if (e.type == T::Cut) {
				r.cut_tick.push_back(b);
				r.cut_beat.push_back(e.at_beat);
				r.cut_slot.push_back(e.slot);
			}
		}
	}
	return r;
}

} // namespace

TEST_CASE("stutter is locked to the drop colour cycle rate and length (scripted samples)")
{
	const struct {
		int cycle_beats, spb;
	} cases[] = {{4, 1}, {4, 2}, {4, 4}, {8, 1}, {8, 2}, {8, 4}};
	for (const auto &cs : cases) {
		INFO("cycle " << cs.cycle_beats << " beats, steps/beat " << cs.spb);
		const Run r = run(cs.cycle_beats, cs.spb);
		const double per_frame = kBpm / 60.0 / 60.0;

		// one flip per palette step, each on its exact step beat, alternating
		REQUIRE(static_cast<int>(r.flips.size()) == cs.cycle_beats * cs.spb);
		for (std::size_t k = 0; k < r.flips.size(); ++k) {
			CHECK(r.flips[k] == doctest::Approx(64.0 + static_cast<double>(k) / cs.spb));
			CHECK(r.flip_prev[k] == (k % 2 == 1));
		}

		// the stutter spans the cycle (within a frame)
		REQUIRE(r.on > 0.0);
		REQUIRE(r.off > 0.0);
		CHECK(std::fabs(r.on - 64.0) <= per_frame + 1e-9);
		CHECK(std::fabs(r.off - (64.0 + cs.cycle_beats)) <= per_frame + 1e-9);

		// the drop clip cuts in on the section beat, from the armed drop deck
		bool drop_cut = false;
		for (std::size_t k = 0; k < r.cut_beat.size(); ++k)
			drop_cut |= r.cut_slot[k] == CutScheduler::kDrop && r.cut_beat[k] == 64.0;
		CHECK(drop_cut);

		// ... and nothing else cuts while the stutter runs
		for (double t : r.cut_tick)
			CHECK_FALSE((t > r.on + 1e-9 && t < r.off - 1e-9));
	}
}
