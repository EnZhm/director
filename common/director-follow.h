/*
director-follow.h -- the Director suite's follower edge detector (header-only, C++17, no libobs).
Lives in EnZhm/director common/director-follow.h; Palette, Clip and Scene Director include it from here.

Feed it every clock sample a follower reads (Sample is filled from struct director_clock, or from a
scripted sequence in tests). update() reports each musical edge exactly once:

  - section : section_seq changed since the last handled sample. Reported once per change, with the
              new section and section_beat, even if several samples arrive in the same beat.
  - phrase  : phrase counter advanced inside the same epoch. Jumps report one edge (the newest).
  - beat    : integer beat advanced inside the same epoch. Jumps report one edge (the newest).
  - resync  : first sample, Music Director (re)found, or epoch changed. Beat/phrase trackers are
              re-anchored WITHOUT firing beat/phrase edges. A section change that arrives together
              with a resync IS reported (section_seq is authoritative across epochs), except on the
              very first sample after start or after Music Director was found again.
  - lost / found : Music Director disappeared / appeared (ok flag). Each is reported once.

Same-beat order for a follower that handles several edges from one sample: section, then phrase,
then beat (the order Palette Director 1.2.0's Director used).
*/
#pragma once

#include <cstdint>

namespace director_follow {

enum class Section : int { Groove = 0, Breakdown = 1, Drop = 2 };

struct Sample {
	bool ok = false; // director_clock_read() succeeded
	int64_t beat = 0;
	int64_t phrase = 0;
	// The beat the current phrase started on (bar lines are phrase_beat + 4k). 0 from a Music
	// Director that predates the field, or from a caller that does not fill it: bars are then
	// beat % 4 == 0. Unused by the followers shipped with 2.1.
	int64_t phrase_beat = 0;
	int section = 0;
	int64_t section_seq = 0;
	int64_t section_beat = 0;
	bool section_forced = false;
	int64_t epoch = 0;
};

struct Edges {
	bool lost = false;
	bool found = false;
	bool resync = false;
	bool section = false;
	Section new_section = Section::Groove;
	int64_t section_beat = 0;
	int64_t section_seq = 0;
	bool section_forced = false; // the new section came from a command, not detection
	bool phrase = false;
	int64_t phrase_no = 0;
	bool beat = false;
	int64_t beat_no = 0;

	bool any() const { return lost || found || resync || section || phrase || beat; }
};

class Follower {
public:
	Edges update(const Sample &s)
	{
		Edges e;
		if (!s.ok) {
			// Music Director missing: report once per loss (also on the very first call)
			e.lost = state_ != State::Lost;
			state_ = State::Lost;
			return e;
		}
		if (state_ != State::Following) {
			// first sample ever, or Music Director found again: sync only
			e.found = state_ == State::Lost;
			e.resync = true;
			state_ = State::Following;
			sync(s);
			return e;
		}
		if (s.section_seq != seq_) {
			e.section = true;
			e.new_section = static_cast<Section>(s.section);
			e.section_beat = s.section_beat;
			e.section_seq = s.section_seq;
			e.section_forced = s.section_forced;
			seq_ = s.section_seq;
		}
		if (s.epoch != epoch_) {
			e.resync = true;
			epoch_ = s.epoch;
			beat_ = s.beat;
			phrase_ = s.phrase;
			return e;
		}
		if (s.phrase > phrase_) {
			e.phrase = true;
			e.phrase_no = s.phrase;
			phrase_ = s.phrase;
		}
		if (s.beat > beat_) {
			e.beat = true;
			e.beat_no = s.beat;
			beat_ = s.beat;
		}
		return e;
	}

	bool following() const { return state_ == State::Following; }
	int64_t handled_section_seq() const { return seq_; }

private:
	void sync(const Sample &s)
	{
		seq_ = s.section_seq;
		epoch_ = s.epoch;
		beat_ = s.beat;
		phrase_ = s.phrase;
	}

	enum class State { Start, Following, Lost };
	State state_ = State::Start;
	int64_t seq_ = 0;
	int64_t epoch_ = 0;
	int64_t beat_ = 0;
	int64_t phrase_ = 0;
};

} // namespace director_follow
