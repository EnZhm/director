#include "doctest.h"

#include <cmath>
#include <functional>

#include "scheduler.h"

using namespace cd;

namespace {

// The test clock: a section function plus a section_seq that bumps on every change, and the
// section's downbeat (section_beat) as Music Director would report it.
struct RunState {
	Section last = Section::Groove;
	int64_t seq = 0;
	int64_t sb = 0;
	bool started = false;
};

using SectionAt = std::function<Section(double beat)>;
using CycleAt = std::function<bool(double beat)>;

std::vector<SchedEvent> run(CutScheduler &s, double from, double to, const SectionAt &sec, const CycleAt &cyc = nullptr,
			    const std::function<void(double)> &hook = nullptr, RunState *state = nullptr,
			    int64_t epoch = 0)
{
	RunState local;
	RunState &st = state ? *state : local;
	std::vector<SchedEvent> all;
	const double per_frame = 128.0 / 60.0 / 60.0;
	for (double b = from; b < to; b += per_frame) {
		if (hook)
			hook(b);
		const Section cur = sec(b);
		if (!st.started || cur != st.last) {
			st.started = true;
			st.last = cur;
			st.seq++; /* Music Director bumps section_seq on every section change */
			st.sb = (int64_t)std::floor(b);
		}
		ClockSample c;
		c.valid = true;
		c.beats = b;
		c.bpm = 128.0;
		c.section = cur;
		c.section_seq = st.seq;
		c.section_beat = st.sb;
		c.epoch = epoch;
		c.drop_cycle = cyc ? cyc(b) : false;
		c.drop_cycle_steps_per_beat = 2;
		auto ev = s.tick(c);
		for (auto &e : ev) {
			e.reason += " @" + std::to_string(b); // remember the tick beat for timing checks
			all.push_back(e);
		}
	}
	return all;
}

double tick_beat(const SchedEvent &e)
{
	return std::stod(e.reason.substr(e.reason.rfind('@') + 1));
}

std::vector<SchedEvent> of(const std::vector<SchedEvent> &v, SchedEvent::Type t)
{
	std::vector<SchedEvent> o;
	for (const auto &e : v)
		if (e.type == t)
			o.push_back(e);
	return o;
}

bool has_prepare(const std::vector<SchedEvent> &v, int slot, Section sec, double from_beat)
{
	for (const auto &e : v)
		if (e.type == SchedEvent::Type::Prepare && e.slot == slot && e.section == sec &&
		    tick_beat(e) >= from_beat - 1e-9)
			return true;
	return false;
}

const double kFrame = 128.0 / 60.0 / 60.0;

} // namespace

TEST_CASE("groove: cuts on bar lines every 4 bars, each prepared >= preroll ahead, within a frame")
{
	SchedulerConfig cfg;
	cfg.groove.crossfade_chance = 0.0;
	CutScheduler s(cfg);
	auto ev = run(s, 1.3, 200.0, [](double) { return Section::Groove; });
	auto cuts = of(ev, SchedEvent::Type::Cut);
	REQUIRE(cuts.size() >= 11);
	CHECK(cuts[0].at_beat == 4.0); // first bar line at least 1 beat ahead of 1.3
	for (std::size_t i = 0; i < cuts.size(); ++i) {
		CHECK(std::fmod(cuts[i].at_beat, 4.0) == 0.0);
		if (i)
			CHECK(cuts[i].at_beat - cuts[i - 1].at_beat == 16.0);
		CHECK(tick_beat(cuts[i]) - cuts[i].at_beat < kFrame + 1e-9); // fired on the first frame past
		CHECK(cuts[i].transition == Transition::Cut);
		CHECK_FALSE(cuts[i].section_edge);
	}
	// every cut's slot was prepared at least 1 beat before it
	double last_prep = -1;
	std::size_t ci = 0;
	for (const auto &e : ev) {
		if (e.type == SchedEvent::Type::Prepare && e.slot == CutScheduler::kNext)
			last_prep = e.at_beat;
		if (e.type == SchedEvent::Type::Cut && ci++ > 0)
			CHECK(e.at_beat - last_prep >= 1.0);
	}
}

TEST_CASE("breakdown / drop: the armed deck cuts at once on section_beat, holds continue on the bar line")
{
	CutScheduler s(SchedulerConfig{});
	auto sec = [](double b) {
		if (b < 32.0)
			return Section::Groove;
		if (b < 96.0)
			return Section::Breakdown;
		return Section::Drop;
	};
	auto ev = run(s, 0.0, 140.0, sec);
	auto cuts = of(ev, SchedEvent::Type::Cut);

	const SchedEvent *bd = nullptr, *dr = nullptr;
	for (const auto &c : cuts) {
		if (c.slot == CutScheduler::kBreakdown && !bd)
			bd = &c;
		if (c.slot == CutScheduler::kDrop && !dr)
			dr = &c;
	}
	// breakdown edge: cut the armed kBreakdown deck at once, on section_beat (32), crossfade 4 beats
	REQUIRE(bd);
	CHECK(bd->section_edge);
	CHECK(bd->section == Section::Breakdown);
	CHECK(bd->at_beat == 32.0);
	CHECK(bd->transition == Transition::Crossfade);
	CHECK(bd->transition_beats == 4.0);
	CHECK(tick_beat(*bd) - bd->at_beat < kFrame + 1e-6);
	// the next breakdown cut follows the breakdown hold rule (8 bars) from section_beat
	bool bd64 = false;
	for (const auto &c : cuts)
		bd64 |= c.slot == CutScheduler::kNext && c.at_beat == 64.0 && c.section == Section::Breakdown;
	CHECK(bd64);

	// drop edge: cut the armed kDrop deck at once, on section_beat (96)
	REQUIRE(dr);
	CHECK(dr->section_edge);
	CHECK(dr->section == Section::Drop);
	CHECK(dr->at_beat == 96.0);
	CHECK(dr->transition == Transition::Cut);
	CHECK(tick_beat(*dr) - dr->at_beat < kFrame + 1e-6);
	// then follow the drop hold rule (2 bars) from section_beat: a next-bar-line cut at 104
	bool drop104 = false;
	for (const auto &c : cuts)
		drop104 |= c.slot == CutScheduler::kNext && c.at_beat == 104.0;
	CHECK(drop104);

	// at the start (groove) both armed decks are prepared
	CHECK(has_prepare(ev, CutScheduler::kBreakdown, Section::Breakdown, 0.0));
	CHECK(has_prepare(ev, CutScheduler::kDrop, Section::Drop, 0.0));
	// after the drop edge the breakdown deck is re-armed for the next breakdown
	CHECK(has_prepare(ev, CutScheduler::kBreakdown, Section::Breakdown, 96.0));
}

TEST_CASE("an armed deck that had less than preroll is not ready: fall back to the next bar line")
{
	SchedulerConfig cfg;
	cfg.preroll_beats = 2.0;
	CutScheduler s(cfg);
	// groove from beat 0 (arms the decks), forced breakdown only half a beat later
	auto samples = [](double b) {
		return b < 0.5 ? Section::Groove : Section::Breakdown;
	};
	RunState st;
	auto ev = run(s, 0.0, 20.0, samples, nullptr, nullptr, &st);
	auto cuts = of(ev, SchedEvent::Type::Cut);
	bool edge = false;
	for (const auto &c : cuts)
		edge |= c.section_edge;
	CHECK_FALSE(edge); // the deck had ~0.5 beats, less than the 2-beat preroll
	// the fallback plans the next bar line at least preroll ahead of the edge
	bool planned = false;
	for (const auto &c : cuts)
		planned |= c.at_beat == 4.0 && c.section == Section::Breakdown;
	CHECK(planned);
	// and it says so in the scheduler log
	bool logged = false;
	for (const auto &line : s.log())
		logged |= line.find("armed deck not ready") != std::string::npos;
	CHECK(logged);
}

TEST_CASE("stutter follows the drop colour cycle: alternate live/prev at 1/8, no cut meanwhile")
{
	CutScheduler s(SchedulerConfig{});
	auto sec = [](double b) {
		return b < 64.0 ? Section::Breakdown : Section::Drop;
	};
	auto cyc = [](double b) {
		return b >= 64.0 && b < 68.0;
	}; // 1 bar
	auto ev = run(s, 0.0, 80.0, sec, cyc);
	auto flips = of(ev, SchedEvent::Type::StutterFlip);
	REQUIRE(flips.size() == 8); // 4 beats x 2 steps
	for (std::size_t i = 0; i < flips.size(); ++i) {
		CHECK(flips[i].at_beat == doctest::Approx(64.0 + 0.5 * i));
		CHECK(flips[i].show_prev == (i % 2 == 1));
	}
	CHECK(of(ev, SchedEvent::Type::StutterOn).size() == 1);
	CHECK(of(ev, SchedEvent::Type::StutterOff).size() == 1);
	for (const auto &c : of(ev, SchedEvent::Type::Cut))
		CHECK(!(tick_beat(c) > 64.1 && tick_beat(c) < 68.0));
}

TEST_CASE("hold freezes cuts (drop and stutter included); release and skip re-plan on the grid")
{
	CutScheduler s(SchedulerConfig{});
	auto ev = run(
		s, 0.0, 100.0, [](double) { return Section::Groove; }, nullptr,
		[&](double b) {
			if (b >= 10.0 && b < 10.0 + kFrame)
				s.set_hold(true);
			if (b >= 50.5 && b < 50.5 + kFrame)
				s.set_hold(false);
			if (b >= 70.2 && b < 70.2 + kFrame)
				s.skip();
		});
	auto cuts = of(ev, SchedEvent::Type::Cut);
	for (const auto &c : cuts)
		CHECK(!(c.at_beat > 10.0 && c.at_beat < 52.0));
	bool released = false, skipped = false;
	for (const auto &c : cuts) {
		released |= c.at_beat == 52.0; // first bar line >= 50.5 + 1
		skipped |= c.at_beat == 71.0;  // next beat after 70.2 (prepared long before)
	}
	CHECK(released);
	CHECK(skipped);

	CutScheduler h(SchedulerConfig{});
	h.set_hold(true);
	auto ev2 = run(
		h, 0.0, 80.0, [](double b) { return b < 40.0 ? Section::Breakdown : Section::Drop; },
		[](double b) { return b >= 40.0 && b < 44.0; });
	CHECK(of(ev2, SchedEvent::Type::Cut).empty());
	CHECK(of(ev2, SchedEvent::Type::StutterFlip).empty());
}

TEST_CASE("clock jump re-plans instead of bursting cuts; same seed gives the same log")
{
	CutScheduler s(SchedulerConfig{});
	RunState st;
	run(s, 0.0, 40.0, [](double) { return Section::Groove; }, nullptr, nullptr, &st);
	auto ev = run(s, 5.0, 40.0, [](double) { return Section::Groove; }, nullptr, nullptr, &st); // back 35 beats
	auto cuts = of(ev, SchedEvent::Type::Cut);
	REQUIRE(!cuts.empty());
	CHECK(cuts[0].at_beat == 8.0); // first bar line >= 5 + 1
	CHECK(cuts.size() <= 3);

	SchedulerConfig cfg;
	cfg.groove.crossfade_chance = 0.5;
	CutScheduler a(cfg), b(cfg);
	cfg.seed = 99;
	CutScheduler c(cfg);
	auto sec = [](double x) {
		return x < 128 ? Section::Groove : (x < 192 ? Section::Breakdown : Section::Drop);
	};
	run(a, 0.0, 400.0, sec);
	run(b, 0.0, 400.0, sec);
	run(c, 0.0, 400.0, sec);
	CHECK(a.log() == b.log());
	CHECK(a.log() != c.log());
}
