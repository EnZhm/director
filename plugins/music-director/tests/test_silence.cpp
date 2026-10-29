#include "doctest.h"

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

TEST_CASE("silence -> fallback BPM, no section edges, no commands")
{
	md::MusicClock d(mdtest::make_cfg(32, 128.0f));
	const WavMono w = mdtest::gen_silence(10.0);
	mdtest::feed_roundtrip(d, w, mdtest::fixture_dir(), "silence.wav");

	const md::ClockState st = d.snapshot();
	CHECK(st.bpm == doctest::Approx(128.0).epsilon(0.001)); // fallback
	CHECK(st.confidence < 0.3f);                            // no lock
	CHECK(st.silent);

	CHECK(mdtest::section_changes(d.events()).empty()); // no automatic sections
	CHECK(mdtest::commands(d.events()).empty());        // no clock commands
	// the beat clock still ran at the fallback tempo (beats were counted)
	CHECK(st.beat > 0);
}
