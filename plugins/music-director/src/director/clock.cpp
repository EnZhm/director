#include "clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "analysis.h"
#include "beat_tracker.h"
#include "section_state.h"

namespace md {

namespace {
constexpr std::size_t kHop = 256;
constexpr double kSilenceThreshold = 1e-6; // full-band mean-square energy
constexpr double kSilenceSmooth = 0.05;    // one-pole coeff for the silence detector

// v1.0.1 adaptive bass reference (identical to PD 1.2.0):
//  - ref_: instant attack, release time constant kRefRelease.
//  - lt_ : a slow follower of ref_ (kLtRelease); the reference never drops below kLtFloor x lt_.
//  - kAbsFloor: absolute floor (bass energy, ~ -70 dB) so silence or noise never looks like a kick.
//  - kContrast: a kick must also rise above the beat's own minimum (rejects steady drones).
constexpr double kRefRelease = 10.0; // s
constexpr double kLtRelease = 60.0;  // s
constexpr double kLtFloor = 0.1;
constexpr double kAbsFloor = 1e-7;
constexpr double kContrast = 1.25;
constexpr double kMeterFall = 0.3; // s, peak meter fall time
constexpr double kClipHold = 1.5;  // s

const char *section_name(Section s)
{
	switch (s) {
	case Section::Groove:
		return "GROOVE";
	case Section::Breakdown:
		return "BREAKDOWN";
	case Section::Drop:
		return "DROP";
	}
	return "?";
}
} // namespace

float input_gain_linear(float db)
{
	const float d = db < -12.0f ? -12.0f : (db > 24.0f ? 24.0f : db);
	return static_cast<float>(std::pow(10.0, static_cast<double>(d) / 20.0));
}

struct MusicClock::Impl {
	explicit Impl(const ClockConfig &c) : analysis_(44100, kHop), tracker_(44100, kHop, c.fallback_bpm)
	{
		apply(c);
	}

	// Full restart: a brand-new grid (section_seq / epoch back to 0 and an empty event log). Used by
	// the constructor and by reset().
	void apply(const ClockConfig &c)
	{
		cfg_ = c;
		phrase_beats_ = c.phrase_beats;
		if (phrase_beats_ < 1)
			phrase_beats_ = 32;

		section_.reset();
		log_.clear();
		out_.clear();
		pending_.clear();
		beat_bass_fast_max_ = 0.0;
		beat_bass_fast_min_ = -1.0;
		silence_smooth_ = 0.0;
		reset_reference();
		kicks_ = 0;
		gain_ = input_gain_linear(c.input_gain_db);
		kick_thr_ = kick_threshold_for(c.kick_sensitivity);
		last_phrase_ = 0;
		phrase_inited_ = false;
		cur_section_ = Section::Groove;
		sample_rate_ = 0;
		phrase_offset_ = 0;
		pending_section_ = -1;
		clock_s_ = 0.0;
		section_seq_ = 0;
		section_forced_ = false;
		section_beat_ = 0;
		epoch_ = 0;
		CommandMsg m;
		while (queue_.pop(m)) {
		}
	}

	// Live re-configuration: the grid keeps running.
	void update(const ClockConfig &c)
	{
		const bool fallback_changed = c.fallback_bpm != cfg_.fallback_bpm;
		cfg_ = c;
		phrase_beats_ = c.phrase_beats;
		if (phrase_beats_ < 1)
			phrase_beats_ = 32;
		if (fallback_changed)
			tracker_.set_fallback_bpm(c.fallback_bpm);
		gain_ = input_gain_linear(c.input_gain_db);
		kick_thr_ = kick_threshold_for(c.kick_sensitivity);
	}

	void reset_reference()
	{
		ref_ = 0.0;
		lt_ = 0.0;
		bass_fast_ = 0.0;
		meter_peak_ = 0.0;
		clip_hold_ = 0.0;
	}

	double ref_eff() const { return std::max({ref_, kLtFloor * lt_, kAbsFloor}); }

	uint64_t phrase_of(uint64_t beat) const
	{
		return (beat + phrase_offset_) / static_cast<uint64_t>(phrase_beats_);
	}

	// The beat the phrase containing `beat` started on (include/director-clock.h: bar lines are
	// phrase_beat + 4k). The grid re-anchors with phrase_offset_, so the start is phrase_beats_
	// before the offset. At the re-anchor beat itself this is that beat.
	uint64_t phrase_beat_of(uint64_t beat) const
	{
		const uint64_t start = phrase_of(beat) * static_cast<uint64_t>(phrase_beats_);
		return start > phrase_offset_ ? start - phrase_offset_ : 0;
	}

	void append(const ClockEvent &ev) { append_events({ev}); }

	void append_events(const std::vector<ClockEvent> &evs)
	{
		for (ClockEvent e : evs) {
			e.phrase = phrase_of(e.beat);
			if (drain_enabled_)
				out_.push_back(e);
			if (keep_history_)
				log_.push_back(std::move(e));
		}
	}

	void append_note(uint64_t beat, const std::string &detail)
	{
		ClockEvent e;
		e.type = ClockEvent::Type::Command;
		e.beat = beat;
		e.detail = detail;
		append(e);
	}

	void drain_commands()
	{
		CommandMsg m;
		while (queue_.pop(m)) {
			const uint64_t beat = static_cast<uint64_t>(std::floor(tracker_.beats()));
			switch (m.cmd) {
			case Command::PhraseNow: {
				const uint64_t pb = static_cast<uint64_t>(phrase_beats_);
				const uint64_t rem = beat % pb;
				phrase_offset_ = rem == 0 ? 0 : pb - rem;
				last_phrase_ = (beat + phrase_offset_) / pb;
				phrase_inited_ = true;
				epoch_++; // the phrase grid is re-anchored
				append_note(beat, "phrase re-anchored: phrase starts now");
				continue;
			}
			case Command::DropNow:
				pending_section_ = static_cast<int>(Section::Drop);
				append_note(beat, "drop now: section -> DROP on the next beat");
				continue;
			case Command::ForceBreakdown:
				pending_section_ = static_cast<int>(Section::Breakdown);
				append_note(beat, "force breakdown: section -> BREAKDOWN on the next beat");
				continue;
			case Command::TapTempo: {
				tracker_.tap(clock_s_);
				epoch_++; // a tap sets the tempo and re-anchors the grid
				char buf[48];
				std::snprintf(buf, sizeof(buf), "tap tempo -> %.1f BPM", tracker_.tapped_bpm());
				append_note(beat, buf);
				continue;
			}
			case Command::LockBpm: {
				const bool lk = !tracker_.locked();
				tracker_.set_lock(lk);
				epoch_++; // the tempo changes, so the grid is re-anchored
				append_note(beat, lk ? "BPM locked: tempo tracking frozen"
						     : "BPM unlocked: tempo tracking resumed");
				continue;
			}
			case Command::NudgeBack:
				tracker_.nudge(-kNudgeStepBeats);
				epoch_++;
				append_note(beat, "nudge: beat phase back one step");
				continue;
			case Command::NudgeFwd:
				tracker_.nudge(kNudgeStepBeats);
				epoch_++;
				append_note(beat, "nudge: beat phase forward one step");
				continue;
			}
		}
	}

	void on_beat(uint64_t b)
	{
		const double bass_fast_max = beat_bass_fast_max_;
		const double bass_fast_min = beat_bass_fast_min_ < 0.0 ? 0.0 : beat_bass_fast_min_;
		beat_bass_fast_max_ = 0.0;
		beat_bass_fast_min_ = -1.0;

		// A queued Drop now / Force breakdown lands here, on the next beat.
		if (pending_section_ >= 0) {
			const Section sec = static_cast<Section>(pending_section_);
			pending_section_ = -1;
			if (sec != cur_section_) {
				cur_section_ = sec;
				section_.force(sec, b);
				section_seq_++;
				section_forced_ = true;
				section_beat_ = b;
				ClockEvent e;
				e.type = ClockEvent::Type::SectionChange;
				e.beat = b;
				e.section = sec;
				e.forced = true;
				e.section_seq = section_seq_;
				e.detail = std::string("forced section -> ") + section_name(sec);
				append(e);
			}
		}

		const bool silent = silence_smooth_ < kSilenceThreshold;
		const bool kick = !silent && bass_fast_max > kAbsFloor &&
				  bass_fast_max > static_cast<double>(kick_thr_) * ref_eff() &&
				  bass_fast_max > kContrast * bass_fast_min;
		if (kick)
			kicks_++;

		if (silent)
			section_.hold(b);
		if (!silent) {
			const Section sec = section_.update(b, kick, phrase_beats_);
			if (sec != cur_section_) {
				cur_section_ = sec;
				section_seq_++;
				section_forced_ = false;
				section_beat_ = b;
				ClockEvent e;
				e.type = ClockEvent::Type::SectionChange;
				e.beat = b;
				e.section = sec;
				e.section_seq = section_seq_;
				e.detail = std::string("section -> ") + section_name(sec);
				append(e);
			}
		}

		const uint64_t ph = phrase_of(b);
		if (!phrase_inited_) {
			last_phrase_ = ph;
			phrase_inited_ = true;
		} else if (ph != last_phrase_) {
			last_phrase_ = ph;
			if (!silent) {
				ClockEvent e;
				e.type = ClockEvent::Type::Phrase;
				e.beat = b;
				e.phrase = ph;
				append(e);
			}
		}

		ClockEvent e;
		e.type = ClockEvent::Type::Beat;
		e.beat = b;
		append(e);
	}

	ClockState snapshot_state() const
	{
		ClockState st;
		const double beats = tracker_.beats();
		const uint64_t beat = static_cast<uint64_t>(std::floor(beats));
		st.bpm = static_cast<float>(tracker_.bpm());
		st.confidence = static_cast<float>(tracker_.confidence());
		const double ref = ref_eff();
		st.bass = static_cast<float>(std::min(1.0, bass_fast_ / ref));
		st.section = cur_section_;
		st.beat = beat;
		st.phrase = phrase_of(beat);
		st.phrase_beat = phrase_beat_of(beat);
		st.beat_frac = beats - std::floor(beats);
		st.phrase_beats = phrase_beats_;
		st.section_seq = section_seq_;
		st.section_forced = section_forced_;
		st.section_beat = section_beat_;
		st.epoch = epoch_;
		st.enabled = cfg_.enabled;
		st.paused = !cfg_.enabled;
		st.silent = silence_smooth_ < kSilenceThreshold;
		st.input_db = meter_peak_ > 1e-6 ? static_cast<float>(20.0 * std::log10(meter_peak_)) : -120.0f;
		st.clip = clip_hold_ > 0.0;
		st.bass_level = st.bass;
		st.kick_threshold = kick_thr_;
		st.kicks = kicks_;
		st.bpm_locked = tracker_.locked();
		return st;
	}

	ClockConfig cfg_;
	uint32_t sample_rate_ = 0;

	Analysis analysis_;
	BeatTracker tracker_;
	SectionState section_;
	CommandQueue queue_;
	std::vector<ClockEvent> log_;
	std::vector<ClockEvent> out_;
	bool keep_history_ = true;
	bool drain_enabled_ = false;

	std::vector<float> pending_;

	double beat_bass_fast_max_ = 0.0;
	double beat_bass_fast_min_ = -1.0;
	double silence_smooth_ = 0.0;
	double ref_ = 0.0;
	double lt_ = 0.0;
	double bass_fast_ = 0.0;
	double meter_peak_ = 0.0;
	double clip_hold_ = 0.0;
	uint32_t kicks_ = 0;
	float gain_ = 1.0f;
	float kick_thr_ = 0.4f;

	int phrase_beats_ = 32;
	uint64_t last_phrase_ = 0;
	bool phrase_inited_ = false;
	Section cur_section_ = Section::Groove;
	uint64_t phrase_offset_ = 0;
	int pending_section_ = -1;
	double clock_s_ = 0.0;
	uint64_t section_seq_ = 0;
	bool section_forced_ = false;
	uint64_t section_beat_ = 0;
	uint64_t epoch_ = 0;
};

MusicClock::MusicClock(const ClockConfig &c) : impl_(new Impl(c)) {}
MusicClock::~MusicClock() = default;
MusicClock::MusicClock(MusicClock &&) noexcept = default;
MusicClock &MusicClock::operator=(MusicClock &&) noexcept = default;

void MusicClock::push_audio(const float *mono, std::size_t frames, uint32_t sample_rate)
{
	if (!impl_ || frames == 0)
		return;
	Impl &d = *impl_;

	d.drain_commands();

	if (sample_rate != d.sample_rate_) {
		d.sample_rate_ = sample_rate;
		d.analysis_ = Analysis(sample_rate, kHop);
		d.tracker_ = BeatTracker(sample_rate, kHop, d.cfg_.fallback_bpm);
		d.pending_.clear();
	}

	if (d.gain_ == 1.0f) {
		d.pending_.insert(d.pending_.end(), mono, mono + frames);
	} else {
		const std::size_t at = d.pending_.size();
		d.pending_.resize(at + frames);
		for (std::size_t i = 0; i < frames; ++i)
			d.pending_[at + i] = mono[i] * d.gain_;
	}

	const double dt = static_cast<double>(kHop) / static_cast<double>(sample_rate);
	const double ref_rel = std::exp(-dt / kRefRelease);
	const double lt_coeff = 1.0 - std::exp(-dt / kLtRelease);
	const double meter_rel = std::exp(-dt / kMeterFall);
	std::size_t pos = 0;
	while (pos + kHop <= d.pending_.size()) {
		const float *base = d.pending_.data() + pos;
		const AnalysisResult ar = d.analysis_.process(base, kHop);
		d.clock_s_ += dt; // shared time base for tap tempo

		d.silence_smooth_ += (ar.energy.total - d.silence_smooth_) * kSilenceSmooth;
		const bool silent = d.silence_smooth_ < kSilenceThreshold;

		d.ref_ = std::max(d.ref_ * ref_rel, ar.bass_fast);
		d.lt_ += (d.ref_ - d.lt_) * lt_coeff;
		d.bass_fast_ = ar.bass_fast;
		d.beat_bass_fast_max_ = std::max(d.beat_bass_fast_max_, ar.bass_fast);
		d.beat_bass_fast_min_ = d.beat_bass_fast_min_ < 0.0 ? ar.bass_fast
								    : std::min(d.beat_bass_fast_min_, ar.bass_fast);

		float hop_peak = 0.0f;
		for (std::size_t i = 0; i < kHop; ++i)
			hop_peak = std::max(hop_peak, std::fabs(base[i]));
		d.meter_peak_ = std::max(d.meter_peak_ * meter_rel, static_cast<double>(hop_peak));
		if (hop_peak > 0.891f) // -1 dBFS
			d.clip_hold_ = kClipHold;
		else
			d.clip_hold_ = std::max(0.0, d.clip_hold_ - dt);

		const double before = d.tracker_.beats();
		d.tracker_.process(ar.onset, silent, dt);
		const double after = d.tracker_.beats();

		const int64_t b0 = static_cast<int64_t>(std::floor(before));
		const int64_t b1 = static_cast<int64_t>(std::floor(after));
		for (int64_t b = b0 + 1; b <= b1; ++b)
			d.on_beat(static_cast<uint64_t>(b));

		pos += kHop;
	}
	d.pending_.erase(d.pending_.begin(), d.pending_.begin() + static_cast<std::ptrdiff_t>(pos));
}

std::vector<ClockEvent> MusicClock::push_hop(const float *hop, std::size_t frames, uint32_t sample_rate)
{
	if (!impl_)
		return {};
	const bool prev_drain = impl_->drain_enabled_;
	impl_->drain_enabled_ = true; // collect just the events this hop produces
	push_audio(hop, frames, sample_rate);
	impl_->drain_enabled_ = prev_drain;
	std::vector<ClockEvent> evs;
	evs.swap(impl_->out_);
	return evs;
}

ClockState MusicClock::snapshot() const
{
	return impl_ ? impl_->snapshot_state() : ClockState{};
}

void MusicClock::push_command(Command cmd, int index)
{
	if (!impl_)
		return;
	impl_->queue_.push(CommandMsg{cmd, index});
}

void MusicClock::apply_config(const ClockConfig &c)
{
	if (impl_)
		impl_->update(c);
}

const ClockConfig &MusicClock::config() const
{
	static const ClockConfig kEmpty;
	return impl_ ? impl_->cfg_ : kEmpty;
}

void MusicClock::drain_events(std::vector<ClockEvent> &out)
{
	if (!impl_)
		return;
	impl_->drain_enabled_ = true;
	out.insert(out.end(), impl_->out_.begin(), impl_->out_.end());
	impl_->out_.clear();
}

void MusicClock::reset_audio_reference()
{
	if (impl_) {
		impl_->reset_reference();
		impl_->epoch_++; // a new audio source re-anchors the grid for followers
	}
}

void MusicClock::reset()
{
	if (!impl_)
		return;
	const uint64_t seq = impl_->section_seq_;
	const uint64_t epoch = impl_->epoch_;
	const Section prev = impl_->cur_section_;
	impl_->apply(impl_->cfg_);
	impl_->epoch_ = epoch + 1; // the grid is re-anchored
	if (prev != Section::Groove) {
		impl_->section_seq_ = seq + 1; // back to GROOVE is a section change too
		impl_->append_note(0, "reset: grid re-anchored, section -> GROOVE");
	}
}

void MusicClock::set_keep_history(bool keep)
{
	if (!impl_)
		return;
	impl_->keep_history_ = keep;
	if (!keep)
		impl_->log_.clear();
}

const std::vector<ClockEvent> &MusicClock::events() const
{
	static const std::vector<ClockEvent> kEmpty;
	return impl_ ? impl_->log_ : kEmpty;
}

} // namespace md
