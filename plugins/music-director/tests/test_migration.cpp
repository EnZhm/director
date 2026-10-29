#include "doctest.h"

#include "migration.h"

// The migration picker (spec 3.1): a pure function over the Palette Lock filters the OBS layer found.

namespace {
md::PaletteLock lock(const char *source, const char *filter, bool enabled, bool has_audio, const char *audio = "",
		     float gain = 0.0f, float sens = 0.6f, float bpm = 128.0f, int phrase = 32)
{
	md::PaletteLock l;
	l.source = source;
	l.filter = filter;
	l.enabled = enabled;
	l.has_audio_source = has_audio;
	l.values.audio_source = audio;
	l.values.input_gain_db = gain;
	l.values.kick_sensitivity = sens;
	l.values.fallback_bpm = bpm;
	l.values.phrase_beats = phrase;
	return l;
}
} // namespace

TEST_CASE("no Palette Lock at all: MD keeps its own defaults")
{
	const md::Migration m = md::pick_migration({});
	CHECK_FALSE(m.found);
	CHECK(m.index == -1);
	CHECK(m.differing.empty());
}

TEST_CASE("the first ENABLED filter with a user audio_source wins")
{
	std::vector<md::PaletteLock> locks = {
		lock("Cam", "Palette Lock", false, true, "Mic", 3.0f, 0.5f, 130.0f, 16),
		lock("Desktop", "Palette Lock", true, false, "", 0.0f, 0.6f, 128.0f, 32),
		lock("Mixer", "Palette Lock", true, true, "Desktop Audio", 6.0f, 0.4f, 126.0f, 16),
		lock("Spout", "Palette Lock", true, true, "Game", 1.0f, 0.7f, 140.0f, 32),
	};
	const md::Migration m = md::pick_migration(locks);
	REQUIRE(m.found);
	CHECK(m.index == 2);
	CHECK(m.source == "Mixer");
	CHECK(m.filter == "Palette Lock");
	CHECK(m.values.audio_source == "Desktop Audio");
	CHECK(m.values.input_gain_db == doctest::Approx(6.0f));
	CHECK(m.values.kick_sensitivity == doctest::Approx(0.4f));
	CHECK(m.values.fallback_bpm == doctest::Approx(126.0f));
	CHECK(m.values.phrase_beats == 16);
}

TEST_CASE("no enabled filter with a user audio_source: the first Palette Lock is used")
{
	std::vector<md::PaletteLock> locks = {
		lock("A", "Lock 1", false, true, "Mic"),
		lock("B", "Lock 2", true, false, ""),
	};
	const md::Migration m = md::pick_migration(locks);
	REQUIRE(m.found);
	CHECK(m.index == 0);
	CHECK(m.source == "A");
	CHECK(m.values.audio_source == "Mic");
}

TEST_CASE("the other filters that hold different audio values are reported (warning)")
{
	std::vector<md::PaletteLock> locks = {
		lock("A", "Lock 1", true, true, "Desktop Audio", 6.0f, 0.4f, 126.0f, 16),
		lock("B", "Lock 2", false, false, "Desktop Audio", 6.0f, 0.4f, 126.0f, 16), // identical
		lock("C", "Lock 3", true, false, "Mic", 0.0f, 0.6f, 128.0f, 32),            // differs
		lock("D", "Lock 4", true, false, "Desktop Audio", 6.0f, 0.4f, 126.0f, 32),  // phrase length differs
	};
	const md::Migration m = md::pick_migration(locks);
	REQUIRE(m.found);
	CHECK(m.index == 0);
	REQUIRE(m.differing.size() == 2);
	CHECK(m.differing[0] == 2); // C
	CHECK(m.differing[1] == 3); // D
	// B is not listed: identical audio values
	for (int idx : m.differing)
		CHECK(idx != 1);
}

TEST_CASE("defaults match Palette Director 1.2.0 (gain 0 dB, sensitivity 60%, 128 BPM, 32 beats)")
{
	const md::AudioSettings d;
	CHECK(d.audio_source.empty());
	CHECK(d.input_gain_db == doctest::Approx(0.0f));
	CHECK(d.kick_sensitivity == doctest::Approx(0.6f));
	CHECK(d.fallback_bpm == doctest::Approx(128.0f));
	CHECK(d.phrase_beats == 32);
}
