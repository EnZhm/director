#include "doctest.h"

#include <cstdint>
#include <vector>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

/*
 * Spec 3.1: the clock's phrase grid publishes phrase_beat -- the beat the current phrase started
 * on. Bar lines are phrase_beat + 4k, so phrase_beat is 4-aligned on a grid that starts at beat 0
 * (and follows the re-anchor after Phrase now). api_version stays 1: fields are versioned by name.
 *
 * mdtest::gen_kick(bpm, bars) generates 4 kicks per bar plus a short silent tail.
 */

namespace {

// Feed `beats` worth of digital silence: the beat clock keeps running at the fallback BPM.
void feed_silence_beats(md::MusicClock &d, double beats, double bpm = 128.0, uint32_t sr = 44100)
{
	const double secs = beats * 60.0 / bpm;
	const std::size_t n = static_cast<std::size_t>(secs * static_cast<double>(sr));
	mdtest::feed(d, std::vector<float>(n, 0.0f), sr);
}

} // namespace

TEST_CASE("phrase_beat is the first beat of the current phrase; phrase edges land on it")
{
	md::MusicClock d(mdtest::make_cfg(16));
	mdtest::feed(d, mdtest::gen_kick(128.0, 6).to_float(), 44100); // 24 beats

	const md::ClockState st = d.snapshot();
	REQUIRE(st.phrase_beats == 16);
	REQUIRE(st.phrase == 1u); // past the beat-16 boundary, short of beat 32
	CHECK(st.phrase_beat == 16u);
	CHECK(st.phrase_beat == st.phrase * 16u); // no re-anchor: the grid starts at beat 0
	CHECK(st.phrase_beat <= st.beat);
	CHECK(st.beat - st.phrase_beat < 16u);
	CHECK(st.phrase_beat % 4u == 0u); // a bar line: bars are phrase_beat + 4k

	// every phrase edge starts a new phrase on its first beat
	const auto evs = mdtest::phrases(d.events());
	REQUIRE(!evs.empty());
	for (const auto &e : evs) {
		CHECK(e.beat == static_cast<uint64_t>(e.phrase) * 16u);
		CHECK(e.beat % 4u == 0u);
	}
	CHECK(evs.back().beat == st.phrase_beat); // the last edge and the snapshot agree
}

TEST_CASE("phrase_beat steps to the next boundary across a phrase edge")
{
	md::MusicClock d(mdtest::make_cfg(16));
	const std::vector<float> mono = mdtest::gen_kick(128.0, 8).to_float(); // 32 beats

	uint64_t prev_phrase = 0;
	uint64_t prev_start = 0; // the phrase grid starts on beat 0
	int edges = 0;
	mdtest::feed_hopwise(d, mono, 44100, [&](const md::ClockState &st, const std::vector<md::ClockEvent> &evs) {
		if (st.phrase > prev_phrase) {
			edges++;
			// one phrase later, exactly: the start never drifts
			CHECK(st.phrase_beat == prev_start + 16u);
			prev_phrase = st.phrase;
			prev_start = st.phrase_beat;
		}
		for (const auto &e : evs)
			if (e.type == md::ClockEvent::Type::Phrase)
				CHECK(e.beat == st.phrase_beat); // edge and snapshot agree
	});
	CHECK(edges >= 1);
}

TEST_CASE("Phrase now re-anchors the grid: phrase_beat is the beat the command landed on")
{
	md::MusicClock d(mdtest::make_cfg(16));
	mdtest::feed(d, mdtest::gen_kick(128.0, 8).to_float(), 44100); // 32 beats
	d.push_command(md::Command::PhraseNow);
	mdtest::feed(d, std::vector<float>(256, 0.0f), 44100); // drain the command

	REQUIRE(mdtest::has_detail(d.events(), "phrase re-anchored: phrase starts now"));
	const md::ClockState at = d.snapshot();
	CHECK(at.phrase_beat == at.beat); // the phrase starts on the re-anchor beat itself

	feed_silence_beats(d, 10.0); // still inside the phrase (16 beats)
	CHECK(d.snapshot().phrase == at.phrase);
	CHECK(d.snapshot().phrase_beat == at.phrase_beat);

	feed_silence_beats(d, 10.0); // past the boundary: a new phrase, one phrase later
	const md::ClockState next = d.snapshot();
	CHECK(next.phrase == at.phrase + 1u);
	CHECK(next.phrase_beat == at.phrase_beat + 16u);
}

TEST_CASE("phrase_beat follows a 16 -> 32 -> 16 phrase-length change")
{
	md::MusicClock d(mdtest::make_cfg(16));
	mdtest::feed(d, mdtest::gen_kick(128.0, 5).to_float(), 44100); // 20 beats

	const md::ClockState g16 = d.snapshot();
	REQUIRE(g16.phrase_beats == 16);
	REQUIRE(g16.phrase == 1u);
	CHECK(g16.phrase_beat == 16u);

	// 32-beat phrases: the same beat now sits in the first phrase of the new grid
	d.apply_config(mdtest::make_cfg(32));
	const md::ClockState st32 = d.snapshot();
	CHECK(st32.phrase_beats == 32);
	CHECK(st32.phrase == 0u);
	CHECK(st32.phrase_beat == 0u);

	// ... and its next boundary is beat 32, not 16
	mdtest::feed(d, mdtest::gen_kick(128.0, 5).to_float(), 44100); // 20 more beats: past 32
	const md::ClockState st32b = d.snapshot();
	REQUIRE(st32b.phrase == 1u);
	CHECK(st32b.phrase_beat == 32u);

	// back to 16: the phrase start returns to the 16-beat grid
	d.apply_config(mdtest::make_cfg(16));
	const md::ClockState back = d.snapshot();
	CHECK(back.phrase_beats == 16);
	CHECK(back.phrase_beat == back.phrase * 16u);
	CHECK(back.phrase_beat == 32u);
}
