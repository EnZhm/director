// director-follow.h: each clock edge is reported exactly once.
#include "director-follow.h"
#include "doctest.h"

using director_follow::Edges;
using director_follow::Follower;
using director_follow::Sample;
using director_follow::Section;

namespace {
Sample clock(int64_t beat, int64_t phrase, int section, int64_t seq, int64_t section_beat, int64_t epoch)
{
	Sample s;
	s.ok = true;
	s.beat = beat;
	s.phrase = phrase;
	s.section = section;
	s.section_seq = seq;
	s.section_beat = section_beat;
	s.epoch = epoch;
	return s;
}
} // namespace

TEST_CASE("no Music Director: lost is reported once, then found syncs without edges")
{
	Follower f;
	Sample none;
	Edges e = f.update(none);
	CHECK(e.lost);
	CHECK_FALSE(f.update(none).any());

	e = f.update(clock(10, 0, 0, 3, 0, 1));
	CHECK(e.found);
	CHECK(e.resync);
	CHECK_FALSE(e.section);
	CHECK_FALSE(e.beat);
	CHECK(f.following());
}

TEST_CASE("first sample syncs silently (no found, no edges)")
{
	Follower f;
	Edges e = f.update(clock(100, 3, 2, 7, 96, 1));
	CHECK(e.resync);
	CHECK_FALSE(e.found);
	CHECK_FALSE(e.section);
	CHECK_FALSE(e.phrase);
	CHECK_FALSE(e.beat);
}

TEST_CASE("beat, phrase and section edges fire once per change")
{
	Follower f;
	f.update(clock(30, 0, 0, 1, 0, 1));

	Edges e = f.update(clock(31, 0, 0, 1, 0, 1));
	CHECK(e.beat);
	CHECK(e.beat_no == 31);
	CHECK_FALSE(f.update(clock(31, 0, 0, 1, 0, 1)).any()); // same beat polled again

	e = f.update(clock(32, 1, 2, 2, 32, 1)); // drop on the phrase downbeat
	CHECK(e.section);
	CHECK(e.new_section == Section::Drop);
	CHECK(e.section_beat == 32);
	CHECK(e.section_seq == 2);
	CHECK(e.phrase);
	CHECK(e.phrase_no == 1);
	CHECK(e.beat);
	for (int i = 0; i < 50; ++i) // many polls inside the same beat: nothing more
		CHECK_FALSE(f.update(clock(32, 1, 2, 2, 32, 1)).any());
	CHECK(f.handled_section_seq() == 2);
}

TEST_CASE("a jump reports one edge, not a replay")
{
	Follower f;
	f.update(clock(10, 0, 0, 1, 0, 1));
	Edges e = f.update(clock(14, 0, 0, 1, 0, 1));
	CHECK(e.beat);
	CHECK(e.beat_no == 14);
}

TEST_CASE("epoch change resyncs without beat/phrase edges but keeps a section change")
{
	Follower f;
	f.update(clock(10, 0, 0, 1, 0, 1));
	Edges e = f.update(clock(40, 2, 1, 2, 40, 2)); // re-anchored AND forced breakdown
	CHECK(e.resync);
	CHECK(e.section);
	CHECK(e.new_section == Section::Breakdown);
	CHECK_FALSE(e.beat);
	CHECK_FALSE(e.phrase);
	e = f.update(clock(41, 2, 1, 2, 40, 2));
	CHECK(e.beat);
	CHECK_FALSE(e.section);
}

TEST_CASE("Music Director lost and found again: no phantom section edge")
{
	Follower f;
	f.update(clock(10, 0, 0, 1, 0, 1));
	Sample none;
	CHECK(f.update(none).lost);
	Edges e = f.update(clock(500, 15, 2, 9, 480, 1));
	CHECK(e.found);
	CHECK_FALSE(e.section);
	CHECK(f.handled_section_seq() == 9);
}

TEST_CASE("a forced section edge carries the forced flag")
{
	Follower f;
	f.update(clock(10, 0, 0, 1, 0, 1));
	Sample s = clock(12, 0, 1, 2, 12, 1);
	s.section_forced = true;
	Edges e = f.update(s);
	CHECK(e.section);
	CHECK(e.section_forced);
	s = clock(20, 0, 2, 3, 20, 1);
	e = f.update(s);
	CHECK(e.section);
	CHECK_FALSE(e.section_forced);
}
