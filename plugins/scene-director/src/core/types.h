#pragma once
// Scene Director core: shared types. Pure C++17, no libobs/Qt.
#include <cstdint>

namespace cd {

// Mirrors Music Director's sections (director-clock.h DIRECTOR_CLOCK_SECTION_*).
enum class Section { Groove = 0, Breakdown = 1, Drop = 2 };

// One clock reading from Music Director (director_clock_read), mapped for the scheduler.
// `valid` is false while Music Director is not loaded: the scheduler then sends nothing
// (Scene Director idles; manual Next / Prev still work).
struct ClockSample {
	bool valid = false;
	double beats = 0.0; // beat position for this frame (already extrapolated + sync offset)
	double bpm = 128.0;
	Section section = Section::Groove;
	int phrase_beats = 32;
	bool paused = false;
	// The clock edge fields. A section change is acted on exactly once: when section_seq
	// differs from the last value the scheduler handled.
	int64_t section_seq = 0;     // +1 on EVERY section change, natural or forced
	int64_t section_beat = 0;    // the beat the current section started on (the landing beat)
	bool section_forced = false; // the section came from a command (Drop now / Force breakdown)
	int64_t epoch = 0;           // +1 when the beat grid is re-anchored
	int64_t beat = 0;            // integer beat (for the follower's beat/phrase resync)
	int64_t phrase = 0;          // phrase counter
};

} // namespace cd
