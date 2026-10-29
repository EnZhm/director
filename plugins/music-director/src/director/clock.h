#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "clock_types.h"
#include "command_queue.h"

namespace md {

// v1.2 Music dock: how far one Nudge back / forward shifts the beat phase, in beats.
constexpr double kNudgeStepBeats = 0.05;

// v1.0.1: kick threshold as a fraction of the adaptive bass reference.
// sensitivity 0 -> 0.85, 0.6 (default) -> 0.40 (= v1.0.0's fixed 0.4), 1 -> 0.10. Higher = more sensitive.
inline float kick_threshold_for(float sensitivity)
{
	const float s = sensitivity < 0.0f ? 0.0f : (sensitivity > 1.0f ? 1.0f : sensitivity);
	return 0.85f - 0.75f * s;
}

// v1.0.1: input gain in dB (clamped to -12..+24) -> linear amplitude factor.
float input_gain_linear(float db);

// The clock's configuration. These are the audio keys that live in the scene collection under
// "music_director"; the palette scheduling keys stayed in Palette Director.
struct ClockConfig {
	int phrase_beats = 32;         // 16 | 32
	float fallback_bpm = 128.0f;   // tempo while silent
	float input_gain_db = 0.0f;    // -12..+24 dB, applied to the mono downmix before analysis
	float kick_sensitivity = 0.6f; // 0..1 (shown 0-100%); see kick_threshold_for()
	bool enabled = true;           // Music Director enabled
};

// A per-hop snapshot of the clock: everything a follower or the dock needs. The values refer to the
// last analysed hop's sample; include/director-clock.h adds the wall-clock extrapolation (beats_now).
struct ClockState {
	float bpm = 0.0f;
	float confidence = 0.0f;
	float bass = 0.0f; // 0..1 bass envelope (breathing)
	Section section = Section::Groove;
	uint64_t beat = 0;
	uint64_t phrase = 0;
	double beat_frac = 0.0f; // fractional part of the beat clock (beat lights)
	int phrase_beats = 32;
	uint64_t section_seq = 0;    // +1 on EVERY section change, natural or forced
	uint64_t section_beat = 0;   // the beat the current section started on
	uint64_t epoch = 0;          // +1 whenever the beat grid is re-anchored
	uint32_t kicks = 0;          // detected kick count (the dock flashes its kick light on change)
	bool silent = true;          // feed is silent (fallback BPM in use)
	bool paused = false;         // clock held (Music Director disabled)
	bool enabled = true;         // Music Director enabled
	float input_db = -120.0f;    // peak level, dBFS (fast attack, ~300 ms fall)
	bool clip = false;           // peak above -1 dBFS within the last ~1.5 s
	float bass_level = 0.0f;     // bass envelope / adaptive reference (0..1)
	float kick_threshold = 0.4f; // the kick threshold on the same scale (marker on the bass bar)
	bool bpm_locked = false;     // tempo held (Lock BPM or a tap)
};

// The clock's edges, in Palette Director 1.2.0's per-beat order (section, then phrase, then beat).
// A follower (or the AUDIT card) replays them; `forced` marks a Drop now / Force breakdown section.
struct ClockEvent {
	enum class Type { SectionChange, Phrase, Beat, Command };
	Type type = Type::Command;
	uint64_t beat = 0;
	uint64_t phrase = 0;
	Section section = Section::Groove;
	bool forced = false;
	uint64_t section_seq = 0; // the clock's section_seq after this change (SectionChange only)
	std::string detail;       // human-readable reason (commands and section changes)
};

// The Music Director clock core. Pure C++17, no libobs/Qt.
//
// The audio analysis, beat tracker and section detector are moved from Palette Director 1.2.0
// unchanged, so the same audio gives the same beat grid and section decisions. On top of that the
// clock exposes the follower signals: section_seq (+1 on every section change), section_beat (the
// beat a section starts on) and epoch (+1 whenever the grid is re-anchored).
//
// Threading contract: push_audio runs on the worker thread and drains the command queue; snapshot()
// and events() are safe on any thread (plain struct copies); push_command is safe from the UI thread
// (SPSC queue).
class MusicClock {
public:
	explicit MusicClock(const ClockConfig & = ClockConfig{});
	~MusicClock();
	MusicClock(MusicClock &&) noexcept;
	MusicClock &operator=(MusicClock &&) noexcept;
	MusicClock(const MusicClock &) = delete;
	MusicClock &operator=(const MusicClock &) = delete;

	void push_audio(const float *mono, std::size_t frames, uint32_t sample_rate);
	// Push audio and return the events produced by it (the same stream drain_events yields). Feeding
	// exactly one 256-sample hop at a time and reading the state after every call is how the deep
	// tests and the AUDIT card drive the clock.
	std::vector<ClockEvent> push_hop(const float *hop, std::size_t frames, uint32_t sample_rate);

	ClockState snapshot() const;
	const std::vector<ClockEvent> &events() const;
	void drain_events(std::vector<ClockEvent> &out);
	void set_keep_history(bool keep);

	void push_command(Command cmd, int index = -1);

	// Live re-configuration (input gain / kick sensitivity / phrase length / fallback BPM applied
	// without resetting the grid), as PD 1.2.0's update_set did.
	void apply_config(const ClockConfig &);
	const ClockConfig &config() const;
	// v1.0.1: forget the adaptive bass reference + meters (audio source changed). The grid keeps
	// running (PD 1.2.0 did not reset it either); the epoch advances so followers re-sync.
	void reset_audio_reference();
	// Full reset: fresh grid; epoch + 1.
	void reset();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace md
