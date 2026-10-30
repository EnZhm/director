#pragma once
// Scene Director core: process-wide exactly-once guard, keyed by (event kind, key). Pure C++17,
// no libobs/Qt.
//
// Several Scene Director sources share the one Music Director clock (and one palette feed), so a
// musical event is seen by all of them at once. Exactly one source must act on each event. The OBS
// layer keeps ONE static EventGuard for the process; before sending it claims the event's
// (kind, key) pair. The first source to claim a pair wins, the rest skip.
//
// This generalises SeqGuard (seq_guard.h): SeqGuard deduplicates a single monotonically increasing
// sequence; EventGuard deduplicates arbitrary (kind, key) pairs compared lexicographically (the
// pair's operator< is kind-first, then key). Keys are the spec 4.2 exactly-once keys rendered as
// strings; manual events carry an empty key and never go through the guard.
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace cd {

class EventGuard {
public:
	// True for the first caller of (kind, key); false if it was already claimed. Thread-safe.
	bool claim(const std::string &kind, const std::string &key)
	{
		std::lock_guard<std::mutex> lock(mu_);
		return claimed_.emplace(kind, key).second;
	}

	// Number of pairs claimed so far (for tests / diagnostics).
	size_t claimed() const
	{
		std::lock_guard<std::mutex> lock(mu_);
		return claimed_.size();
	}

private:
	mutable std::mutex mu_;
	std::set<std::pair<std::string, std::string>> claimed_; // lexicographic (kind, key)
};

// One source's side of the guard. A musical event can produce several sends on one source (a
// mapping with several actions, several mappings on the same event); the source claims the event
// once and then owns it, so all of its own sends for that event go out while every other source
// skips it. Sends with an empty key (manual presses, scene_loaded) are always this source's own.
// Not thread-safe on its own: the source calls it under its own mutex.
class SourceClaims {
public:
	bool may_send(EventGuard &guard, const std::string &kind, const std::string &key)
	{
		if (key.empty())
			return true;
		const std::pair<std::string, std::string> k(kind, key);
		if (owned_.count(k))
			return true;
		if (!guard.claim(kind, key))
			return false;
		owned_.insert(k);
		order_.push_back(k);
		// Only recent events can still have sends pending; keep the set bounded in a long set.
		if (order_.size() > kMaxOwned) {
			owned_.erase(order_.front());
			order_.pop_front();
		}
		return true;
	}

	size_t owned() const { return owned_.size(); }

private:
	static constexpr size_t kMaxOwned = 1024;
	std::set<std::pair<std::string, std::string>> owned_;
	std::deque<std::pair<std::string, std::string>> order_;
};

} // namespace cd
