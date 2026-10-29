#pragma once
#include <cstdint>

#include "clock_types.h"

namespace md {

// GROOVE / BREAKDOWN / DROP from bass presence (design 2 "Section state"):
//   BREAKDOWN: bass absent for >= 2 bars (8 beats)
//   DROP:      bass returns on a downbeat; lasts one phrase
//   GROOVE:    otherwise
class SectionState {
public:
	SectionState() = default;

	// Call once per beat. `bass_present` = a kick/bass hit occurred this beat.
	Section update(uint64_t beat, bool bass_present, int phrase_beats);

	// v1.2 Music dock: force a section now (a queue-fed command), keeping the internal
	// timers consistent so normal detection carries on from here (a forced DROP lasts one
	// phrase, as a detected one does; a forced BREAKDOWN waits for the drop as usual).
	void force(Section s, uint64_t beat)
	{
		cur_ = s;
		last_bass_beat_ = beat;
		drop_start_ = (s == Section::Drop) ? static_cast<int64_t>(beat) : -1;
	}

	Section current() const { return cur_; }
	// Silent feed: no information about bass, so don't let the breakdown timer run.
	void hold(uint64_t beat) { last_bass_beat_ = beat; }
	void reset()
	{
		cur_ = Section::Groove;
		last_bass_beat_ = 0;
		drop_start_ = -1;
	}

private:
	Section cur_ = Section::Groove;
	uint64_t last_bass_beat_ = 0;
	int64_t drop_start_ = -1;
};

} // namespace md
