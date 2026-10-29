#pragma once
// Clip Director core: shared types. Pure C++17, no libobs/Qt.
#include <cstdint>
#include <string>
#include <vector>

namespace cd {

// Mirrors Music Director's sections (director-clock.h DIRECTOR_CLOCK_SECTION_*).
enum class Section { Groove = 0, Breakdown = 1, Drop = 2 };

// Folder section tag. Any = plays in every section's pool (see picker.h for the pool rules).
enum class Tag { Any = 0, Groove = 1, Breakdown = 2, Drop = 3 };

// Per-clip polarity override (spec 10). Auto = decided once per clip from ingest stats.
enum class Polarity { Auto = 0, Always = 1, Never = 2 };

// One clock reading from Music Director (built from struct director_clock). section_seq and
// section_beat come straight from the clock: the scheduler acts on a section change exactly
// once, when section_seq differs from the last value it handled, and lands on section_beat.
struct ClockSample {
	bool valid = false;
	double beats = 0.0; // beat position for this frame (already extrapolated + sync offset)
	double bpm = 128.0;
	Section section = Section::Groove;
	int phrase_beats = 32;
	int64_t section_seq = 0;     // +1 on every section change, natural or forced
	int64_t section_beat = 0;    // the beat the current section started on
	bool section_forced = false; // the section came from a command, not from detection
	int64_t epoch = 0;           // +1 when Music Director re-anchors the beat grid
	bool drop_cycle = false;     // Palette Director's drop colour cycle (stutter source)
	int drop_cycle_steps_per_beat = 2;
	bool paused = false;
};

// Ingest stats (spec 5), luma 0..1. has = false: unknown, the engine falls back to live levels.
struct ClipStats {
	bool has = false;
	double mean = 0.0, std = 0.0, p02 = 0.0, p50 = 0.0, p98 = 1.0, dark = 0.0;
};

struct Folder {
	std::string path;
	double weight = 1.0; // 0..10; 0 = never picked (like disabled, but keeps its place)
	Tag tag = Tag::Any;
	bool enabled = true;
};

struct Clip {
	std::string path;
	int folder = 0;          // index into the folder list
	double duration = 0.0;   // seconds
	double keyframe_s = 0.0; // keyframe interval in seconds, 0 = unknown (start points use a 1 s grid)
	ClipStats stats;
	bool banned = false;
	Polarity polarity = Polarity::Auto;
};

} // namespace cd
