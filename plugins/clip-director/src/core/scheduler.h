#pragma once
// Clip Director core: beat-synced cut scheduler (spec 8). Pure C++17, deterministic.
//
// The scheduler never touches clips or decks. It turns clock samples into events, and the engine
// (OBS layer) acts on them:
//   Prepare(slot, section, hold_s)  load a pick for `section` into `slot`, seek it, pause it
//   Cut(slot, transition, beats)    show `slot`, starting on the beat `at_beat`
//   StutterOn / StutterFlip / StutterOff   alternate between the live and the outgoing clip at
//                                   Palette Director's drop-cycle rate (no seeks needed)
//
// Three slots (decks), each loaded, seeked and paused ahead of time:
//   kNext       the next normal clip of the current section
//   kBreakdown  a Breakdown-pool clip, armed while the section is NOT Breakdown, so a breakdown
//               edge cuts at once on section_beat
//   kDrop       a Drop-pool clip, armed while the section is NOT Drop, so a drop edge cuts at
//               once on section_beat
//
// Timing rules:
//   - The clock is Music Director's (spec 2). A section change is acted on exactly once, when
//     section_seq differs from the last value handled; its landing beat is section_beat.
//   - A breakdown or drop edge cuts its armed deck at once, on section_beat (a breakdown
//     crossfade STARTS on that beat). When the armed deck was prepared less than `preroll_beats`
//     ago it is not ready: fall back to the next bar line (and log it).
//   - A return to GROOVE, and the cuts inside a section, land on bar lines (multiples of 4 beats
//     on the clock's absolute grid), each at least `preroll_beats` after its Prepare.
//   - A clock jump (Music Director reset or a big tempo correction) re-plans from the new position.
#include <cstdint>
#include <string>
#include <vector>

#include "rng.h"
#include "types.h"

namespace cd {

enum class Transition { Cut = 0, Crossfade = 1, Dip = 2 };

struct SectionRule {
	int hold_bars = 4;
	Transition transition = Transition::Cut;
	double transition_beats = 1.0; // Crossfade / Dip length
	double crossfade_chance = 0.0; // for Transition::Cut: chance per cut to crossfade instead
};

struct SchedulerConfig {
	SectionRule groove{4, Transition::Cut, 1.0, 0.25};
	SectionRule breakdown{8, Transition::Crossfade, 4.0, 0.0};
	SectionRule drop{2, Transition::Cut, 1.0, 0.0};
	bool cut_on_section_change = true;
	bool stutter = true;        // follow Palette Director's drop colour cycle
	double preroll_beats = 1.0; // the OBS source defaults to 2 (deck loads take ~0.3-0.5 s)
	uint32_t seed = 7;
};

struct SchedEvent {
	enum class Type { Prepare, Cut, StutterOn, StutterFlip, StutterOff };
	Type type = Type::Prepare;
	double at_beat = 0.0;
	int slot = 0;
	Section section = Section::Groove;
	double hold_s = 0.0; // Prepare: how long the clip will be shown (wall seconds)
	Transition transition = Transition::Cut;
	double transition_beats = 0.0;
	bool show_prev = false;    // StutterFlip: true = show the outgoing clip
	bool section_edge = false; // Cut: fired by a section change, landing on section_beat
	int64_t section_seq = 0;   // Cut: the section_seq the edge belongs to
	std::string reason;
};

class CutScheduler {
public:
	static constexpr int kNext = 0;
	static constexpr int kBreakdown = 1;
	static constexpr int kDrop = 2;
	static constexpr int kSlots = 3;

	explicit CutScheduler(const SchedulerConfig &cfg);

	std::vector<SchedEvent> tick(const ClockSample &clk);
	void set_config(const SchedulerConfig &cfg); // keeps RNG and plan
	void skip();                                 // manual: cut on the next beat (preroll permitting)
	void set_hold(bool hold);                    // freeze on the current clip, no cuts
	bool hold() const { return hold_; }
	double next_cut_beat() const { return next_cut_; }

	// Text log of every event so far (determinism oracle for the tests).
	const std::vector<std::string> &log() const { return log_; }

private:
	const SectionRule &rule(Section s) const;
	double hold_seconds(Section s, double bpm) const;
	double next_bar_at_least(double beat) const;
	void prepare(int slot, Section s, double at, double bpm, const char *why, std::vector<SchedEvent> &out);
	void emit(const SchedEvent &e, std::vector<SchedEvent> &out);
	// A section edge into `sec` at `beat` (= section_beat): cut the armed deck at once when it
	// was ready, else fall back to the next bar line.
	void section_cut(Section sec, double beat, double now, int64_t seq, std::vector<SchedEvent> &out);
	// (Re)arm the decks the new section needs: a Breakdown clip while NOT Breakdown, a Drop clip
	// while NOT Drop.
	void arm_for(Section sec, double at, double bpm, std::vector<SchedEvent> &out);
	double hold_from_section(double sb, Section sec, double now, double pre) const;

	SchedulerConfig cfg_;
	Mulberry32 rng_;
	bool started_ = false;
	Section section_ = Section::Groove;
	Section next_section_ = Section::Groove; // section the kNext slot was prepared for
	int64_t last_seq_ = 0;
	int64_t last_epoch_ = 0;
	double next_cut_ = 0.0;
	double next_prepared_at_ = 0.0;
	bool bd_armed_ = false;
	bool drop_armed_ = false;
	double bd_prepared_at_ = 0.0;
	double drop_prepared_at_ = 0.0;
	double last_beats_ = 0.0;
	bool hold_ = false;
	bool release_pending_ = false;
	bool skip_pending_ = false;
	bool stutter_on_ = false;
	long long stutter_step_ = -1;
	double stutter_start_ = 0.0;
	std::vector<std::string> log_;
};

} // namespace cd
