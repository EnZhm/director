#pragma once
#include <atomic>
#include <cstddef>
#include <vector>

namespace md {

// Music Director owns THE clock. Its commands only touch the beat grid / section,
// never palettes (palette scheduling stayed in Palette Director).
enum class Command {
	PhraseNow,      // re-anchor the phrase/bar grid so the current beat is a phrase downbeat
	DropNow,        // force section DROP on the next beat
	ForceBreakdown, // force section BREAKDOWN on the next beat
	TapTempo,       // tempo from tap times
	LockBpm,        // toggle: freeze tempo tracking (a tap sets and holds the tempo)
	NudgeBack,      // shift the beat phase back a small step
	NudgeFwd        // shift the beat phase forward a small step
};

struct CommandMsg {
	Command cmd = Command::PhraseNow;
	int palette_index = -1; // unused by the clock; kept so a caller can pass extra data
};

// Lock-free single-producer/single-consumer queue (hotkey/dock thread -> worker thread).
class CommandQueue {
public:
	explicit CommandQueue(std::size_t capacity = 1024)
		: capacity_(capacity + 1),
		  buf_(capacity + 1),
		  head_(0),
		  tail_(0)
	{
	}

	bool push(const CommandMsg &msg)
	{
		const std::size_t h = head_.load(std::memory_order_relaxed);
		const std::size_t t = tail_.load(std::memory_order_acquire);
		const std::size_t next = (t + 1) % capacity_;
		if (next == h)
			return false; // full
		buf_[t] = msg;
		tail_.store(next, std::memory_order_release);
		return true;
	}

	bool pop(CommandMsg &msg)
	{
		const std::size_t h = head_.load(std::memory_order_relaxed);
		const std::size_t t = tail_.load(std::memory_order_acquire);
		if (h == t)
			return false; // empty
		msg = buf_[h];
		head_.store((h + 1) % capacity_, std::memory_order_release);
		return true;
	}

	bool empty() const { return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire); }

private:
	std::size_t capacity_;
	std::vector<CommandMsg> buf_;
	std::atomic<std::size_t> head_;
	std::atomic<std::size_t> tail_;
};

} // namespace md
