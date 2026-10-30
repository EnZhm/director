// EventGuard: process-wide exactly-once, keyed by (event kind, key), lexicographic, thread-safe.
#include "doctest.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "event_guard.h"

using namespace cd;

TEST_CASE("the same (kind, key) is claimed once")
{
	EventGuard g;
	CHECK(g.claim("breakdown", "1"));
	CHECK_FALSE(g.claim("breakdown", "1"));
	CHECK_FALSE(g.claim("breakdown", "1"));
	CHECK(g.claimed() == 1);
}

TEST_CASE("different kinds are independent")
{
	EventGuard g;
	CHECK(g.claim("breakdown", "1"));
	CHECK(g.claim("drop", "1"));
	CHECK(g.claim("section", "1"));
	CHECK(g.claimed() == 3);
}

TEST_CASE("keys compare lexicographically, not numerically")
{
	EventGuard g;
	CHECK(g.claim("breakdown", "10"));
	// "9" sorts after "10" lexicographically, so it is a distinct key, not an older one.
	CHECK(g.claim("breakdown", "9"));
	CHECK_FALSE(g.claim("breakdown", "9"));
}

TEST_CASE("tuple keys for phrase / bar are distinct")
{
	EventGuard g;
	CHECK(g.claim("phrase", "e2:p1"));
	CHECK(g.claim("phrase", "e2:p2"));
	CHECK_FALSE(g.claim("phrase", "e2:p1"));
	CHECK(g.claim("bar", "e2:b0"));
}

TEST_CASE("concurrent claims for one key: exactly one wins")
{
	for (int round = 0; round < 200; ++round) {
		EventGuard g;
		std::atomic<int> winners{0};
		std::vector<std::thread> threads;
		for (int t = 0; t < 16; ++t) {
			threads.emplace_back([&g, &winners] {
				if (g.claim("drop", "42"))
					winners.fetch_add(1, std::memory_order_relaxed);
			});
		}
		for (std::thread &t : threads)
			t.join();
		REQUIRE(winners.load() == 1);
	}
}

// ---- per-source ownership (SourceClaims) ----

TEST_CASE("a source that claims an event sends all of its own plans for it")
{
	EventGuard g;
	SourceClaims a, b;
	// source A has two mappings on the same drop (two actions): both go out
	CHECK(a.may_send(g, "drop", "7"));
	CHECK(a.may_send(g, "drop", "7"));
	// source B sees the same drop: skipped, for every one of its plans
	CHECK_FALSE(b.may_send(g, "drop", "7"));
	CHECK_FALSE(b.may_send(g, "drop", "7"));
	// the next drop can be won by either
	CHECK(b.may_send(g, "drop", "8"));
	CHECK_FALSE(a.may_send(g, "drop", "8"));
}

TEST_CASE("empty keys (manual, scene_loaded) are always the source's own")
{
	EventGuard g;
	SourceClaims a, b;
	CHECK(a.may_send(g, "manual_next", ""));
	CHECK(b.may_send(g, "manual_next", ""));
	CHECK(a.may_send(g, "scene_loaded", ""));
	CHECK(b.may_send(g, "scene_loaded", ""));
}

TEST_CASE("the owned set stays bounded in a long set")
{
	EventGuard g;
	SourceClaims a;
	for (int i = 0; i < 5000; ++i)
		REQUIRE(a.may_send(g, "bar", std::to_string(i)));
	CHECK(a.owned() <= 1024);
}
