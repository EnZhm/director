#include "doctest.h"

#include <cstdint>
#include <vector>

#include "clock.h"
#include "test_helpers.h"

using namespace mdtest;

// The follower signals Music Director adds on top of Palette Director 1.2.0's clock:
// section_seq, section_beat and epoch (include/director-clock.h).

TEST_CASE("section_seq increments on every section change; section_beat is the beat it started on")
{
	md::MusicClock d(mdtest::make_cfg());
	const md::ClockState st0 = d.snapshot();
	CHECK(st0.section == md::Section::Groove);
	CHECK(st0.section_seq == 0);
	CHECK(st0.section_beat == 0);

	mdtest::feed(d, mdtest::gen_breakdown(128.0, 8, 4, 4).to_float(), 44100);

	uint64_t expect = 0, last_beat = 0;
	for (const auto &e : mdtest::section_changes(d.events())) {
		expect++;
		CHECK(e.beat >= last_beat); // sections advance in beat order
		last_beat = e.beat;
	}
	const md::ClockState st = d.snapshot();
	CHECK(expect >= 2);                  // breakdown, then drop at least
	CHECK(st.section_seq == expect);     // exactly one increment per change
	CHECK(st.section_beat == last_beat); // points at the current section's start beat
}

TEST_CASE("a forced section bumps section_seq and lands on the next beat's number")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, mdtest::gen_kick(128.0, 2).to_float(), 44100);
	const uint64_t seq0 = d.snapshot().section_seq;
	REQUIRE(d.snapshot().section == md::Section::Groove);

	d.push_command(md::Command::DropNow);
	mdtest::feed(d, std::vector<float>(22050, 0.0f), 44100);

	const md::ClockState st = d.snapshot();
	CHECK(st.section == md::Section::Drop);
	CHECK(st.section_seq == seq0 + 1);
	// the forced section event and the snapshot agree on the downbeat
	int64_t forced_beat = -1;
	for (const auto &e : d.events())
		if (e.type == md::ClockEvent::Type::SectionChange && e.forced)
			forced_beat = static_cast<int64_t>(e.beat);
	CHECK(forced_beat >= 0);
	CHECK(st.section_beat == static_cast<uint64_t>(forced_beat));
}

TEST_CASE("epoch increments on every re-anchor (phrase now, tap, lock, nudge, source change, reset)")
{
	md::MusicClock d(mdtest::make_cfg());
	mdtest::feed(d, std::vector<float>(44100, 0.0f), 44100);
	uint64_t epoch = d.snapshot().epoch;

	auto bump = [&](md::Command c) {
		d.push_command(c);
		mdtest::feed(d, std::vector<float>(256, 0.0f), 44100);
		const uint64_t next = d.snapshot().epoch;
		CHECK(next > epoch);
		epoch = next;
	};
	bump(md::Command::PhraseNow);
	bump(md::Command::TapTempo);
	bump(md::Command::LockBpm);
	bump(md::Command::NudgeFwd);
	bump(md::Command::NudgeBack);

	// a new audio source re-anchors the grid for followers
	d.reset_audio_reference();
	CHECK(d.snapshot().epoch > epoch);
	epoch = d.snapshot().epoch;

	// a full reset re-anchors too
	d.reset();
	CHECK(d.snapshot().epoch == epoch + 1);
	CHECK(d.snapshot().section == md::Section::Groove);
}

TEST_CASE("per-hop snapshots: the clock can be read after every 256-sample hop")
{
	md::MusicClock d(mdtest::make_cfg());
	const std::vector<float> mono = mdtest::gen_kick(128.0, 4).to_float();

	int hops = 0;
	uint64_t prev_beat = 0;
	mdtest::feed_hopwise(d, mono, 44100, [&](const md::ClockState &st, const std::vector<md::ClockEvent> &evs) {
		hops++;
		CHECK(st.beat >= prev_beat); // the beat counter never goes backwards inside an epoch
		prev_beat = st.beat;
		for (const auto &e : evs)
			if (e.type == md::ClockEvent::Type::SectionChange)
				CHECK(st.section_beat == e.beat); // the snapshot agrees with the edge
	});
	CHECK(hops == static_cast<int>(mono.size() / 256));
	CHECK(hops > 100);
	CHECK(prev_beat > 0);
}
