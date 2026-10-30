#include "confirm.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

namespace cd {

namespace {

constexpr int64_t kNsPerMs = 1000000;
constexpr char kScenesPrefix[] = "/scenes/";

std::string ms_text(int64_t ns)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(ns) / static_cast<double>(kNsPerMs));
	return buf;
}

} // namespace

std::string osc_slug(const std::string &name)
{
	std::string s;
	s.reserve(name.size());
	for (const char c : name) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (c == ' ' || c == '_' || c == '-' || c == '\t')
			continue;
		s.push_back(static_cast<char>(std::tolower(u)));
	}
	return s;
}

const char *ConfirmEvent::kind_name() const
{
	switch (kind) {
	case Kind::Confirmed:
		return "confirmed";
	case Kind::Missed:
		return "missed";
	case Kind::Mismatch:
		return "mismatch";
	case Kind::External:
		return "external";
	}
	return "?";
}

const char *link_state_name(LinkState state)
{
	switch (state) {
	case LinkState::Sending:
		return "sending";
	case LinkState::Linked:
		return "linked";
	case LinkState::Error:
		return "error";
	}
	return "?";
}

ConfirmTracker::ConfirmTracker(int64_t default_timeout_ms) : default_timeout_ms_(default_timeout_ms) {}

bool ConfirmTracker::scene_changing(const std::string &route)
{
	if (route.rfind(kScenesPrefix, 0) == 0)
		return true;
	return route == "/playlist/next" || route == "/playlist/previous" || route == "/playlist/position" ||
	       route == "/playlist/select";
}

ConfirmEvent ConfirmTracker::miss_event(const Expectation &e, int64_t now_ns)
{
	ConfirmEvent ev;
	ev.kind = ConfirmEvent::Kind::Missed;
	ev.now_ns = now_ns;
	ev.route = e.route;
	ev.args = e.args;
	ev.text = "no confirmation for " + e.route + " after " + std::to_string(e.timeout_ms) + " ms";
	counters_.missed++;
	return ev;
}

std::vector<ConfirmEvent> ConfirmTracker::expire(int64_t now_ns)
{
	std::vector<ConfirmEvent> out;
	for (auto it = open_.begin(); it != open_.end();) {
		if (now_ns >= it->deadline_ns) {
			out.push_back(miss_event(*it, now_ns));
			it = open_.erase(it);
		} else {
			++it;
		}
	}
	return out;
}

std::vector<ConfirmEvent> ConfirmTracker::on_sent(const std::string &route, const std::vector<std::string> &args,
						  int64_t now_ns, int64_t timeout_ms)
{
	std::vector<ConfirmEvent> out;
	if (!scene_changing(route))
		return out;
	if (timeout_ms <= 0)
		timeout_ms = default_timeout_ms_;

	Expectation e;
	e.route = route;
	e.args = args;
	e.sent_ns = now_ns;
	e.timeout_ms = timeout_ms;
	e.deadline_ns = now_ns + timeout_ms * kNsPerMs;
	if (route.rfind(kScenesPrefix, 0) == 0) {
		e.scene_send = true;
		e.expected_scene = route.substr(sizeof(kScenesPrefix) - 1);
		e.expected_slug = osc_slug(e.expected_scene);
	}

	// Bounded memory: the oldest outstanding expectation gives up its slot.
	if (open_.size() >= kMaxOpen) {
		out.push_back(miss_event(open_.front(), now_ns));
		open_.pop_front();
	}
	open_.push_back(std::move(e));
	return out;
}

std::vector<ConfirmEvent> ConfirmTracker::on_scene(const std::string &name, int64_t now_ns)
{
	// Anything already past its deadline is late, not a confirmation (spec 4.6: a report confirms
	// only before the deadline), so it is reported first even if the caller did not tick.
	std::vector<ConfirmEvent> out = expire(now_ns);
	on_datagram(now_ns);

	if (open_.empty()) {
		ConfirmEvent ev;
		ev.kind = ConfirmEvent::Kind::External;
		ev.now_ns = now_ns;
		ev.scene = name;
		ev.text = "external scene change: " + name;
		counters_.external++;
		out.push_back(std::move(ev));
		return out;
	}

	Expectation e = std::move(open_.front());
	open_.pop_front();

	if (e.scene_send && osc_slug(name) != e.expected_slug) {
		ConfirmEvent ev;
		ev.kind = ConfirmEvent::Kind::Mismatch;
		ev.now_ns = now_ns;
		ev.route = e.route;
		ev.args = e.args;
		ev.scene = name;
		ev.expected = e.expected_scene;
		ev.text = "mismatch: sent " + e.expected_scene + ", Synesthesia reports " + name;
		counters_.mismatched++;
		out.push_back(std::move(ev));
		return out;
	}

	ConfirmEvent ev;
	ev.kind = ConfirmEvent::Kind::Confirmed;
	ev.now_ns = now_ns;
	ev.route = e.route;
	ev.args = e.args;
	ev.scene = name;
	ev.latency_ns = now_ns - e.sent_ns;
	ev.text = "confirm " + name + " in " + ms_text(ev.latency_ns) + " ms";
	counters_.confirmed++;
	note_confirmed(ev.latency_ns);
	out.push_back(std::move(ev));
	return out;
}

std::vector<ConfirmEvent> ConfirmTracker::tick(int64_t now_ns)
{
	return expire(now_ns);
}

void ConfirmTracker::on_datagram(int64_t now_ns)
{
	feedback_seen_ = true;
	last_feedback_ns_ = now_ns;
	error_ = false;
}

void ConfirmTracker::set_error(bool error)
{
	error_ = error;
}

LinkState ConfirmTracker::link_state(int64_t now_ns) const
{
	if (error_)
		return LinkState::Error;
	if (feedback_seen_ && now_ns <= last_feedback_ns_ + kLinkedWindowNs)
		return LinkState::Linked;
	return LinkState::Sending;
}

void ConfirmTracker::note_confirmed(int64_t latency_ns)
{
	latencies_.push_back(latency_ns);
	while (latencies_.size() > kLatencyWindow)
		latencies_.pop_front();
}

int64_t ConfirmTracker::last_latency_ns() const
{
	return latencies_.empty() ? -1 : latencies_.back();
}

int64_t ConfirmTracker::median_latency_ns() const
{
	if (latencies_.empty())
		return -1;
	std::vector<int64_t> v(latencies_.begin(), latencies_.end());
	std::sort(v.begin(), v.end());
	const std::size_t n = v.size();
	if (n % 2 == 1)
		return v[n / 2];
	return (v[n / 2 - 1] + v[n / 2]) / 2;
}

int64_t ConfirmTracker::p90_latency_ns() const
{
	if (latencies_.empty())
		return -1;
	std::vector<int64_t> v(latencies_.begin(), latencies_.end());
	std::sort(v.begin(), v.end());
	// Nearest rank in integers: ceil(0.9 * n) == (9 * n + 9) / 10.
	std::size_t rank = (9 * v.size() + 9) / 10;
	if (rank < 1)
		rank = 1;
	if (rank > v.size())
		rank = v.size();
	return v[rank - 1];
}

} // namespace cd
