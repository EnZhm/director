// SeqGuard: exactly one Scene Director source sends the Next for a section edge, even when several
// sources (or threads) claim it at once.
#include "doctest.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "seq_guard.h"

using namespace cd;

TEST_CASE("two claimants for one sequence: exactly one wins")
{
	SeqGuard g;
	CHECK(g.claim(5));
	CHECK_FALSE(g.claim(5));
	CHECK_FALSE(g.claim(5));
}

TEST_CASE("only a newer sequence can be claimed")
{
	SeqGuard g;
	CHECK(g.claim(3));
	CHECK_FALSE(g.claim(2));
	CHECK_FALSE(g.claim(3));
	CHECK(g.claim(4));
	CHECK(g.claim(9));
	CHECK_FALSE(g.claim(8));
}

TEST_CASE("sequence 0 is claimable and equal or older ones are refused")
{
	SeqGuard g;
	CHECK(g.claim(0));
	CHECK_FALSE(g.claim(0));
	CHECK(g.claim(1));
}

TEST_CASE("concurrent claims for one sequence: exactly one wins")
{
	constexpr int kThreads = 16;
	for (int round = 0; round < 200; ++round) {
		SeqGuard g;
		std::atomic<int> winners{0};
		std::vector<std::thread> threads;
		threads.reserve(kThreads);
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&g, &winners, round] {
				if (g.claim(round + 1))
					winners.fetch_add(1, std::memory_order_relaxed);
			});
		}
		for (std::thread &t : threads)
			t.join();
		REQUIRE(winners.load() == 1);
	}
}
