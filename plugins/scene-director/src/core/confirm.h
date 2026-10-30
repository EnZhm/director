#pragma once
// Scene Director core: OSC feedback confirmation tracker. Pure C++17, time injected (ns), no
// sockets, no libobs. Spec: docs/spec/director-2.1.0.md section 4.6.
//
// Scene Director sends to Synesthesia and never sees the result of a send directly. Synesthesia
// reports every scene change on `/scenes/{name}`, so a report that arrives after a scene-changing
// send is the proof that the send landed:
//
//   - `on_sent` arms one expectation per scene-changing send (`/playlist/next|previous|position|
//     select`, `/scenes/*`), with a deadline of `now + confirm_timeout_ms`;
//   - `on_scene` confirms the OLDEST open expectation (reports are serialised by Synesthesia, so
//     FIFO is what the send order produces), records the latency and logs it. For a `/scenes/{x}`
//     send the reported slug must be the one that was sent, else the report is a MISMATCH (both
//     names are logged). A report with nothing open is an EXTERNAL scene change;
//   - `tick` reports the expectations whose deadline has passed as "no confirmation".
//
// There is deliberately no resend: relative routes are not idempotent (a late confirmation plus a
// resend skips two scenes) and a resend after the beat is off-beat on stage. Counters and the log
// make every miss visible instead (spec 4.6, decision 2026-10-30).
//
// Link state (spec 4.6): `linked` while a feedback datagram arrived in the last 5 s (or a
// confirmation, which is a datagram), `sending` while sends go out with nothing coming back,
// `error` when the caller saw a socket error or a send-queue overflow. `on_datagram` feeds the 5 s
// window; the SD-SOURCE wiring calls it for every datagram the receiver queue yields.
//
// Log text: ConfirmEvent::text is exactly the spec 4.6 line WITHOUT the log prefix — the source
// logs `[scene-director] <text>`, like the switch scheduler's decision log.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace cd {

// Synesthesia's scene-name slug, byte-identical to common/osc `director_osc::osc_slug`:
// lowercase, and ' ', '_', '-', '\t' dropped. It lives here as a local copy so the core does not
// depend on the transport; SD-SOURCE switches the call sites over to director_osc::osc_slug.
std::string osc_slug(const std::string &name);

// One result of a scene-changing send. `text` is the spec 4.6 log line without the prefix.
struct ConfirmEvent {
	enum class Kind {
		Confirmed, // a report arrived for an open expectation, in time
		Missed,    // the deadline passed with no report
		Mismatch,  // a report arrived, but not for the scene that was sent
		External,  // a report arrived with no expectation open
	};

	Kind kind = Kind::Confirmed;
	int64_t now_ns = 0;
	// What Scene Director sent (empty for External).
	std::string route;
	std::vector<std::string> args;
	// The `/scenes/{name}` name Synesthesia reported (every kind except Missed).
	std::string scene;
	// The scene a `/scenes/{x}` send asked for, raw (Mismatch only).
	std::string expected;
	// `now - sent` for a confirmation, -1 otherwise (a mismatch records no latency).
	int64_t latency_ns = -1;
	std::string text;

	const char *kind_name() const;
};

struct ConfirmCounters {
	int64_t confirmed = 0;
	int64_t missed = 0;
	int64_t mismatched = 0;
	int64_t external = 0;
};

enum class LinkState { Sending, Linked, Error };
const char *link_state_name(LinkState state);

class ConfirmTracker {
public:
	static constexpr int64_t kDefaultTimeoutMs = 2000;     // spec 4.1 default
	static constexpr int64_t kLinkedWindowNs = 5000000000; // 5 s (spec 4.6)
	static constexpr std::size_t kLatencyWindow = 64;      // last 64 confirmations (spec 4.6)
	// Outstanding expectations are bounded: a send that is never confirmed is expired by the next
	// tick anyway, so this only comes into play when the caller stops ticking.
	static constexpr std::size_t kMaxOpen = 64;

	explicit ConfirmTracker(int64_t default_timeout_ms = kDefaultTimeoutMs);

	// A scene-changing send. `/playlist/next`, `/playlist/previous`, `/playlist/position`,
	// `/playlist/select` and `/scenes/*` arm an expectation with deadline now + timeout_ms
	// (timeout_ms <= 0 uses the tracker's default); every other route arms nothing and returns
	// nothing. `args` are carried for logging only. Returns the "no confirmation" of the oldest
	// expectation when more than kMaxOpen are outstanding, otherwise an empty vector.
	std::vector<ConfirmEvent> on_sent(const std::string &route, const std::vector<std::string> &args,
					  int64_t now_ns, int64_t timeout_ms);

	// A `/scenes/{name}` report from Synesthesia: expires anything already past its deadline,
	// then confirms the oldest open expectation (latency = now - sent) or reports a mismatch for a
	// `/scenes/{x}` send whose slug differs. With nothing open it is an external scene change.
	// The report is a datagram: it also feeds the link window.
	std::vector<ConfirmEvent> on_scene(const std::string &name, int64_t now_ns);

	// Expires every expectation whose deadline has been reached (now_ns >= deadline) as
	// "no confirmation", oldest first. A report at the deadline is therefore late, not a confirm.
	std::vector<ConfirmEvent> tick(int64_t now_ns);

	// A feedback datagram arrived (any route): feeds the 5 s link window and clears an error.
	void on_datagram(int64_t now_ns);

	// Set by the caller on a socket error or a send-queue overflow (spec 4.6).
	void set_error(bool error);
	bool error() const { return error_; }

	LinkState link_state(int64_t now_ns) const;

	const ConfirmCounters &counters() const { return counters_; }
	// Latency of the most recent confirmation, -1 when there has not been one yet.
	int64_t last_latency_ns() const;
	// Median of the last kLatencyWindow confirmations: the middle sample, or the floored mean of
	// the two middle samples for an even count. -1 when there has not been one yet.
	int64_t median_latency_ns() const;
	// 90th percentile (nearest rank: the ceil(0.9 * n)-th smallest). -1 when empty.
	int64_t p90_latency_ns() const;
	std::size_t confirmations() const { return latencies_.size(); }
	std::size_t open_expectations() const { return open_.size(); }
	int64_t last_feedback_ns() const { return last_feedback_ns_; }

private:
	struct Expectation {
		std::string route;
		std::vector<std::string> args;
		int64_t sent_ns = 0;
		int64_t deadline_ns = 0;
		int64_t timeout_ms = 0;
		bool scene_send = false;    // `/scenes/{x}`: the report must carry the same slug
		std::string expected_scene; // the raw name in the route
		std::string expected_slug;  // its osc_slug
	};

	static bool scene_changing(const std::string &route);
	std::vector<ConfirmEvent> expire(int64_t now_ns);
	ConfirmEvent miss_event(const Expectation &e, int64_t now_ns);
	void note_confirmed(int64_t latency_ns);

	int64_t default_timeout_ms_ = kDefaultTimeoutMs;
	std::deque<Expectation> open_;  // oldest first
	std::deque<int64_t> latencies_; // last kLatencyWindow confirmations, ns
	ConfirmCounters counters_;
	bool error_ = false;
	bool feedback_seen_ = false;
	int64_t last_feedback_ns_ = 0;
};

} // namespace cd
