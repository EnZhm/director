#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "confirm.h"

using namespace cd;

namespace {

constexpr int64_t kMs = 1000000;

// One scene-changing send with no arguments.
std::vector<ConfirmEvent> send(ConfirmTracker &tr, const std::string &route, int64_t now_ns, int64_t timeout_ms = 2000)
{
	return tr.on_sent(route, {}, now_ns, timeout_ms);
}

} // namespace

TEST_CASE("Synesthesia's slug rule: lowercase, drop space, '_', '-' and tab")
{
	CHECK(osc_slug("House") == "house");
	CHECK(osc_slug("Vocal House") == "vocalhouse");
	CHECK(osc_slug("vocal-house") == "vocalhouse");
	CHECK(osc_slug("vocal_house") == "vocalhouse");
	CHECK(osc_slug("Vocal_House Two") == "vocalhousetwo");
	CHECK(osc_slug("\tHouse ") == "house");
	CHECK(osc_slug("") == "");
	CHECK(osc_slug("a1.b") == "a1.b"); // only the four characters are dropped
}

TEST_CASE("only scene-changing routes arm an expectation")
{
	const char *arming[] = {"/playlist/next",   "/playlist/previous", "/playlist/position",
				"/playlist/select", "/scenes/house",      "/scenes/"};
	for (const char *route : arming) {
		ConfirmTracker tr;
		CHECK(tr.on_sent(route, {}, 0, 2000).empty());
		CHECK(tr.open_expectations() == 1);
	}

	const char *not_arming[] = {
		"/presets",        "/favslots/1", "/controls/meta/low_color", "/controls/global/bang/1", "/playlist",
		"/playlist/nextx", "/scenes"};
	for (const char *route : not_arming) {
		ConfirmTracker tr;
		CHECK(tr.on_sent(route, {}, 0, 2000).empty());
		CHECK(tr.open_expectations() == 0);
		CHECK(tr.tick(kMs * 100000).empty()); // nothing was armed, so nothing can be missed
		CHECK(tr.counters().missed == 0);
	}
}

TEST_CASE("a report inside the timeout confirms the send and records the latency")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 1000).empty());
	CHECK(tr.open_expectations() == 1);
	CHECK(tr.link_state(1000) == LinkState::Sending); // sends go out, nothing seen back yet

	const std::vector<ConfirmEvent> ev = tr.on_scene("fake", 1000 + 42 * kMs);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Confirmed);
	CHECK(ev[0].route == "/playlist/next");
	CHECK(ev[0].scene == "fake");
	CHECK(ev[0].latency_ns == 42 * kMs);
	CHECK(ev[0].text == "confirm fake in 42.0 ms");
	CHECK(ev[0].kind_name() == std::string("confirmed"));
	CHECK(tr.counters().confirmed == 1);
	CHECK(tr.counters().missed == 0);
	CHECK(tr.last_latency_ns() == 42 * kMs);
	CHECK(tr.median_latency_ns() == 42 * kMs);
	CHECK(tr.p90_latency_ns() == 42 * kMs);
	CHECK(tr.open_expectations() == 0);
	// The confirmation itself is feedback: the link is up now.
	CHECK(tr.link_state(1000 + 42 * kMs) == LinkState::Linked);
}

TEST_CASE("the send's arguments are carried on the result")
{
	ConfirmTracker tr;
	CHECK(tr.on_sent("/playlist/select", {"techno"}, 0, 2000).empty());
	const std::vector<ConfirmEvent> ev = tr.on_scene("house", 5 * kMs);
	REQUIRE(ev.size() == 1);
	// 4.6 slug-checks a `/scenes/{x}` send only: a select is confirmed by any report.
	CHECK(ev[0].kind == ConfirmEvent::Kind::Confirmed);
	CHECK(ev[0].route == "/playlist/select");
	REQUIRE(ev[0].args.size() == 1);
	CHECK(ev[0].args[0] == "techno");
}

TEST_CASE("an expectation whose deadline passes is reported as no confirmation")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 0).empty());
	CHECK(tr.tick(1999 * kMs).empty()); // still inside the 2000 ms window
	const std::vector<ConfirmEvent> ev = tr.tick(2000 * kMs);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Missed);
	CHECK(ev[0].route == "/playlist/next");
	CHECK(ev[0].text == "no confirmation for /playlist/next after 2000 ms");
	CHECK(ev[0].kind_name() == std::string("missed"));
	CHECK(tr.counters().missed == 1);
	CHECK(tr.counters().confirmed == 0);
	CHECK(tr.open_expectations() == 0);

	// No resend: the tracker never re-arms the route on its own.
	CHECK(tr.tick(9000 * kMs).empty());
	CHECK(tr.open_expectations() == 0);
	CHECK(tr.counters().missed == 1);
}

TEST_CASE("a report at the deadline is late, not a confirmation")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 0).empty());
	const std::vector<ConfirmEvent> ev = tr.on_scene("fake", 2000 * kMs);
	REQUIRE(ev.size() == 2);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Missed);
	CHECK(ev[1].kind == ConfirmEvent::Kind::External);
	CHECK(tr.counters().confirmed == 0);
}

TEST_CASE("the configured timeout is per send, and zero falls back to the tracker default")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 0, 500).empty());
	REQUIRE(tr.tick(500 * kMs).size() == 1);
	CHECK(tr.counters().missed == 1);

	CHECK(tr.on_sent("/playlist/next", {}, kMs, 0).empty());
	CHECK(tr.tick(kMs + 1999 * kMs).empty());
	const std::vector<ConfirmEvent> ev = tr.tick(kMs + 2000 * kMs);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].text == "no confirmation for /playlist/next after 2000 ms");
}

TEST_CASE("a /scenes send must be confirmed by the scene it asked for")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/scenes/house", 0).empty());
	const std::vector<ConfirmEvent> ev = tr.on_scene("techno", 10 * kMs);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Mismatch);
	CHECK(ev[0].expected == "house");
	CHECK(ev[0].scene == "techno");
	CHECK(ev[0].text == "mismatch: sent house, Synesthesia reports techno");
	CHECK(ev[0].kind_name() == std::string("mismatch"));
	CHECK(ev[0].latency_ns == -1); // a mismatch is not a confirmation: no latency recorded
	CHECK(tr.counters().mismatched == 1);
	CHECK(tr.counters().confirmed == 0);
	CHECK(tr.last_latency_ns() == -1);
	CHECK(tr.open_expectations() == 0); // the report consumed the expectation
}

TEST_CASE("a /scenes send is confirmed when the reported slug matches")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/scenes/vocalhouse", 0).empty());
	const std::vector<ConfirmEvent> ev = tr.on_scene("vocalhouse", 12 * kMs);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Confirmed);
	CHECK(ev[0].latency_ns == 12 * kMs);
	CHECK(tr.counters().confirmed == 1);

	// A route carrying the raw name checks the slug of it, and a raw report name is slugged too,
	// so Synesthesia's own slugs compare equal.
	ConfirmTracker raw;
	CHECK(send(raw, "/scenes/Vocal House", 0).empty());
	CHECK(raw.on_scene("Vocal House", kMs)[0].kind == ConfirmEvent::Kind::Confirmed);
	CHECK(send(raw, "/scenes/Vocal House", 2 * kMs).empty());
	CHECK(raw.on_scene("vocalhouse", 3 * kMs)[0].kind == ConfirmEvent::Kind::Confirmed);
}

TEST_CASE("a report with nothing open is an external scene change")
{
	ConfirmTracker tr;
	const std::vector<ConfirmEvent> ev = tr.on_scene("someone-else", 7 * kMs);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == ConfirmEvent::Kind::External);
	CHECK(ev[0].scene == "someone-else");
	CHECK(ev[0].route.empty());
	CHECK(ev[0].latency_ns == -1);
	CHECK(ev[0].text == "external scene change: someone-else");
	CHECK(ev[0].kind_name() == std::string("external"));
	CHECK(tr.counters().external == 1);
	CHECK(tr.counters().confirmed == 0);
	// An external change is still a datagram: the link is up.
	CHECK(tr.link_state(7 * kMs) == LinkState::Linked);
}

TEST_CASE("two quick sends are confirmed in order by the two reports")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 0).empty());
	CHECK(send(tr, "/playlist/previous", 5 * kMs).empty());
	CHECK(tr.open_expectations() == 2);

	const std::vector<ConfirmEvent> first = tr.on_scene("one", 30 * kMs);
	REQUIRE(first.size() == 1);
	CHECK(first[0].kind == ConfirmEvent::Kind::Confirmed);
	CHECK(first[0].route == "/playlist/next"); // FIFO: the oldest expectation is confirmed first
	CHECK(first[0].latency_ns == 30 * kMs);

	const std::vector<ConfirmEvent> second = tr.on_scene("two", 40 * kMs);
	REQUIRE(second.size() == 1);
	CHECK(second[0].route == "/playlist/previous");
	CHECK(second[0].latency_ns == 35 * kMs);
	CHECK(tr.counters().confirmed == 2);
	CHECK(tr.open_expectations() == 0);
	CHECK(tr.last_latency_ns() == 35 * kMs);
}

TEST_CASE("an expectation that timed out is a miss, and the late report is external")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 0).empty());
	const std::vector<ConfirmEvent> ev = tr.on_scene("fake", 2100 * kMs); // no tick first
	REQUIRE(ev.size() == 2);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Missed);
	CHECK(ev[0].text == "no confirmation for /playlist/next after 2000 ms");
	CHECK(ev[1].kind == ConfirmEvent::Kind::External);
	CHECK(ev[1].text == "external scene change: fake");
	CHECK(tr.counters().missed == 1);
	CHECK(tr.counters().external == 1);
	CHECK(tr.counters().confirmed == 0);
	CHECK(tr.open_expectations() == 0);
}

TEST_CASE("median and p90 are taken over the last 64 confirmations")
{
	ConfirmTracker tr;
	for (int64_t i = 1; i <= 64; i++) {
		CHECK(send(tr, "/playlist/next", i * kMs).empty());
		CHECK(tr.on_scene("fake", i * kMs + i * kMs)[0].latency_ns == i * kMs);
	}
	CHECK(tr.confirmations() == 64);
	CHECK(tr.last_latency_ns() == 64 * kMs);
	CHECK(tr.median_latency_ns() == 32 * kMs + kMs / 2); // mean of 32 and 33 ms
	CHECK(tr.p90_latency_ns() == 58 * kMs);              // nearest rank: ceil(0.9 * 64) = 58

	// The 65th confirmation pushes the 1 ms sample out of the window.
	CHECK(send(tr, "/playlist/next", 100 * kMs).empty());
	CHECK(tr.on_scene("fake", 200 * kMs)[0].latency_ns == 100 * kMs);
	CHECK(tr.confirmations() == 64);
	CHECK(tr.last_latency_ns() == 100 * kMs);
	CHECK(tr.median_latency_ns() == 33 * kMs + kMs / 2); // samples 2..64 plus 100: mean of 33 and 34
	CHECK(tr.p90_latency_ns() == 59 * kMs);
}

TEST_CASE("the latency statistics are empty until the first confirmation")
{
	ConfirmTracker tr;
	CHECK(tr.last_latency_ns() == -1);
	CHECK(tr.median_latency_ns() == -1);
	CHECK(tr.p90_latency_ns() == -1);
	CHECK(tr.confirmations() == 0);

	// An even count takes the floored mean of the two middle samples; p90 of four is the largest.
	for (int64_t i = 1; i <= 4; i++) {
		CHECK(send(tr, "/playlist/next", i * kMs).empty());
		CHECK(tr.on_scene("fake", i * kMs + i * 10 * kMs)[0].kind == ConfirmEvent::Kind::Confirmed);
	}
	CHECK(tr.median_latency_ns() == 25 * kMs);
	CHECK(tr.p90_latency_ns() == 40 * kMs);
}

TEST_CASE("the open expectations are bounded")
{
	ConfirmTracker tr;
	for (std::size_t i = 0; i < ConfirmTracker::kMaxOpen; i++)
		CHECK(send(tr, "/playlist/next", 0).empty());
	CHECK(tr.open_expectations() == ConfirmTracker::kMaxOpen);

	const std::vector<ConfirmEvent> ev = send(tr, "/playlist/next", 0);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == ConfirmEvent::Kind::Missed);
	CHECK(tr.open_expectations() == ConfirmTracker::kMaxOpen);
	CHECK(tr.counters().missed == 1);
}

TEST_CASE("link state: sending, then linked for 5 s after a datagram, error from the caller")
{
	constexpr int64_t kSec = 1000 * kMs;

	ConfirmTracker tr;
	CHECK(tr.link_state(0) == LinkState::Sending);
	CHECK(link_state_name(LinkState::Sending) == std::string("sending"));

	tr.on_datagram(kSec);
	CHECK(tr.link_state(kSec) == LinkState::Linked);
	CHECK(tr.link_state(kSec + 5 * kSec) == LinkState::Linked); // exactly 5 s later: still inside the window
	CHECK(link_state_name(tr.link_state(kSec + 5 * kSec)) == std::string("linked"));
	CHECK(tr.last_feedback_ns() == kSec);
	CHECK(tr.link_state(kSec + 5 * kSec + 1) == LinkState::Sending);

	// The caller reports a socket error or a send-queue overflow; the next datagram clears it.
	tr.set_error(true);
	CHECK(tr.error());
	CHECK(tr.link_state(kSec + 5 * kSec + 1) == LinkState::Error);
	CHECK(link_state_name(LinkState::Error) == std::string("error"));
	tr.on_datagram(20 * kSec);
	CHECK(!tr.error());
	CHECK(tr.link_state(20 * kSec) == LinkState::Linked);
	CHECK(tr.link_state(20 * kSec + 5 * kSec + 1) == LinkState::Sending);
}

TEST_CASE("a confirmation keeps the link up even without a separate datagram")
{
	ConfirmTracker tr;
	CHECK(send(tr, "/playlist/next", 0).empty());
	CHECK(tr.on_scene("fake", 3 * kMs)[0].kind == ConfirmEvent::Kind::Confirmed);
	CHECK(tr.link_state(3 * kMs) == LinkState::Linked);
	CHECK(tr.link_state(3 * kMs + ConfirmTracker::kLinkedWindowNs) == LinkState::Linked);
	CHECK(tr.link_state(3 * kMs + ConfirmTracker::kLinkedWindowNs + 1) == LinkState::Sending);
}
