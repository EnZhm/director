#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "clock.h"
#include "test_helpers.h"

/*
 * v1.2 Music dock commands, moved from Palette Director 1.2.0 and run against the clock core:
 * Phrase is now, Drop now, Force breakdown, Tap tempo, Lock BPM, Nudge back / forward.
 * All deterministic and WAV-fixture based, like the rest of the suite.
 */

using namespace mdtest;

namespace {

// Feed `beats` worth of digital silence: the beat clock keeps running at the fallback BPM.
void feed_silence_beats(md::MusicClock &d, double beats, double bpm = 128.0, uint32_t sr = 44100)
{
	const double secs = beats * 60.0 / bpm;
	const std::size_t n = static_cast<std::size_t>(secs * static_cast<double>(sr));
	mdtest::feed(d, std::vector<float>(n, 0.0f), sr);
}

bool has_forced_section(const std::vector<md::ClockEvent> &evs, md::Section sec)
{
	for (const auto &e : evs)
		if (e.type == md::ClockEvent::Type::SectionChange && e.section == sec && e.forced)
			return true;
	return false;
}

} // namespace

TEST_CASE("v1.2 phrase is now re-anchors the phrase grid")
{
	md::MusicClock d(mdtest::make_cfg(16));
	mdtest::feed(d, mdtest::gen_kick(128.0, 8).to_float(), 44100); // get the clock running

	d.push_command(md::Command::PhraseNow);
	mdtest::feed(d, std::vector<float>(256, 0.0f), 44100); // drain the command

	CHECK(mdtest::has_detail(d.events(), "phrase re-anchored: phrase starts now"));

	const uint64_t pb = d.snapshot().phrase;
	feed_silence_beats(d, 10.0); // less than one phrase (16 beats): no tick
	CHECK(d.snapshot().phrase == pb);
	feed_silence_beats(d, 10.0); // now past the boundary: exactly one tick
	CHECK(d.snapshot().phrase == pb + 1);
}

TEST_CASE("v1.2 drop now forces section DROP on the next beat")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_kick(128.0, 2).to_float(), 44100);
	REQUIRE(d.snapshot().section == md::Section::Groove);

	d.push_command(md::Command::DropNow);
	mdtest::feed(d, std::vector<float>(22050, 0.0f), 44100); // 0.5 s silence: drain + advance

	CHECK(d.snapshot().section == md::Section::Drop);
	CHECK(mdtest::has_detail(d.events(), "drop now"));
	CHECK(has_forced_section(d.events(), md::Section::Drop));
	CHECK(mdtest::section_beat(d.events(), md::Section::Drop) >= 0);
}

TEST_CASE("v1.2 force breakdown forces section BREAKDOWN on the next beat")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_kick(128.0, 2).to_float(), 44100);
	REQUIRE(d.snapshot().section == md::Section::Groove);

	d.push_command(md::Command::ForceBreakdown);
	mdtest::feed(d, std::vector<float>(44100, 0.0f), 44100); // 1 s silence

	CHECK(d.snapshot().section == md::Section::Breakdown);
	CHECK(mdtest::has_detail(d.events(), "force breakdown"));
	CHECK(has_forced_section(d.events(), md::Section::Breakdown));
	CHECK(mdtest::section_beat(d.events(), md::Section::Breakdown) >= 0);
}

TEST_CASE("v1.2 tap tempo sets the tempo from tap spacing")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, std::vector<float>(44100, 0.0f), 44100); // ~1 s of clock

	// five taps 0.5 s apart -> 120 BPM
	for (int i = 0; i < 5; i++) {
		d.push_command(md::Command::TapTempo);
		mdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);
	}
	CHECK(d.snapshot().bpm == doctest::Approx(120.0).epsilon(0.05));
	CHECK(mdtest::has_detail(d.events(), "tap tempo -> 1"));
}

TEST_CASE("v1.2 lock bpm freezes and unfreezes tempo tracking")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, std::vector<float>(44100, 0.0f), 44100);
	for (int i = 0; i < 3; i++) { // tap to 120 BPM (a tap sets and holds the tempo)
		d.push_command(md::Command::TapTempo);
		mdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);
	}
	REQUIRE(d.snapshot().bpm == doctest::Approx(120.0).epsilon(0.05));

	// a 150 BPM track must not move the clock while the tempo is held
	mdtest::feed_roundtrip(d, mdtest::gen_kick(150.0, 8), mdtest::fixture_dir(), "music-lockbpm.wav");
	CHECK(d.snapshot().bpm == doctest::Approx(120.0).epsilon(0.05));

	// unlock -> tracking resumes and follows the new tempo
	d.push_command(md::Command::LockBpm);
	mdtest::feed_roundtrip(d, mdtest::gen_kick(150.0, 12), mdtest::fixture_dir(), "music-lockbpm2.wav");
	CHECK(d.snapshot().bpm > 135.0);
	CHECK(d.snapshot().bpm < 165.0);
	CHECK(mdtest::has_detail(d.events(), "BPM unlocked"));
}

TEST_CASE("v1.2 nudge forward shifts the beat phase")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_kick(128.0, 4).to_float(), 44100);

	const double b0 = mdtest::beats_of(d);
	for (int i = 0; i < 4; i++)
		d.push_command(md::Command::NudgeFwd); // +0.2 beats
	mdtest::feed(d, std::vector<float>(256, 0.0f), 44100);
	const double b1 = mdtest::beats_of(d);
	CHECK(b1 - b0 > 0.15); // clearly beyond the ~0.012-beat advance the feed alone gives
	CHECK(b1 - b0 < 0.30);
	CHECK(mdtest::has_detail(d.events(), "beat phase forward"));
}

TEST_CASE("v1.2 nudge back shifts the beat phase")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_kick(128.0, 4).to_float(), 44100);

	const double b0 = mdtest::beats_of(d);
	for (int i = 0; i < 8; i++)
		d.push_command(md::Command::NudgeBack); // -0.4 beats
	mdtest::feed(d, std::vector<float>(256, 0.0f), 44100);
	const double b1 = mdtest::beats_of(d);
	CHECK(b1 - b0 < -0.30);
	CHECK(b1 - b0 > -0.50);
	CHECK(mdtest::has_detail(d.events(), "beat phase back"));
}

TEST_CASE("v1.2 bpm_locked in the snapshot follows every Lock BPM toggle (dock button state)")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, std::vector<float>(44100, 0.0f), 44100);
	CHECK_FALSE(d.snapshot().bpm_locked);

	// the dock button, the hotkey and a MIDI mapping all arrive as the same queued command
	d.push_command(md::Command::LockBpm);
	mdtest::feed(d, std::vector<float>(4410, 0.0f), 44100);
	CHECK(d.snapshot().bpm_locked);
	d.push_command(md::Command::LockBpm);
	mdtest::feed(d, std::vector<float>(4410, 0.0f), 44100);
	CHECK_FALSE(d.snapshot().bpm_locked);

	// a tap sets and holds the tempo, so the button lights without a Lock press
	for (int i = 0; i < 3; i++) {
		d.push_command(md::Command::TapTempo);
		mdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);
	}
	CHECK(d.snapshot().bpm_locked);
}
