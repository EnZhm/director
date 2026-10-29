#include "doctest.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

// v1.0.1 audio sensitivity: adaptive bass reference, input gain, kick sensitivity, meter.
// Moved from Palette Director 1.2.0's suite and run against the clock core.

namespace {
std::vector<float> scaled(const WavMono &w, double db)
{
	std::vector<float> v = w.to_float();
	const float g = static_cast<float>(std::pow(10.0, db / 20.0));
	for (float &x : v)
		x *= g;
	return v;
}

std::vector<float> noise(double seconds, double rms_db, uint32_t sr = 44100)
{
	std::vector<float> v(static_cast<std::size_t>(seconds * sr));
	uint32_t s = 12345u;
	const double amp = std::pow(10.0, rms_db / 20.0) * std::sqrt(3.0); // uniform: rms = a/sqrt(3)
	for (float &x : v) {
		s = s * 1664525u + 1013904223u;
		x = static_cast<float>(amp * ((static_cast<double>(s >> 8) / 16777216.0) * 2.0 - 1.0));
	}
	return v;
}

std::vector<float> sine(double seconds, double hz, double peak_db, uint32_t sr = 44100)
{
	std::vector<float> v(static_cast<std::size_t>(seconds * sr));
	const double a = std::pow(10.0, peak_db / 20.0);
	for (std::size_t i = 0; i < v.size(); ++i)
		v[i] = static_cast<float>(a *
					  std::sin(2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / sr));
	return v;
}

int64_t first_section_after(const std::vector<md::ClockEvent> &evs, md::Section sec, uint64_t after)
{
	for (const auto &e : evs)
		if (e.type == md::ClockEvent::Type::SectionChange && e.section == sec && e.beat >= after)
			return static_cast<int64_t>(e.beat);
	return -1;
}
} // namespace

// User report (VJ laptop): after one loud moment the all-time bass peak never decayed, normal kicks never
// cleared 0.4 x peak again and the Director sat in BREAKDOWN with no drop. Fails on v1.0.0 (no drop
// after the loud section at all); passes with the decaying reference.
TEST_CASE("v1.0.1 loud section, then normal-level music: breakdown and drop are still detected")
{
	md::MusicClock d(mdtest::make_cfg());
	const uint32_t sr = 44100;
	// 8 bars of full-scale kicks (the loud synthetic track), then normal-level music 15 dB down:
	// 32 bars groove -> 8 bars breakdown -> 8 bars drop.
	std::vector<float> a = mdtest::gen_kick(128.0, 8, sr).to_float();
	a.resize(static_cast<std::size_t>(32 * 60.0 / 128.0 * sr)); // exactly 32 beats
	const std::vector<float> b = scaled(mdtest::gen_breakdown(128.0, 32, 8, 8, sr), -15.0);
	std::vector<float> all = a;
	all.insert(all.end(), b.begin(), b.end());
	mdtest::feed(d, all, sr);

	const uint64_t bd_start = 32 + 128; // beat where the real breakdown starts
	const uint64_t drop_at = 32 + 160;  // beat where the real drop lands
	const int64_t bd = first_section_after(d.events(), md::Section::Breakdown, bd_start);
	const int64_t dp = first_section_after(d.events(), md::Section::Drop, bd_start);
	CHECK(bd > static_cast<int64_t>(bd_start));
	CHECK(bd <= static_cast<int64_t>(bd_start + 9));
	CHECK(dp >= static_cast<int64_t>(drop_at - 1));
	CHECK(dp <= static_cast<int64_t>(drop_at + 1));
	// the breakdown and the drop are two section changes on the clock
	CHECK(d.snapshot().section_seq >= 2);
}

// kRefRelease = 10 s: after a section 15 dB louder (31.6x the bass energy) the reference has to fall
// ~12.6x before default-sensitivity kicks count: ln(12.6) x 10 s = ~25 s = ~14 bars at 128 BPM.
TEST_CASE("v1.0.1 the reference recovers from a 15 dB louder section within 16 bars")
{
	md::MusicClock d(mdtest::make_cfg());
	const uint32_t sr = 44100;
	std::vector<float> a = mdtest::gen_kick(128.0, 8, sr).to_float();
	a.resize(static_cast<std::size_t>(32 * 60.0 / 128.0 * sr));
	mdtest::feed(d, a, sr);
	const std::vector<float> q = scaled(mdtest::gen_kick(128.0, 32, sr), -15.0);
	const std::size_t bar = static_cast<std::size_t>(4 * 60.0 / 128.0 * sr);
	int first_bar = -1;
	for (int i = 0; i < 32 && first_bar < 0; ++i) {
		const uint32_t k0 = d.snapshot().kicks;
		mdtest::feed(d, std::vector<float>(q.begin() + i * bar, q.begin() + (i + 1) * bar), sr);
		if (d.snapshot().kicks - k0 >= 3u)
			first_bar = i;
	}
	MESSAGE("quiet kicks count again from bar ", first_bar + 1);
	CHECK(first_bar >= 0);
	CHECK(first_bar <= 15);
}

TEST_CASE("v1.0.1 a long breakdown (32 bars, pad only) does not turn into a false drop")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_breakdown(128.0, 8, 32, 4).to_float(), 44100);
	const int64_t dp = mdtest::section_beat(d.events(), md::Section::Drop);
	CHECK(dp >= 159); // drop returns on beat 160
	CHECK(dp <= 161);
}

TEST_CASE("v1.0.1 kick sensitivity and input gain mappings")
{
	CHECK(md::kick_threshold_for(0.6f) == doctest::Approx(0.40f)); // = v1.0.0's fixed 0.4 x reference
	CHECK(md::kick_threshold_for(0.0f) == doctest::Approx(0.85f));
	CHECK(md::kick_threshold_for(1.0f) == doctest::Approx(0.10f));
	CHECK(md::kick_threshold_for(-1.0f) == doctest::Approx(0.85f)); // clamped
	CHECK(md::kick_threshold_for(2.0f) == doctest::Approx(0.10f));
	for (int i = 0; i < 100; ++i) // higher = more sensitive = lower threshold
		CHECK(md::kick_threshold_for((i + 1) / 100.0f) < md::kick_threshold_for(i / 100.0f));

	CHECK(md::input_gain_linear(0.0f) == doctest::Approx(1.0f));
	CHECK(md::input_gain_linear(6.0f) == doctest::Approx(1.9953f).epsilon(0.001));
	CHECK(md::input_gain_linear(-12.0f) == doctest::Approx(0.2512f).epsilon(0.001));
	CHECK(md::input_gain_linear(24.0f) == doctest::Approx(15.849f).epsilon(0.001));
	CHECK(md::input_gain_linear(40.0f) == doctest::Approx(15.849f).epsilon(0.001)); // clamped to +24
	CHECK(md::input_gain_linear(-40.0f) == doctest::Approx(0.2512f).epsilon(0.001));

	md::ClockConfig c;
	CHECK(c.input_gain_db == 0.0f);
	CHECK(c.kick_sensitivity == doctest::Approx(0.6f));
	md::MusicClock d(c);
	CHECK(d.snapshot().kick_threshold == doctest::Approx(0.4f));
}

TEST_CASE("v1.0.1 the meter shows the level after the input gain, and clips above -1 dBFS")
{
	md::ClockConfig c;
	{
		md::MusicClock d(c);
		mdtest::feed(d, sine(1.0, 440.0, -20.0), 44100);
		CHECK(d.snapshot().input_db == doctest::Approx(-20.0f).epsilon(0.02));
		CHECK_FALSE(d.snapshot().clip);
	}
	c.input_gain_db = 12.0f;
	{
		md::MusicClock d(c);
		mdtest::feed(d, sine(1.0, 440.0, -20.0), 44100);
		CHECK(d.snapshot().input_db == doctest::Approx(-8.0f).epsilon(0.03));
		CHECK_FALSE(d.snapshot().clip);
	}
	{
		md::MusicClock d(c);
		mdtest::feed(d, sine(1.0, 440.0, -6.0), 44100); // +12 dB -> +6 dBFS
		CHECK(d.snapshot().clip);
	}
	// the gain is live: apply_config changes it without resetting the clock
	c.input_gain_db = 0.0f;
	md::MusicClock d(c);
	mdtest::feed(d, sine(0.5, 440.0, -20.0), 44100);
	c.input_gain_db = -12.0f;
	d.apply_config(c);
	mdtest::feed(d, sine(1.5, 440.0, -20.0), 44100);
	CHECK(d.snapshot().input_db == doctest::Approx(-32.0f).epsilon(0.03));
}

TEST_CASE("v1.0.1 near-silent feeds at +24 dB gain stay silent: no kicks, no decisions")
{
	md::ClockConfig c;
	c.input_gain_db = 24.0f;
	{
		// -90 dBFS RMS hiss (an idle line input) -> -66 dBFS after +24 dB: below the silence floor
		md::MusicClock d(c);
		mdtest::feed(d, noise(20.0, -90.0), 44100);
		CHECK(d.snapshot().silent);
		CHECK(d.snapshot().kicks == 0u);
		CHECK(mdtest::section_changes(d.events()).empty());
		CHECK(d.snapshot().bpm == doctest::Approx(128.0f));
	}
	{
		// -80 dBFS hiss -> -56 dBFS: not silent any more, but its bass is under the kick floor
		md::MusicClock d(c);
		mdtest::feed(d, noise(20.0, -80.0), 44100);
		CHECK(d.snapshot().kicks == 0u);
		CHECK(mdtest::section_beat(d.events(), md::Section::Drop) < 0);
	}
}

TEST_CASE("v1.0.1 input gain lets a very quiet line feed through the silence floor")
{
	md::ClockConfig c;
	const std::vector<float> quiet = scaled(mdtest::gen_kick(128.0, 16), -62.0);
	md::MusicClock d0(c);
	mdtest::feed(d0, quiet, 44100);
	CHECK(d0.snapshot().kicks == 0u); // too quiet at 0 dB: treated as silence
	c.input_gain_db = 24.0f;
	md::MusicClock d1(c);
	mdtest::feed(d1, quiet, 44100);
	CHECK(d1.snapshot().kicks > 40u);
	CHECK(d1.snapshot().confidence > 0.5f);
	CHECK(d1.snapshot().bpm == doctest::Approx(128.0f).epsilon(0.01));
}

TEST_CASE("v1.0.1 kick sensitivity decides whether softer kicks count")
{
	// 4 bars of kicks, then 4 bars of kicks 8 dB softer (bass energy x0.16 of the reference)
	const uint32_t sr = 44100;
	std::vector<float> a = mdtest::gen_kick(128.0, 4, sr).to_float();
	a.resize(static_cast<std::size_t>(16 * 60.0 / 128.0 * sr));
	const std::vector<float> soft = scaled(mdtest::gen_kick(128.0, 4, sr), -8.0);
	auto soft_kicks = [&](float sens) {
		md::ClockConfig c;
		c.kick_sensitivity = sens;
		md::MusicClock d(c);
		mdtest::feed(d, a, sr);
		const uint32_t k0 = d.snapshot().kicks;
		mdtest::feed(d, soft, sr);
		return d.snapshot().kicks - k0;
	};
	const uint32_t lo = soft_kicks(0.0f), def = soft_kicks(0.6f), hi = soft_kicks(1.0f);
	MESSAGE("soft kicks counted: sensitivity 0% ", lo, ", 60% ", def, ", 100% ", hi);
	CHECK(lo <= 2u);
	CHECK(def >= lo);
	CHECK(def <= 8u);
	CHECK(hi >= 14u);
	CHECK(hi > def + 6u);
}

TEST_CASE("v1.0.1 changing the audio source resets the reference: normal kicks count at once")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_kick(128.0, 4).to_float(), 44100); // loud
	d.reset_audio_reference();
	const uint32_t k0 = d.snapshot().kicks;
	mdtest::feed(d, scaled(mdtest::gen_kick(128.0, 2), -20.0), 44100); // 8 quiet kicks
	CHECK(d.snapshot().kicks - k0 >= 6u);
}
