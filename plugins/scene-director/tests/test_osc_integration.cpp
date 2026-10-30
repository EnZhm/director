// Source-layer-free integration test: the event engine + confirmation tracker + a real
// director_osc transport on 127.0.0.1, driven against a fake Synesthesia (a UDP peer that
// answers a scene-changing send with /scenes/fake). Asserts one datagram per section edge,
// in-order (deadline) delivery, confirmation counting, and a clean stop(). No libobs, no OBS
// source -- the same wiring the source runs, minus the OBS layer.
#include "doctest.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "confirm.h"
#include "director-follow.h"
#include "event_engine.h"
#include "event_guard.h"
#include "event_map.h"
#include "osc.h"
#include "osc-transport.h"
#include "types.h"

using namespace cd;
using namespace director_osc;

namespace {

int reserve_free_port()
{
	std::string err;
	UdpSocket s;
	if (!s.open("127.0.0.1", 0, 0, &err))
		return 0;
	const int p = s.local_port();
	s.close();
	return p;
}

/* A fake Synesthesia: receives on one socket (its OSC input) and answers every scene-changing
 * send with /scenes/fake from its output socket, aimed back at the transport's feedback port. */
std::vector<uint8_t> bundle_of(const std::vector<std::vector<uint8_t>> &elements)
{
	std::vector<uint8_t> b = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
	for (const std::vector<uint8_t> &e : elements) {
		const uint32_t n = (uint32_t)e.size();
		b.push_back((uint8_t)(n >> 24));
		b.push_back((uint8_t)(n >> 16));
		b.push_back((uint8_t)(n >> 8));
		b.push_back((uint8_t)n);
		b.insert(b.end(), e.begin(), e.end());
	}
	return b;
}
struct FakeSyn {
	UdpSocket in;
	UdpSocket out;
	int feedback_port = 0;
	std::mutex mx;
	std::vector<std::string> received;
	std::vector<OscMessage> messages; // full decoded messages, in arrival order
	std::vector<uint64_t> arrived_ns; // osc_now_ns() at arrival, parallel to messages
	std::atomic<bool> answer{true};   // false: never sends feedback (feedback loss)
	std::atomic<bool> bundled{true};  // answer as an OSC bundle, as Synesthesia 1.25 does
	std::atomic<bool> stop{false};
	std::thread th;

	bool start(int fb_port, std::string *err)
	{
		feedback_port = fb_port;
		if (!in.open("127.0.0.1", 0, 0, err))
			return false;
		if (!out.open("127.0.0.1", feedback_port, 0, err))
			return false;
		th = std::thread([this] { loop(); });
		return true;
	}

	void loop()
	{
		uint8_t buf[2048];
		while (!stop.load()) {
			size_t n = 0;
			if (!in.recv(buf, sizeof(buf), &n, 50))
				continue;
			OscMessage m;
			if (!osc_decode(buf, n, m))
				continue;
			{
				std::lock_guard<std::mutex> g(mx);
				received.push_back(m.address);
				messages.push_back(m);
				arrived_ns.push_back(osc_now_ns());
			}
			if (answer.load() && (m.address == "/playlist/next" || m.address == "/playlist/previous")) {
				std::vector<uint8_t> echo;
				if (osc_encode(osc_scene("fake", ""), echo)) {
					/* Synesthesia 1.25 answers with a bundle: "#bundle\0", time tag 1
					 * (immediately), one element (size + message) */
					if (bundled.load())
						echo = bundle_of({echo});
					out.send(echo.data(), echo.size());
				}
			}
		}
	}

	int port() const { return in.local_port(); }

	// An unsolicited datagram from Synesthesia's output (an operator changed the scene).
	void emit(const std::vector<uint8_t> &bytes) { out.send(bytes.data(), bytes.size()); }

	size_t count()
	{
		std::lock_guard<std::mutex> g(mx);
		return received.size();
	}

	std::vector<std::string> snapshot()
	{
		std::lock_guard<std::mutex> g(mx);
		return received;
	}

	void shutdown()
	{
		stop.store(true);
		if (th.joinable())
			th.join();
		out.close();
		in.close();
	}
};

struct Sample {
	double beat;
	Section section = Section::Groove;
	int64_t seq = 0;
	int64_t section_beat = 0;
	bool forced = false;
	int64_t epoch = 0;

	ClockSample to_clock() const
	{
		ClockSample c;
		c.valid = true;
		c.beats = beat;
		c.bpm = 120.0;
		c.section = section;
		c.phrase_beats = 16;
		c.beat = (int64_t)beat;
		c.phrase = (int64_t)(beat / 16);
		c.phrase_beat = 0;
		c.section_seq = seq;
		c.section_beat = section_beat;
		c.section_forced = forced;
		c.epoch = epoch;
		return c;
	}
};

director_follow::Sample follow_sample(const ClockSample &clk)
{
	director_follow::Sample s;
	s.ok = clk.valid;
	s.beat = clk.beat;
	s.phrase = clk.phrase;
	s.phrase_beat = clk.phrase_beat;
	s.section = (int)clk.section;
	s.section_seq = clk.section_seq;
	s.section_beat = clk.section_beat;
	s.section_forced = clk.section_forced;
	s.epoch = clk.epoch;
	return s;
}

} // namespace

TEST_CASE("engine + confirm + transport: one datagram per section edge, confirmed, clean stop")
{
	std::string err;
	const int feedback_port = reserve_free_port();
	REQUIRE(feedback_port > 0);

	FakeSyn syn;
	REQUIRE(syn.start(feedback_port, &err));

	OscTransport link;
	REQUIRE(link.start("127.0.0.1", syn.port(), feedback_port, &err));
	REQUIRE(link.running());
	REQUIRE(link.feedback_bound());

	/* cooldown 0 so every automatic edge fires; the default map sends /playlist/next on breakdown
	 * and drop. */
	const EventMap map = EventMap::defaults_seeded(true, true, 0);
	EventEngine eng(map, /*lead_ms=*/0.0);
	ConfirmTracker confirm(2000);
	director_follow::Follower follower;

	auto feed = [&](const Sample &sm, int repeats) {
		std::vector<PlannedSend> fired;
		for (int i = 0; i < repeats; ++i) {
			const ClockSample clk = sm.to_clock();
			const director_follow::Edges e = follower.update(follow_sample(clk));
			const int64_t now = (int64_t)osc_now_ns();
			for (PlannedSend &p : eng.tick(clk, e, {}, now)) {
				OscMessage msg;
				msg.address = p.route;
				for (const EventArg &a : p.args) {
					if (a.type == EventArg::Type::Int)
						msg.add(a.i);
					else if (a.type == EventArg::Type::Float)
						msg.add(a.f);
					else
						msg.add(a.s);
				}
				std::vector<uint8_t> wire;
				REQUIRE(osc_encode(msg, wire));
				const uint64_t deadline = (uint64_t)p.send_at_ns;
				REQUIRE(link.send_at(wire.data(), wire.size(), deadline));
				confirm.on_sent(p.route, {}, now, 2000);
				fired.push_back(std::move(p));
			}
		}
		return fired;
	};

	/* anchor on the first sample */
	feed(Sample{0.0, Section::Groove, 0, 0}, 1);

	/* three section edges, each polled twice to prove exactly-once per section_seq */
	feed(Sample{16.0, Section::Breakdown, 1, 16}, 2);
	feed(Sample{32.0, Section::Drop, 2, 32}, 2);
	feed(Sample{48.0, Section::Breakdown, 3, 48}, 2);

	/* wait for the fake Synesthesia to receive all three datagrams */
	std::vector<std::string> got;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	for (;;) {
		got = syn.snapshot();
		if (got.size() >= 3 || std::chrono::steady_clock::now() > deadline)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	/* one datagram per section edge, in order (deadline ordering) */
	REQUIRE(got.size() == 3);
	for (const std::string &a : got)
		CHECK(a == "/playlist/next");
	CHECK(link.sent() >= 3u);

	/* drain the (bundled) feedback: each /scenes/fake confirms the oldest expectation */
	std::vector<uint8_t> msg;
	std::vector<OscMessage> ms;
	size_t bundles = 0;
	while (link.pop(msg)) {
		if (msg.size() >= 8 && msg[0] == '#')
			++bundles;
		if (!osc_decode_packet(msg, ms))
			continue;
		for (const OscMessage &m : ms)
			if (m.address == "/scenes/fake")
				confirm.on_scene("fake", (int64_t)osc_now_ns());
	}
	CHECK(bundles == 3u); /* the confirmations below arrived as bundles */

	const ConfirmCounters cc = confirm.counters();
	CHECK(cc.confirmed == 3);
	CHECK(cc.missed == 0);
	CHECK(cc.mismatched == 0);
	CHECK(cc.external == 0);
	CHECK(confirm.last_latency_ns() >= 0);

	/* stop() is clean and idempotent */
	link.stop();
	link.stop();
	CHECK_FALSE(link.running());
	syn.shutdown();
}

TEST_CASE("a /scenes/x with no expectation is external and never answered")
{
	std::string err;
	const int feedback_port = reserve_free_port();
	REQUIRE(feedback_port > 0);
	FakeSyn syn;
	REQUIRE(syn.start(feedback_port, &err));
	OscTransport link;
	REQUIRE(link.start("127.0.0.1", syn.port(), feedback_port, &err));

	ConfirmTracker confirm(2000);
	const int64_t now = (int64_t)osc_now_ns();
	const std::vector<ConfirmEvent> evs = confirm.on_scene("elsewhere", now);
	REQUIRE(evs.size() == 1);
	CHECK(evs[0].kind == ConfirmEvent::Kind::External);
	CHECK(confirm.counters().external == 1);

	link.stop();
	syn.shutdown();
}

// ---- the source's per-tick pipeline, without libobs ----
//
// Rig mirrors SdSource's video_tick exactly: drain feedback (confirm tracker + scene_loaded into the
// engine), engine tick with the 50 ms release horizon, per-source guard ownership, encode, and
// send_at with the engine's osc_now_ns() deadline. Tests drive it with real time and a real
// transport against FakeSyn.
namespace {

struct Rig {
	EventEngine eng;
	ConfirmTracker confirm{2000};
	director_follow::Follower follower;
	SourceClaims claims;
	EventGuard *guard;
	OscTransport link;
	std::vector<QueuedEvent> queued;
	std::vector<ConfirmEvent> confirm_log;
	int sends = 0;
	int64_t confirm_timeout_ms = 2000;

	Rig(const EventMap &map, EventGuard &g) : eng(map, 0.0), guard(&g) { eng.set_release_horizon_ms(50.0); }

	bool start(int send_port, int feedback_port, std::string *err)
	{
		return link.start("127.0.0.1", send_port, feedback_port, err);
	}

	void drain(int64_t now)
	{
		std::vector<uint8_t> raw;
		std::vector<OscMessage> ms;
		while (link.pop(raw)) {
			confirm.on_datagram(now);
			if (!osc_decode_packet(raw, ms)) /* the same path as the source's drain */
				continue;
			for (const OscMessage &m : ms) {
				if (m.address.rfind("/scenes/", 0) != 0 || m.address.size() <= 8)
					continue;
				const std::string name = m.address.substr(8);
				for (ConfirmEvent &ev : confirm.on_scene(name, now))
					confirm_log.push_back(std::move(ev));
				QueuedEvent q;
				q.kind = QueuedEvent::Kind::SceneLoaded;
				q.scene = name;
				queued.push_back(std::move(q));
			}
		}
	}

	void tick(const ClockSample &clk)
	{
		const int64_t now = (int64_t)osc_now_ns();
		drain(now);
		for (ConfirmEvent &ev : confirm.tick(now))
			confirm_log.push_back(std::move(ev));
		const director_follow::Edges e = follower.update(follow_sample(clk));
		std::vector<QueuedEvent> q;
		q.swap(queued);
		for (PlannedSend &p : eng.tick(clk, e, q, now)) {
			if (!claims.may_send(*guard, p.event, p.key))
				continue;
			OscMessage msg;
			msg.address = p.route;
			std::vector<std::string> args;
			for (const EventArg &a : p.args) {
				if (a.type == EventArg::Type::Int) {
					msg.add(a.i);
					args.push_back(std::to_string(a.i));
				} else if (a.type == EventArg::Type::Float) {
					msg.add(a.f);
					args.push_back(std::to_string(a.f));
				} else {
					msg.add(a.s);
					args.push_back(a.s);
				}
			}
			std::vector<uint8_t> wire;
			REQUIRE(osc_encode(msg, wire));
			const uint64_t deadline = p.has_send_at_ns ? (uint64_t)p.send_at_ns : osc_now_ns();
			REQUIRE(link.send_at(wire.data(), wire.size(), deadline));
			for (ConfirmEvent &ev : confirm.on_sent(p.route, args, now, confirm_timeout_ms))
				confirm_log.push_back(std::move(ev));
			++sends;
		}
	}
};

// A real-time clock at `bpm`: beats advance with osc_now_ns() from `start_beat`.
struct LiveClock {
	uint64_t t0 = osc_now_ns();
	double start_beat = 0.0;
	double bpm = 120.0;
	double beats() const { return start_beat + (double)(osc_now_ns() - t0) / 1e9 * bpm / 60.0; }
	uint64_t ns_at(double beat) const { return t0 + (uint64_t)((beat - start_beat) * 60.0 / bpm * 1e9); }
};

ClockSample live_sample(const LiveClock &lc, Section sec, int64_t seq, int64_t section_beat, int64_t epoch = 0)
{
	Sample s{lc.beats(), sec, seq, section_beat, false, epoch};
	ClockSample c = s.to_clock();
	c.bpm = lc.bpm;
	return c;
}

bool wait_for(FakeSyn &syn, size_t n, int ms)
{
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	while (std::chrono::steady_clock::now() < until) {
		if (syn.count() >= n)
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return syn.count() >= n;
}

} // namespace

TEST_CASE("deadline order: distinguishable sends released together leave in deadline order, on time")
{
	// Three mappings on the same drop, enqueued in file order A, B, C, with offsets that put
	// their deadlines in the order B (0 ms), C (+20 ms), A (+40 ms). All three fall inside the
	// 50 ms release horizon on the same tick, so the transport -- not the engine -- orders them.
	std::string err;
	const int fb = reserve_free_port();
	FakeSyn syn;
	REQUIRE(syn.start(fb, &err));
	syn.answer.store(false);
	EventGuard guard;
	EventMap map;
	REQUIRE(map.parse(R"({"version":1,"mappings":[
	  {"id":"a","event":"drop","offset_ms":40,"actions":[{"route":"/playlist/position","args":[{"int":1}]}]},
	  {"id":"b","event":"drop","offset_ms":0,"actions":[{"route":"/playlist/position","args":[{"int":2}]}]},
	  {"id":"c","event":"drop","offset_ms":20,"actions":[{"route":"/playlist/position","args":[{"int":3}]}]}]})")
			.syntax_ok);
	REQUIRE(map.mappings().size() == 3);
	Rig rig(map, guard);
	REQUIRE(rig.start(syn.port(), fb, &err));

	LiveClock lc;
	lc.start_beat = 60.0;
	rig.tick(live_sample(lc, Section::Groove, 0, 0));
	// the drop edge arrives with its downbeat at beat 61 (500 ms ahead at 120 BPM): ~16 ms ticks
	while (lc.beats() < 61.2) {
		rig.tick(live_sample(lc, Section::Drop, 1, 61));
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	REQUIRE(wait_for(syn, 3, 1000));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	std::vector<OscMessage> got;
	std::vector<uint64_t> at;
	{
		std::lock_guard<std::mutex> g(syn.mx);
		got = syn.messages;
		at = syn.arrived_ns;
	}
	REQUIRE(got.size() == 3); // exactly once each, across ~75 ticks
	REQUIRE(got[0].args.size() == 1);
	CHECK(got[0].args[0].i == 2);
	CHECK(got[1].args[0].i == 3);
	CHECK(got[2].args[0].i == 1);

	// each landed no earlier than its deadline and within 25 ms of it (loopback, idle box)
	const uint64_t downbeat = lc.ns_at(61.0);
	const int64_t offs[3] = {0, 20, 40};
	for (int i = 0; i < 3; ++i) {
		const double late_ms = ((double)at[(size_t)i] - (double)(downbeat + (uint64_t)offs[i] * 1000000)) / 1e6;
		INFO("send " << i << " late by " << late_ms << " ms");
		CHECK(late_ms >= -1.0);
		CHECK(late_ms < 25.0);
	}
	rig.link.stop();
	syn.shutdown();
}

TEST_CASE("the drop send lands on the beat, not on the next video frame")
{
	// 30 fps ticks (33 ms) with the 50 ms horizon: the send is released on the frame before the
	// downbeat and held to it, so it is never a frame late.
	std::string err;
	const int fb = reserve_free_port();
	FakeSyn syn;
	REQUIRE(syn.start(fb, &err));
	syn.answer.store(false);
	EventGuard guard;
	Rig rig(EventMap::defaults_seeded(true, true, 0), guard);
	REQUIRE(rig.start(syn.port(), fb, &err));
	LiveClock lc;
	lc.start_beat = 30.0;
	rig.tick(live_sample(lc, Section::Groove, 0, 0));
	while (lc.beats() < 31.3) {
		rig.tick(live_sample(lc, Section::Drop, 1, 31));
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}
	REQUIRE(wait_for(syn, 1, 1000));
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	REQUIRE(syn.count() == 1);
	uint64_t at;
	{
		std::lock_guard<std::mutex> g(syn.mx);
		at = syn.arrived_ns[0];
	}
	const double late_ms = ((double)at - (double)lc.ns_at(31.0)) / 1e6;
	INFO("late by " << late_ms << " ms");
	CHECK(late_ms >= -1.0);
	CHECK(late_ms < 15.0);
	rig.link.stop();
	syn.shutdown();
}

TEST_CASE("two sources on one guard: a multi-action drop goes out once, all actions, from one source")
{
	std::string err;
	const int fb1 = reserve_free_port();
	const int fb2 = reserve_free_port();
	REQUIRE(fb1 != fb2);
	FakeSyn syn;
	REQUIRE(syn.start(fb1, &err));
	syn.answer.store(false);
	EventGuard guard;
	EventMap map;
	REQUIRE(map.parse(R"({"version":1,"mappings":[
	  {"id":"drop-two","event":"drop","actions":[{"route":"/playlist/next"},
	   {"route":"/playlist/position","args":[{"int":9}]}]},
	  {"id":"drop-extra","event":"drop","actions":[{"route":"/render/enabled","args":[{"int":1}]}]}]})")
			.syntax_ok);
	Rig a(map, guard), b(map, guard);
	REQUIRE(a.start(syn.port(), fb1, &err));
	REQUIRE(b.start(syn.port(), fb2, &err));
	LiveClock lc;
	lc.start_beat = 10.0;
	a.tick(live_sample(lc, Section::Groove, 0, 0));
	b.tick(live_sample(lc, Section::Groove, 0, 0));
	for (int i = 0; i < 6; ++i) { // the same edge polled repeatedly by both, interleaved
		a.tick(live_sample(lc, Section::Drop, 1, 10));
		b.tick(live_sample(lc, Section::Drop, 1, 10));
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(wait_for(syn, 3, 1000));
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	CHECK(syn.count() == 3);       // 3 actions, once -- not 1 (split ownership), not 6 (both)
	CHECK(a.sends + b.sends == 3); // and one source sent all three
	CHECK((a.sends == 3 || b.sends == 3));
	a.link.stop();
	b.link.stop();
	syn.shutdown();
}

TEST_CASE("an external scene change is logged and never answered (0 datagrams to Synesthesia)")
{
	std::string err;
	const int fb = reserve_free_port();
	FakeSyn syn;
	REQUIRE(syn.start(fb, &err));
	EventGuard guard;
	Rig rig(EventMap::defaults(), guard); // the shipped defaults
	REQUIRE(rig.start(syn.port(), fb, &err));
	LiveClock lc;
	rig.tick(live_sample(lc, Section::Groove, 0, 0));

	// The operator changes the scene on Synesthesia's own controller: /scenes/elsewhere arrives
	// on the feedback port with nothing pending.
	std::vector<uint8_t> wire;
	REQUIRE(osc_encode(osc_scene("elsewhere", ""), wire));
	syn.emit(wire);
	// run the full pipeline for 500 ms of ticks
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
	while (std::chrono::steady_clock::now() < until) {
		rig.tick(live_sample(lc, Section::Groove, 0, 0));
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	const ConfirmCounters cc = rig.confirm.counters();
	CHECK(cc.external == 1);
	CHECK(cc.confirmed == 0);
	CHECK(rig.sends == 0);
	CHECK(rig.link.sent() == 0u);
	CHECK(syn.count() == 0); // Scene Director never fights an external change
	rig.link.stop();
	syn.shutdown();
}

TEST_CASE("feedback loss: a missed confirmation is counted, and the next drop still sends")
{
	std::string err;
	const int fb = reserve_free_port();
	FakeSyn syn;
	REQUIRE(syn.start(fb, &err));
	syn.answer.store(false); // Synesthesia drops its feedback (seen on the show rig)
	EventGuard guard;
	EventMap map = EventMap::defaults_seeded(true, true, 0);
	Rig rig(map, guard);
	rig.confirm_timeout_ms = 150; // short timeout so the test is quick
	REQUIRE(rig.start(syn.port(), fb, &err));
	LiveClock lc;
	lc.start_beat = 0.0;
	rig.tick(live_sample(lc, Section::Groove, 0, 0));
	rig.tick(live_sample(lc, Section::Drop, 1, 0));
	REQUIRE(wait_for(syn, 1, 500));
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
	while (std::chrono::steady_clock::now() < until) {
		rig.tick(live_sample(lc, Section::Drop, 1, 0));
		std::this_thread::sleep_for(std::chrono::milliseconds(16));
	}
	// the next section edge is not held back by the unconfirmed one
	rig.tick(live_sample(lc, Section::Breakdown, 2, (int64_t)lc.beats()));
	REQUIRE(wait_for(syn, 2, 500));
	CHECK(syn.count() == 2);
	rig.tick(live_sample(lc, Section::Breakdown, 2, (int64_t)lc.beats()));
	CHECK(rig.confirm.counters().missed >= 1);
	rig.link.stop();
	syn.shutdown();
}
