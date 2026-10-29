#pragma once
// Scene Director core: shared types. Pure C++17, no libobs/Qt.
#include <cstdint>

namespace cd {

// Mirrors Palette Director's sections (pd-clock-api.h PD_CLOCK_SECTION_*).
enum class Section { Groove = 0, Breakdown = 1, Drop = 2 };

// One clock reading from Palette Director (or the free-running fallback clock).
struct ClockSample {
	bool valid = false;
	double beats = 0.0; // beat position for this frame (already extrapolated + sync offset)
	double bpm = 128.0;
	Section section = Section::Groove;
	int phrase_beats = 32;
	bool paused = false;
};

} // namespace cd
