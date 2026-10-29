#include "doctest.h"

#include "clock_api.h"

// v1.1 clock API: extrapolation + instance resolution (the libobs-free half of the proc glue).

TEST_CASE("clock extrapolation follows bpm, caps staleness, freezes when paused")
{
	const uint64_t t0 = 100000000000ull; // 100 s, so t0 - 5 s stays positive
	// 120 bpm = 2 beats/s: 100 ms later -> +0.2 beats
	CHECK(pd::clock_extrapolate(10.0, 120.0, t0, t0 + 100000000ull, false) == doctest::Approx(10.2));
	// stalled feed: 5 s stale is capped at 0.5 s -> +1 beat
	CHECK(pd::clock_extrapolate(10.0, 120.0, t0, t0 + 5000000000ull, false) == doctest::Approx(11.0));
	// audio timestamp ahead of the caller: extrapolate backwards (also capped)
	CHECK(pd::clock_extrapolate(10.0, 120.0, t0, t0 - 100000000ull, false) == doctest::Approx(9.8));
	CHECK(pd::clock_extrapolate(10.0, 120.0, t0, t0 - 5000000000ull, false) == doctest::Approx(9.0));
	// paused / no tempo: no movement
	CHECK(pd::clock_extrapolate(10.0, 120.0, t0, t0 + 100000000ull, true) == doctest::Approx(10.0));
	CHECK(pd::clock_extrapolate(10.0, 0.0, t0, t0 + 100000000ull, false) == doctest::Approx(10.0));
}

TEST_CASE("clock instance resolution")
{
	std::vector<pd::ClockInstance> all = {
		{7, "Clips", "Palette Lock"},
		{3, "Spout In", "Palette Lock"},
		{9, "Spout In", "Lock B"},
	};
	CHECK(pd::clock_resolve(all, 0, "", "") == 3); // no selector: oldest
	CHECK(pd::clock_resolve(all, 9, "", "") == 9);
	CHECK(pd::clock_resolve(all, 42, "", "") == 0); // unknown id never falls back
	CHECK(pd::clock_resolve(all, 0, "Clips", "") == 7);
	CHECK(pd::clock_resolve(all, 0, "Spout In", "Lock B") == 9);
	CHECK(pd::clock_resolve(all, 0, "Nope", "") == 0);
	CHECK(pd::clock_resolve(all, 0, "", "Lock B") == 9);
	CHECK(pd::clock_resolve({}, 0, "", "") == 0);
}

TEST_CASE("clock instance list is valid, escaped JSON")
{
	std::vector<pd::ClockInstance> all = {{1, "Spout \"In\"", "Lock\\1"}};
	CHECK(pd::clock_instances_json(all) == "[{\"id\":1,\"source\":\"Spout \\\"In\\\"\",\"filter\":\"Lock\\\\1\"}]");
	CHECK(pd::clock_instances_json({}) == "[]");
}

TEST_CASE("audio clock map removes bursty delivery jitter")
{
	// audio timeline in a foreign domain (+1 h), delivered in bursts: up to 80 ms late,
	// then caught up (the sandbox sawtooth). The mapped time must stay within the minimum delay.
	pd::AudioClockMap m;
	const uint64_t dom = 3600000000000ull;
	uint64_t worst = 0;
	for (int i = 0; i < 2000; ++i) {
		const uint64_t wall = 10000000000ull + static_cast<uint64_t>(i) * 5000000ull; // 5 ms ticks
		const uint64_t lag = static_cast<uint64_t>((i % 16) * 5000000ull);            // 0..75 ms sawtooth
		const uint64_t ats = wall - dom - lag;
		const uint64_t mapped = m.observe(ats, wall);
		if (i > 32) {
			const uint64_t truth = ats + dom; // what the audio sample's wall time really is
			const uint64_t err = mapped > truth ? mapped - truth : truth - mapped;
			worst = err > worst ? err : worst;
		}
	}
	CHECK(worst == 0); // min-filter locks onto the zero-delay observations exactly
}

TEST_CASE("audio clock map follows a timestamp jump")
{
	pd::AudioClockMap m;
	uint64_t wall = 50000000000ull;
	for (int i = 0; i < 100; ++i, wall += 5000000ull)
		m.observe(wall - 1000000000ull, wall); // offset 1 s
	CHECK(m.offset() == 1000000000ll);
	// media restarted: timestamps now 20 s behind -> offset jumps by +19 s, adopted at once
	const uint64_t mapped = m.observe(wall - 20000000000ull, wall);
	CHECK(m.offset() == 20000000000ll);
	CHECK(mapped == wall);
	// and a later, smaller delivery delay inside the window still wins (minimum)
	wall += 5000000ull;
	m.observe(wall - 20000000000ull + 3000000ull, wall);
	CHECK(m.offset() == 19997000000ll);
}
