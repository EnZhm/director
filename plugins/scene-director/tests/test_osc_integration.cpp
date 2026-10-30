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
struct FakeSyn {
	UdpSocket in;
	UdpSocket out;
	int feedback_port = 0;
	std::mutex mx;
	std::vector<std::string> received;
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
			}
			if (m.address == "/playlist/next" || m.address == "/playlist/previous") {
				std::vector<uint8_t> echo;
				if (osc_encode(osc_scene("fake", ""), echo))
					out.send(echo.data(), echo.size());
			}
		}
	}

	int port() const { return in.local_port(); }

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

	/* drain the feedback: each /scenes/fake confirms the oldest expectation */
	std::vector<uint8_t> msg;
	while (link.pop(msg)) {
		OscMessage m;
		if (osc_decode(msg, m) && m.address == "/scenes/fake")
			confirm.on_scene("fake", (int64_t)osc_now_ns());
	}

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
