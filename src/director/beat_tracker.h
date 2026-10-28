#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>

namespace pd {

// BPM estimation (autocorrelation of the onset envelope, 90-180 BPM) plus a soft
// phase-lock that keeps the beat grid aligned to onsets without jumps.
class BeatTracker {
public:
	BeatTracker(uint32_t sample_rate, std::size_t hop, double fallback_bpm);

	// Feed one onset sample + silence flag; advances the beat grid by dt seconds.
	void process(double onset, bool silent, double dt);

	double bpm() const { return effective_bpm_; } // tempo the grid runs at
	double estimated_bpm() const { return estimated_bpm_; }
	double confidence() const { return confidence_; }
	double beats() const { return beats_; }
	void reset();
	void set_fallback_bpm(double bpm)
	{
		if (effective_bpm_ == fallback_bpm_)
			effective_bpm_ = bpm;
		fallback_bpm_ = bpm;
	}

	// v1.2 Music dock.
	// Record a tap at `now_s` (a monotonic clock the caller shares across taps, e.g. the
	// Director's integrated audio time). Two or more taps inside 3 s give a tempo; a tap
	// sets that tempo AND holds it (Lock BPM toggles the hold off).
	void tap(double now_s);
	double tapped_bpm() const { return tapped_bpm_; }
	bool have_tap() const { return have_tap_; }
	// Freeze / resume tempo tracking. Locking captures the tempo the grid is running at.
	void set_lock(bool locked);
	bool locked() const { return locked_; }
	// Shift the beat phase by `beats` (small step); the clock keeps running from there.
	void nudge(double beats);

private:
	double autocorr(std::size_t lag) const;

	uint32_t sample_rate_;
	std::size_t hop_;
	double env_rate_;
	double fallback_bpm_;

	double beats_ = 0.0;
	double estimated_bpm_ = 0.0;
	double confidence_ = 0.0;
	double effective_bpm_;
	bool have_lock_ = false;
	double locked_bpm_ = 0.0;

	std::deque<double> onset_buf_;
	std::size_t min_buf_ = 0;
	std::size_t max_buf_ = 0;
	std::size_t lag_min_ = 0;
	std::size_t lag_max_ = 0;
	double conf_threshold_ = 0.30;

	double onset_peak_ = 0.0;
	std::deque<double> recent_max_; // last ~2 s of onsets (activity gate)
	std::size_t recent_len_ = 0;

	// v1.2 Music dock: tap tempo + Lock BPM
	std::deque<double> taps_; // recent tap times (<= 3 s window, <= 8 taps)
	bool have_tap_ = false;
	double tapped_bpm_ = 0.0;
	bool locked_ = false;     // Lock BPM: freeze tempo tracking
	double frozen_bpm_ = 0.0; // tempo held while locked
};

} // namespace pd
