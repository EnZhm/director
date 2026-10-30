// Transport: a real UDP loopback round trip through the same code the plugins ship (POSIX
// sockets here, Winsock on Windows), deadline-scheduled sends, queue overflow and shutdown.
#include "doctest.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "osc.h"
#include "osc-transport.h"

using namespace director_osc;

namespace {

/* poll a transport's receive queue for up to `ms`, returning the first datagram */
bool wait_pop(OscTransport &t, std::vector<uint8_t> &msg, int ms = 2000)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	for (;;) {
		if (t.pop(msg))
			return true;
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

bool recv_datagram(UdpSocket &s, std::vector<uint8_t> &msg, int ms = 2000)
{
	uint8_t buf[2048];
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	for (;;) {
		size_t n = 0;
		if (s.recv(buf, sizeof(buf), &n, 50)) {
			msg.assign(buf, buf + n);
			return true;
		}
		if (std::chrono::steady_clock::now() > deadline)
			return false;
	}
}

std::vector<uint8_t> encode(const OscMessage &m)
{
	std::vector<uint8_t> out;
	REQUIRE(osc_encode(m, out));
	return out;
}

/* a free 127.0.0.1 UDP port: bind an ephemeral one, read it back, release it */
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

} // namespace

TEST_CASE("transport: send /scenes/x and receive it on a feedback socket")
{
	std::string err;

	// the peer stands in for Synesthesia's OSC input
	UdpSocket peer_in;
	REQUIRE(peer_in.open("127.0.0.1", 0, 0, &err));
	REQUIRE(peer_in.valid());

	// the transport sends to the peer and listens on its own feedback port
	const int feedback_port = reserve_free_port();
	REQUIRE(feedback_port > 0);
	OscTransport link;
	REQUIRE(link.start("127.0.0.1", peer_in.local_port(), feedback_port, &err));
	REQUIRE(link.running());
	REQUIRE(link.feedback_bound());
	CHECK(link.local_port() == feedback_port);

	const std::vector<uint8_t> wire = encode(osc_scene("tunnel", "acid"));
	REQUIRE(link.send(wire.data(), wire.size()));

	// 1. the message arrives on the peer (this is what Synesthesia would receive)
	std::vector<uint8_t> got;
	REQUIRE(recv_datagram(peer_in, got));
	CHECK(got == wire);

	// 2. Synesthesia's echo comes back on the feedback port and is queued for the tick.
	//    (Synesthesia answers from its own socket, so a second one stands in for it here.)
	UdpSocket syn_out;
	REQUIRE(syn_out.open("127.0.0.1", feedback_port, 0, &err));
	const std::vector<uint8_t> echo = encode(osc_scene("tunnel", ""));
	REQUIRE(syn_out.send(echo.data(), echo.size()));
	std::vector<uint8_t> back;
	REQUIRE(wait_pop(link, back));
	CHECK(back == echo);
	CHECK(link.sent() >= 1u);
	CHECK(link.received() >= 1u);

	link.stop();
	CHECK_FALSE(link.running());
	syn_out.close();
	peer_in.close();
}

TEST_CASE("transport: an address-only message survives the round trip, and stop() is clean")
{
	std::string err;
	UdpSocket peer_in;
	REQUIRE(peer_in.open("localhost", 0, 0, &err));

	OscTransport link;
	REQUIRE(link.start("localhost", peer_in.local_port(), 0, &err));
	CHECK_FALSE(link.feedback_bound()); // 0 = no feedback
	CHECK(link.local_port() == 0);
	const std::vector<uint8_t> wire = encode(osc_playlist_next());
	REQUIRE(link.send(wire.data(), wire.size()));
	std::vector<uint8_t> got;
	REQUIRE(recv_datagram(peer_in, got));
	OscMessage m;
	REQUIRE(osc_decode(got, m));
	CHECK(m.address == "/playlist/next");
	CHECK(m.args.empty());

	// several stops in a row, and send after stop, are all safe
	link.stop();
	link.stop();
	CHECK_FALSE(link.running());
	CHECK_FALSE(link.send(wire.data(), wire.size()));
	peer_in.close();
}

TEST_CASE("transport: a bad host fails to start and a socket refuses a nonsensical destination")
{
	std::string err;
	OscTransport bad;
	CHECK_FALSE(bad.start("host.invalid.", 7000, 0, &err));
	CHECK_FALSE(bad.running());
	CHECK_FALSE(err.empty());
	const std::vector<uint8_t> wire = encode(osc_bang(1));
	CHECK_FALSE(bad.send(wire.data(), wire.size()));

	UdpSocket s;
	CHECK_FALSE(s.valid());
	CHECK(s.local_port() == 0);
	CHECK_FALSE(s.send(nullptr, 0));
	uint8_t buf[8];
	size_t n = 0;
	CHECK_FALSE(s.recv(buf, sizeof(buf), &n, 10));
}

TEST_CASE("transport: datagrams leave in not_before_ns order and never early")
{
	std::string err;
	UdpSocket peer_in;
	REQUIRE(peer_in.open("127.0.0.1", 0, 0, &err));

	OscTransport link;
	REQUIRE(link.start("127.0.0.1", peer_in.local_port(), 0, &err));

	const std::vector<uint8_t> a = encode(osc_scene("a", ""));
	const std::vector<uint8_t> b = encode(osc_scene("b", ""));
	const std::vector<uint8_t> c = encode(osc_scene("c", ""));

	// enqueue out of deadline order: A at +300 ms, B at +150 ms, C at +450 ms
	const uint64_t ms = 1000000ull;
	const uint64_t base = osc_now_ns();
	REQUIRE(link.send_at(a.data(), a.size(), base + 300 * ms));
	REQUIRE(link.send_at(b.data(), b.size(), base + 150 * ms));
	REQUIRE(link.send_at(c.data(), c.size(), base + 450 * ms));

	const uint64_t deadlines[] = {base + 150 * ms, base + 300 * ms, base + 450 * ms};
	const std::string want[] = {"/scenes/b", "/scenes/a", "/scenes/c"};
	for (int i = 0; i < 3; ++i) {
		std::vector<uint8_t> got;
		const auto t0 = std::chrono::steady_clock::now();
		REQUIRE(recv_datagram(peer_in, got, 2000));
		const auto t1 = std::chrono::steady_clock::now();
		const uint64_t arrival = osc_now_ns();
		OscMessage m;
		REQUIRE(osc_decode(got, m));
		CHECK(m.address == want[i]);
		CHECK(arrival + 5 * ms >= deadlines[i]); // none early (5 ms slack)
		CHECK(t1 - t0 < std::chrono::milliseconds(2000));
	}
}

TEST_CASE("transport: a full send queue counts drops instead of blocking")
{
	std::string err;
	UdpSocket peer_in;
	REQUIRE(peer_in.open("127.0.0.1", 0, 0, &err));

	OscTransport link;
	REQUIRE(link.start("127.0.0.1", peer_in.local_port(), 0, &err));

	const std::vector<uint8_t> wire = encode(osc_playlist_next());
	const uint64_t far = osc_now_ns() + 60ull * 1000000000ull; // nothing leaves while we fill it
	for (size_t i = 0; i < OscTransport::kMaxQueued; ++i)
		REQUIRE(link.send_at(wire.data(), wire.size(), far));
	CHECK(link.dropped() == 0u);

	CHECK_FALSE(link.send_at(wire.data(), wire.size(), far));
	CHECK_FALSE(link.send(wire.data(), wire.size()));
	CHECK(link.dropped() == 2u);
}

TEST_CASE("transport: stop() under load joins both threads within 250 ms and is idempotent")
{
	std::string err;
	UdpSocket peer_in;
	REQUIRE(peer_in.open("127.0.0.1", 0, 0, &err));

	OscTransport link;
	REQUIRE(link.start("127.0.0.1", peer_in.local_port(), 0, &err));
	const std::vector<uint8_t> wire = encode(osc_playlist_next());

	std::atomic<bool> go{true};
	std::thread flood([&] {
		while (go.load())
			link.send(wire.data(), wire.size());
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	const auto t0 = std::chrono::steady_clock::now();
	link.stop();
	const auto elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
	go.store(false);
	flood.join();

	CHECK(elapsed.count() < 250);
	CHECK_FALSE(link.running());
	link.stop(); // idempotent, from the caller
	CHECK_FALSE(link.running());
	peer_in.close();
}

TEST_CASE("transport: a feedback port in use leaves sending working, then rebinds")
{
	std::string err;

	// a blocker holds a port, so the transport cannot bind it
	UdpSocket blocker;
	REQUIRE(blocker.open("127.0.0.1", 0, 0, &err));
	const int feedback_port = blocker.local_port();
	REQUIRE(feedback_port > 0);

	// the peer stands in for Synesthesia's OSC input, whatever port we send to
	UdpSocket peer_in;
	REQUIRE(peer_in.open("127.0.0.1", 0, 0, &err));

	OscTransport link;
	REQUIRE(link.start("127.0.0.1", peer_in.local_port(), feedback_port, &err));
	CHECK(link.running());
	CHECK_FALSE(link.feedback_bound());
	CHECK(link.local_port() == 0);

	// sending still works while the feedback bind is failing
	const std::vector<uint8_t> wire = encode(osc_playlist_next());
	REQUIRE(link.send(wire.data(), wire.size()));
	std::vector<uint8_t> got;
	REQUIRE(recv_datagram(peer_in, got));
	OscMessage m;
	REQUIRE(osc_decode(got, m));
	CHECK(m.address == "/playlist/next");
	CHECK(link.sent() >= 1u);

	// release the port: the bind is retried, so it succeeds within the retry window
	blocker.close();
	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::milliseconds(OscTransport::kBindRetryMs + 2000);
	while (std::chrono::steady_clock::now() < deadline && !link.feedback_bound())
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	CHECK(link.feedback_bound());
	CHECK(link.local_port() == feedback_port);

	// and feedback now arrives on the rebound socket
	UdpSocket syn_out;
	REQUIRE(syn_out.open("127.0.0.1", feedback_port, 0, &err));
	REQUIRE(syn_out.send(wire.data(), wire.size()));
	std::vector<uint8_t> back;
	REQUIRE(wait_pop(link, back));
	CHECK(back == wire);

	link.stop();
	syn_out.close();
	peer_in.close();
}
