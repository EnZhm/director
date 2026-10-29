#pragma once
// Palette Director v1.1 clock API: the libobs-free part (extrapolation + instance lookup),
// shared by the proc-handler glue in palette-lock-host.cpp and the unit tests.
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace md {

// Beat position at `now_ns`, extrapolated from a snapshot whose beat position refers to
// `sample_ns` (an OBS audio timestamp, which can sit slightly ahead of or behind the caller's
// clock). Extrapolation runs both ways and is capped at `max_s`, so a stalled feed (no audio
// arriving) does not run the clock away. A paused clock or one with no tempo does not move.
inline double clock_extrapolate(double beats, double bpm, uint64_t sample_ns, uint64_t now_ns, bool paused,
				double max_s = 0.5)
{
	if (paused || bpm <= 0.0)
		return beats;
	double dt = (static_cast<double>(now_ns) - static_cast<double>(sample_ns)) * 1e-9;
	if (dt > max_s)
		dt = max_s;
	if (dt < -max_s)
		dt = -max_s;
	return beats + dt * bpm / 60.0;
}

// Maps OBS audio timestamps onto the wall clock (os_gettime_ns).
// Audio reaches the worker in bursts, so (wall time when processed - audio timestamp) is a
// sawtooth: true offset + a delivery delay that is never negative. The sliding MINIMUM over
// `window_ns` recovers the offset without the burst jitter. It also works when a source's
// timestamps live in another domain (media sources can sit far from os_gettime_ns).
// A jump of more than `reset_ns` (source switched, media restarted) restarts the window.
class AudioClockMap {
public:
	explicit AudioClockMap(uint64_t window_ns = 2000000000ull, int64_t reset_ns = 500000000ll)
		: window_(window_ns),
		  reset_(reset_ns)
	{
	}

	// One observation: audio timestamp `audio_ts` was processed at wall time `wall_now`.
	// Returns the wall-clock time the audio timestamp corresponds to.
	uint64_t observe(uint64_t audio_ts, uint64_t wall_now)
	{
		const int64_t o = static_cast<int64_t>(wall_now - audio_ts); // wraps safely to signed
		if (!q_.empty()) {
			const int64_t m = q_.front().o;
			if (o - m > reset_ || m - o > reset_)
				q_.clear();
		}
		while (!q_.empty() && q_.back().o >= o)
			q_.pop_back();
		q_.push_back({wall_now, o});
		while (!q_.empty() && wall_now - q_.front().t > window_)
			q_.pop_front();
		return audio_ts + static_cast<uint64_t>(q_.front().o);
	}

	bool empty() const { return q_.empty(); }
	int64_t offset() const { return q_.empty() ? 0 : q_.front().o; }
	void reset() { q_.clear(); }

private:
	struct Obs {
		uint64_t t;
		int64_t o;
	};
	uint64_t window_;
	int64_t reset_;
	std::deque<Obs> q_; // monotonic queue: offsets increase front -> back, front = window minimum
};

struct ClockInstance {
	uint64_t id = 0;
	std::string parent; // source the Palette Lock filter sits on
	std::string filter; // filter name
};

// Pick the instance a caller asked for. Priority: id (> 0), then parent source name,
// then filter name, then (no selector given at all) the oldest instance.
// Returns 0 when nothing matches, including when a selector was given but did not match
// (a named request never silently falls back to another instance).
inline uint64_t clock_resolve(const std::vector<ClockInstance> &all, uint64_t id, const std::string &parent,
			      const std::string &filter)
{
	if (id > 0) {
		for (const auto &c : all)
			if (c.id == id)
				return c.id;
		return 0;
	}
	if (!parent.empty()) {
		for (const auto &c : all)
			if (c.parent == parent && (filter.empty() || c.filter == filter))
				return c.id;
		return 0;
	}
	if (!filter.empty()) {
		for (const auto &c : all)
			if (c.filter == filter)
				return c.id;
		return 0;
	}
	uint64_t best = 0;
	for (const auto &c : all)
		if (best == 0 || c.id < best)
			best = c.id;
	return best;
}

// Minimal JSON string escaping for the instance list.
inline std::string json_escape(const std::string &s)
{
	std::string o;
	o.reserve(s.size() + 2);
	for (unsigned char ch : s) {
		switch (ch) {
		case '"':
			o += "\\\"";
			break;
		case '\\':
			o += "\\\\";
			break;
		case '\n':
			o += "\\n";
			break;
		case '\r':
			o += "\\r";
			break;
		case '\t':
			o += "\\t";
			break;
		default:
			if (ch < 0x20) {
				static const char hex[] = "0123456789abcdef";
				o += "\\u00";
				o += hex[ch >> 4];
				o += hex[ch & 15];
			} else {
				o += static_cast<char>(ch);
			}
		}
	}
	return o;
}

inline std::string clock_instances_json(const std::vector<ClockInstance> &all)
{
	std::string o = "[";
	for (std::size_t i = 0; i < all.size(); ++i) {
		if (i)
			o += ",";
		o += "{\"id\":" + std::to_string(all[i].id) + ",\"source\":\"" + json_escape(all[i].parent) +
		     "\",\"filter\":\"" + json_escape(all[i].filter) + "\"}";
	}
	return o + "]";
}

} // namespace md
