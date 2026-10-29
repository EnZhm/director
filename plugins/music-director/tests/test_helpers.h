#pragma once
#include <algorithm>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "clock.h"
#include "gen_fixtures.h"

namespace mdtest {

// Clock configuration with the defaults the tests tweak (phrase length / fallback BPM).
inline md::ClockConfig make_cfg(int phrase_beats = 32, float fallback_bpm = 128.0f)
{
	md::ClockConfig c;
	c.phrase_beats = phrase_beats;
	c.fallback_bpm = fallback_bpm;
	return c;
}

// Feed a mono float buffer in fixed-size blocks.
inline void feed(md::MusicClock &d, const std::vector<float> &mono, uint32_t sr, std::size_t block = 1024)
{
	for (std::size_t i = 0; i < mono.size(); i += block) {
		const std::size_t n = std::min(block, mono.size() - i);
		d.push_audio(mono.data() + i, n, sr);
	}
}

// Push one 256-sample hop at a time and take the clock state after every hop (the AUDIT card's view).
inline void feed_hopwise(md::MusicClock &d, const std::vector<float> &mono, uint32_t sr,
			 const std::function<void(const md::ClockState &, const std::vector<md::ClockEvent> &)> &on_hop)
{
	for (std::size_t i = 0; i + 256 <= mono.size(); i += 256) {
		const std::vector<md::ClockEvent> evs = d.push_hop(mono.data() + i, 256, sr);
		on_hop(d.snapshot(), evs);
	}
}

// Generate -> write WAV -> read back -> feed (full WAV round-trip, same as PD 1.2.0's suite).
inline void feed_roundtrip(md::MusicClock &d, const WavMono &w, const std::string &dir, const std::string &name,
			   std::size_t block = 1024)
{
	const std::string path = dir + "/" + name;
	REQUIRE(w.write(path));
	WavMono back;
	REQUIRE(WavMono::read(path, back));
	REQUIRE(back.sample_rate == w.sample_rate);
	REQUIRE(back.samples.size() == w.samples.size());
	feed(d, back.to_float(), back.sample_rate, block);
}

inline std::string fixture_dir()
{
	const std::filesystem::path p = std::filesystem::temp_directory_path() / "md_director_tests";
	std::filesystem::create_directories(p);
	return p.string();
}

inline double beats_of(const md::MusicClock &d)
{
	const md::ClockState st = d.snapshot();
	return static_cast<double>(st.beat) + st.beat_frac;
}

inline std::vector<md::ClockEvent> of_type(const std::vector<md::ClockEvent> &evs, md::ClockEvent::Type t)
{
	std::vector<md::ClockEvent> out;
	for (const auto &e : evs)
		if (e.type == t)
			out.push_back(e);
	return out;
}

inline std::vector<md::ClockEvent> section_changes(const std::vector<md::ClockEvent> &evs)
{
	return of_type(evs, md::ClockEvent::Type::SectionChange);
}

inline std::vector<md::ClockEvent> phrases(const std::vector<md::ClockEvent> &evs)
{
	return of_type(evs, md::ClockEvent::Type::Phrase);
}

inline std::vector<md::ClockEvent> beats(const std::vector<md::ClockEvent> &evs)
{
	return of_type(evs, md::ClockEvent::Type::Beat);
}

inline std::vector<md::ClockEvent> commands(const std::vector<md::ClockEvent> &evs)
{
	return of_type(evs, md::ClockEvent::Type::Command);
}

// Find the beat of the first section change to `sec` (>= 0 if any).
inline int64_t section_beat(const std::vector<md::ClockEvent> &evs, md::Section sec)
{
	for (const auto &e : evs)
		if (e.type == md::ClockEvent::Type::SectionChange && e.section == sec)
			return static_cast<int64_t>(e.beat);
	return -1;
}

inline bool has_detail(const std::vector<md::ClockEvent> &evs, const std::string &needle)
{
	for (const auto &e : evs)
		if (e.detail.find(needle) != std::string::npos)
			return true;
	return false;
}

// Deterministic serialization of an event log (for the determinism test).
inline std::string summarize(const std::vector<md::ClockEvent> &evs)
{
	std::ostringstream os;
	for (const auto &e : evs) {
		os << static_cast<int>(e.type) << '|' << e.beat << '|' << e.phrase << '|' << static_cast<int>(e.section)
		   << '|' << (e.forced ? 1 : 0) << '|' << e.detail << ';';
	}
	return os.str();
}

// FNV-1a 64 of the summary: a compact golden digest. The clock's event log must stay byte-identical
// for a given feed whenever no clock command is sent.
inline uint64_t fnv1a64(const std::string &s)
{
	uint64_t h = 1469598103934665603ull;
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

inline uint64_t digest(const std::vector<md::ClockEvent> &evs)
{
	return fnv1a64(summarize(evs));
}

} // namespace mdtest
