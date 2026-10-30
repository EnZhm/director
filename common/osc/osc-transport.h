#pragma once
// Director suite: OSC UDP transport. One small interface over UDP, Winsock on Windows (_WIN32)
// and POSIX sockets elsewhere, so the same code that runs in a plugin also runs in a Linux unit
// test that does a real loopback round trip.
//
// Promoted from Clip Director 1.1.0 (plugins/clip-director/src/net/osc-transport.{h,cpp} at
// 68cf066^), namespace director_osc, plus deadline-scheduled sends.
//
// Neither the send nor the receive path is ever allowed to block the graphics or audio thread:
// send()/send_at() only queue a datagram for the sender thread, and the receiver thread drains
// the socket into a queue that pop() reads without waiting. Both threads are joined by stop().
//
// Sending and feedback use two UDP sockets on purpose. A feedback bind failure (the port is in
// use by another instance) must leave sending working and must be retried later; if the sender
// shared the feedback socket, its first sendto would auto-bind an ephemeral port and the retry
// could never succeed. The sender socket is therefore aimed at host:send_port and never bound to
// the feedback port, and the receiver socket is the one bound to it.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <netinet/in.h>
#endif

namespace director_osc {

// Monotonic nanoseconds (steady_clock). send_at() deadlines are measured against this clock.
uint64_t osc_now_ns();

// One UDP socket. send()/recv() wait at most `timeout_ms`; false on timeout or error.
class UdpSocket {
public:
#ifdef _WIN32
	using sock_t = uintptr_t;
	static constexpr sock_t kInvalid = static_cast<sock_t>(~static_cast<uintptr_t>(0));
#else
	using sock_t = int;
	static constexpr sock_t kInvalid = -1;
#endif

	UdpSocket() = default;
	~UdpSocket() { close(); }
	UdpSocket(const UdpSocket &) = delete;
	UdpSocket &operator=(const UdpSocket &) = delete;

	// Resolve host:send_port and bind locally. recv_port >= 0 binds that local port (0 = an
	// ephemeral one, read back with local_port()); recv_port < 0 leaves the socket unbound, for a
	// send-only socket.
	bool open(const std::string &host, int send_port, int recv_port, std::string *err);
	// Bind a receive-only socket (no remote) to recv_port; 0 binds an ephemeral port.
	bool open_recv(int recv_port, std::string *err);
	void close();
	bool valid() const;
	int local_port() const { return local_port_; }
	bool send(const uint8_t *data, std::size_t len, int timeout_ms = 200);
	bool recv(uint8_t *buf, std::size_t cap, std::size_t *len, int timeout_ms = 200);

private:
	bool bind_local(int family, int recv_port, std::string *err);
	bool finish_open(int family);

#ifdef _WIN32
	sock_t fd_ = kInvalid;
	char remote_[128] = {0};
	int remote_len_ = 0;
#else
	sock_t fd_ = kInvalid;
	struct sockaddr_storage remote_ = {};
	socklen_t remote_len_ = 0;
#endif
	int local_port_ = 0;
};

// Sender thread + receiver thread around one UdpSocket each.
class OscTransport {
public:
	OscTransport() = default;
	~OscTransport() { stop(); }
	OscTransport(const OscTransport &) = delete;
	OscTransport &operator=(const OscTransport &) = delete;

	// feedback_port > 0: a receiver socket is bound to it (a test reads it with local_port()).
	// feedback_port <= 0: no feedback socket at all; feedback_bound() stays false and nothing is
	// retried (Scene Director's "0 = no feedback"). A feedback bind failure is never fatal: sending
	// keeps working and the bind is retried every kBindRetryMs.
	bool start(const std::string &host, int send_port, int feedback_port, std::string *err);
	// Safe to call twice, from any non-graphics thread and from the destructor.
	void stop();
	bool running() const { return running_.load(); }
	bool feedback_bound() const { return feedback_bound_.load(); }
	int local_port() const { return recv_sock_.local_port(); }

	// Queue one datagram to leave as soon as possible (never blocks). false when not running or the
	// queue is full (a full queue also counts one drop()).
	bool send(const uint8_t *data, std::size_t len);
	// Queue one datagram to leave no earlier than not_before_ns (an osc_now_ns() timestamp). The
	// sender holds each datagram until the clock reaches its deadline, so datagrams leave in
	// not_before_ns order; equal deadlines keep enqueue order. send() == send_at(now).
	bool send_at(const uint8_t *data, std::size_t len, uint64_t not_before_ns);
	// One received datagram, or false when none is waiting. Never blocks.
	bool pop(std::vector<uint8_t> &msg);

	uint64_t sent() const;
	uint64_t received() const;
	uint64_t dropped() const;

	static constexpr std::size_t kMaxQueued = 256;
	static constexpr int kBindRetryMs = 5000;

private:
	struct Datagram {
		uint64_t not_before_ns = 0;
		std::vector<uint8_t> data;
	};

	void send_loop();
	void recv_loop();
	bool try_bind_feedback();

	mutable std::mutex mx_;
	std::condition_variable cv_;
	std::deque<Datagram> out_;            // queued datagrams, ordered by not_before_ns
	std::deque<std::vector<uint8_t>> in_; // received datagrams
	UdpSocket send_sock_;                 // aimed at host:send_port, never bound locally
	UdpSocket recv_sock_;                 // bound to the feedback port once (re)binding works
	std::thread tx_, rx_;
	std::atomic<bool> running_{false};
	std::atomic<bool> feedback_bound_{false};
	bool stop_ = false;
	int feedback_port_ = 0;
	uint64_t sent_ = 0, recv_ = 0, dropped_ = 0;
};

} // namespace director_osc
