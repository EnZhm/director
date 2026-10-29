// One scripted Music Director clock, shared by the cross-plugin exactly-once tests
// (test_xonce_pd.cpp, test_xonce_cd.cpp, test_xonce_sd.cpp). Each follower is driven by the SAME
// sequence and must handle every section_seq exactly once, landing on section_beat.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace xscript {

struct Step {
	bool ok = true; // Music Director present
	double beats = 0.0;
	int section = 0; // 0 groove, 1 breakdown, 2 drop
	int64_t seq = 0;
	int64_t section_beat = 0;
	bool forced = false;
	int64_t epoch = 1;
	int phrase_beats = 32;
	int64_t beat() const { return static_cast<int64_t>(std::floor(beats + 1e-9)); }
	int64_t phrase() const { return beat() / phrase_beats; }
};

struct Edge {
	int64_t seq;
	int section;
	int64_t section_beat;
	bool forced;
};

// Timeline at 128 BPM, sampled 8x per beat and every sample polled 3 times (followers see
// repeated identical samples, as video_tick / the 5 ms worker do):
//   beat   0  groove (seq 0, start)
//   beat  32  natural BREAKDOWN    seq 1
//   beat  48  forced DROP          seq 2
//   beat  80  natural GROOVE       seq 3
//   beat  90  grid re-anchored     epoch 2 (no section change)
//   beat  96  forced BREAKDOWN     seq 4
//   beat 100-105  Music Director missing
//   beat 116  natural DROP         seq 5
//   beat 136  natural GROOVE       seq 6
inline const std::vector<Edge> &edges()
{
	static const std::vector<Edge> e = {
		{1, 1, 32, false}, {2, 2, 48, true},   {3, 0, 80, false},
		{4, 1, 96, true},  {5, 2, 116, false}, {6, 0, 136, false},
	};
	return e;
}

inline std::vector<Step> script()
{
	std::vector<Step> out;
	for (int i = 0; i <= 150 * 8; ++i) {
		Step s;
		s.beats = i / 8.0;
		s.epoch = s.beats >= 90.0 ? 2 : 1;
		s.ok = !(s.beats >= 100.0 && s.beats < 106.0);
		for (const Edge &e : edges()) {
			if (s.beats + 1e-9 >= static_cast<double>(e.section_beat)) {
				s.seq = e.seq;
				s.section = e.section;
				s.section_beat = e.section_beat;
				s.forced = e.forced;
			}
		}
		for (int k = 0; k < 3; ++k)
			out.push_back(s);
	}
	return out;
}

} // namespace xscript
