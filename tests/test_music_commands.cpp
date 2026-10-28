#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "director.h"
#include "test_helpers.h"

/*
 * v1.2 Music dock commands: Phrase is now, Drop now, Force breakdown, Tap tempo, Lock BPM,
 * Nudge back / forward. All deterministic and WAV-fixture based, like the rest of the suite.
 */

namespace {

// Feed `beats` worth of digital silence: the beat clock keeps running at the fallback BPM.
void feed_silence_beats(pd::Director &d, double beats, double bpm = 128.0, uint32_t sr = 44100)
{
	const double secs = beats * 60.0 / bpm;
	const std::size_t n = static_cast<std::size_t>(secs * static_cast<double>(sr));
	pdtest::feed(d, std::vector<float>(n, 0.0f), sr);
}

bool has_detail(const std::vector<pd::Event> &evs, const std::string &needle)
{
	for (const auto &e : evs)
		if (e.detail.find(needle) != std::string::npos)
			return true;
	return false;
}

double beats_of(const pd::Director &d)
{
	const pd::DirectorState st = d.snapshot();
	return static_cast<double>(st.beat) + static_cast<double>(st.beat_frac);
}

} // namespace

TEST_CASE("v1.2 phrase is now re-anchors the phrase grid")
{
	pd::SetFile s = pdtest::make_set(2);
	s.director.phrase_beats = 16;
	s.director.random_off = false;
	pd::Director d(s);

	// get the clock running (8 bars at 128 BPM)
	pdtest::feed(d, pdtest::gen_kick(128.0, 8).to_float(), 44100);

	d.push_command(pd::Command::PhraseNow);
	pdtest::feed(d, std::vector<float>(256, 0.0f), 44100); // drain the command

	bool got = false;
	for (const auto &e : d.events())
		got = got || e.detail == "phrase re-anchored: phrase starts now";
	CHECK(got);

	const uint64_t pb = d.snapshot().phrase;
	feed_silence_beats(d, 10.0); // less than one phrase (16 beats): no tick
	CHECK(d.snapshot().phrase == pb);
	feed_silence_beats(d, 10.0); // now past the boundary: exactly one tick
	CHECK(d.snapshot().phrase == pb + 1);
}

TEST_CASE("v1.2 drop now forces section DROP and the scheduler reacts")
{
	pd::SetFile s = pdtest::make_set(3);
	s.director.random_off = false;
	s.palette_changes.trigger = pd::ChangeTrigger::Drop;
	pd::Director d(s);
	pdtest::feed(d, pdtest::gen_kick(128.0, 2).to_float(), 44100);
	REQUIRE(d.snapshot().section == pd::Section::Groove);

	d.push_command(pd::Command::DropNow);
	pdtest::feed(d, std::vector<float>(22050, 0.0f), 44100); // 0.5 s silence: drain + advance

	CHECK(d.snapshot().section == pd::Section::Drop);
	CHECK(has_detail(d.events(), "drop now"));
	CHECK(pdtest::section_beat(d.events(), pd::Section::Drop) >= 0);
	// a forced drop drives palette changes just like a detected one
	CHECK_FALSE(pdtest::palette_changes(d.events()).empty());
}

TEST_CASE("v1.2 force breakdown forces section BREAKDOWN")
{
	pd::SetFile s = pdtest::make_set(3, "Pal 2");
	s.director.random_off = false;
	pd::Director d(s);
	pdtest::feed(d, pdtest::gen_kick(128.0, 2).to_float(), 44100);
	REQUIRE(d.snapshot().section == pd::Section::Groove);

	d.push_command(pd::Command::ForceBreakdown);
	pdtest::feed(d, std::vector<float>(44100, 0.0f), 44100); // 1 s silence

	CHECK(d.snapshot().section == pd::Section::Breakdown);
	CHECK(has_detail(d.events(), "force breakdown"));
	CHECK(pdtest::section_beat(d.events(), pd::Section::Breakdown) >= 0);
	// the breakdown palette takes over (breakdown_palette = "Pal 2")
	CHECK_FALSE(pdtest::palette_changes(d.events()).empty());
}

TEST_CASE("v1.2 tap tempo sets the tempo from tap spacing")
{
	pd::SetFile s = pdtest::make_set(2);
	s.director.random_off = false;
	pd::Director d(s);
	pdtest::feed(d, std::vector<float>(44100, 0.0f), 44100); // ~1 s of clock

	// five taps 0.5 s apart -> 120 BPM
	for (int i = 0; i < 5; i++) {
		d.push_command(pd::Command::TapTempo);
		pdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);
	}
	CHECK(d.snapshot().bpm == doctest::Approx(120.0).epsilon(0.05));
	CHECK(has_detail(d.events(), "tap tempo -> 1"));
}

TEST_CASE("v1.2 lock bpm freezes and unfreezes tempo tracking")
{
	pd::SetFile s = pdtest::make_set(2);
	pd::Director d(s);
	(void)s;
	pdtest::feed(d, std::vector<float>(44100, 0.0f), 44100);
	for (int i = 0; i < 3; i++) { // tap to 120 BPM (a tap sets and holds the tempo)
		d.push_command(pd::Command::TapTempo);
		pdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);
	}
	REQUIRE(d.snapshot().bpm == doctest::Approx(120.0).epsilon(0.05));

	// a 150 BPM track must not move the clock while the tempo is held
	pdtest::feed_roundtrip(d, pdtest::gen_kick(150.0, 8), pdtest::fixture_dir(), "music-lockbpm.wav");
	CHECK(d.snapshot().bpm == doctest::Approx(120.0).epsilon(0.05));

	// unlock -> tracking resumes and follows the new tempo
	d.push_command(pd::Command::LockBpm);
	pdtest::feed_roundtrip(d, pdtest::gen_kick(150.0, 12), pdtest::fixture_dir(), "music-lockbpm2.wav");
	CHECK(d.snapshot().bpm > 135.0);
	CHECK(d.snapshot().bpm < 165.0);
	CHECK(has_detail(d.events(), "BPM unlocked"));
}

TEST_CASE("v1.2 nudge forward shifts the beat phase")
{
	pd::SetFile s = pdtest::make_set(2);
	s.director.random_off = false;
	pd::Director d(s);
	pdtest::feed(d, pdtest::gen_kick(128.0, 4).to_float(), 44100);

	const double b0 = beats_of(d);
	for (int i = 0; i < 4; i++)
		d.push_command(pd::Command::NudgeFwd); // +0.2 beats
	pdtest::feed(d, std::vector<float>(256, 0.0f), 44100);
	const double b1 = beats_of(d);
	CHECK(b1 - b0 > 0.15); // clearly beyond the ~0.012-beat advance the feed alone gives
	CHECK(b1 - b0 < 0.30);
	CHECK(has_detail(d.events(), "beat phase forward"));
}

TEST_CASE("v1.2 nudge back shifts the beat phase")
{
	pd::SetFile s = pdtest::make_set(2);
	s.director.random_off = false;
	pd::Director d(s);
	pdtest::feed(d, pdtest::gen_kick(128.0, 4).to_float(), 44100);

	const double b0 = beats_of(d);
	for (int i = 0; i < 8; i++)
		d.push_command(pd::Command::NudgeBack); // -0.4 beats
	pdtest::feed(d, std::vector<float>(256, 0.0f), 44100);
	const double b1 = beats_of(d);
	CHECK(b1 - b0 < -0.30);
	CHECK(b1 - b0 > -0.50);
	CHECK(has_detail(d.events(), "beat phase back"));
}

TEST_CASE("v1.2 bpm_locked in the snapshot follows every Lock BPM toggle (dock button state)")
{
	pd::SetFile s = pdtest::make_set(2);
	pd::Director d(s);
	pdtest::feed(d, std::vector<float>(44100, 0.0f), 44100);
	CHECK_FALSE(d.snapshot().bpm_locked);

	// the dock button, the hotkey and a MIDI mapping all arrive as the same queued command
	d.push_command(pd::Command::LockBpm);
	pdtest::feed(d, std::vector<float>(4410, 0.0f), 44100);
	CHECK(d.snapshot().bpm_locked);
	d.push_command(pd::Command::LockBpm);
	pdtest::feed(d, std::vector<float>(4410, 0.0f), 44100);
	CHECK_FALSE(d.snapshot().bpm_locked);

	// a tap sets and holds the tempo, so the button lights without a Lock press
	for (int i = 0; i < 3; i++) {
		d.push_command(pd::Command::TapTempo);
		pdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);
	}
	CHECK(d.snapshot().bpm_locked);
}
