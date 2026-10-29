#pragma once
// Scene Director core: one-Next-per-section guard. Pure C++17, no libobs/Qt.
//
// Several Scene Director sources share the one Music Director clock, so a section edge is seen by
// all of them at once. Exactly one of them must send the Synesthesia Next for that edge. The OBS
// layer keeps ONE static SeqGuard for the process; before sending a section-triggered Next it
// claims the edge's section_seq. The first source to claim a seq wins, the rest skip.
// Manual Next / Prev are not deduplicated and never go through this.
#include <atomic>
#include <cstdint>
#include <limits>

namespace cd {

class SeqGuard {
public:
	// True for the first caller of `seq` (and for any seq newer than the last claimed one);
	// false for a seq that has already been claimed. Thread-safe.
	bool claim(int64_t seq)
	{
		int64_t seen = last_.load(std::memory_order_relaxed);
		for (;;) {
			if (seq <= seen)
				return false;
			if (last_.compare_exchange_weak(seen, seq, std::memory_order_acq_rel,
							std::memory_order_relaxed))
				return true;
			/* `seen` was refreshed by compare_exchange_weak: retry */
		}
	}

	// The newest claimed sequence (sentinel when nothing was claimed yet).
	int64_t claimed() const { return last_.load(std::memory_order_relaxed); }

private:
	static constexpr int64_t kNone = std::numeric_limits<int64_t>::min();
	std::atomic<int64_t> last_{kNone};
};

} // namespace cd
