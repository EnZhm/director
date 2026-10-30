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

} // namespace cd
