#pragma once
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "palette-director.h"

/*
 * Scripted-clock test helpers (spec 7). The Palette Director core is a follower: it no longer
 * takes audio, it takes clock samples (struct director_clock) through pd::PaletteDirector::
 * on_clock(). These helpers build deterministic sample sequences and drive the core, so the
 * scheduler can be tested without Music Director, libobs or the audio stack.
 */
namespace pdtest {

// Build a set with n named palettes (4 colours each), defaults matching the mock DEF_DIR.
inline pd::SetFile make_set(int n_palettes, const std::string &breakdown_name = "")
{
	pd::SetFile s;
	s.name = "Test Set";
	for (int i = 0; i < n_palettes; ++i) {
		pd::PaletteSpec p;
		p.name = "Pal " + std::to_string(i);
		p.colours = {{0.0f, 0.0f, 0.0f}, {0.3f, 0.3f, 0.3f}, {0.7f, 0.7f, 0.7f}, {1.0f, 0.0f, 0.0f}};
		s.palettes.push_back(p);
	}
	s.palette_changes.breakdown_palette = breakdown_name;
	return s;
}

// One section span: a section starts at `beat` (integer) and runs until the next span.
struct SectionSpan {
	int beat;
	int section; // pd::Section
};

inline const std::vector<SectionSpan> kGrooveOnly = {{0, 0}};

// Build a scripted clock: a sample every `step` beats from 0 to total_beats inclusive.
// Sections switch at the given beats (section_seq increments on every change, as the contract
// promises); the phrase counter is floor(beat / phrase_beats).
inline std::vector<pd::ClockSample> script(int total_beats, int phrase_beats,
					   const std::vector<SectionSpan> &secs = kGrooveOnly, double step = 1.0,
					   double bpm = 128.0)
{
	std::vector<pd::ClockSample> out;
	const double ns_per_beat = 60.0 / bpm * 1e9;
	int64_t seq = 0;
	int cur_section = secs.empty() ? 0 : secs.front().section;
	int64_t section_beat = secs.empty() ? 0 : secs.front().beat;
	int64_t now = 1000000000;
	std::size_t si = 0;
	for (double t = 0.0; t <= static_cast<double>(total_beats) + 1e-9; t += step) {
		while (si + 1 < secs.size() && secs[si + 1].beat <= static_cast<int>(std::floor(t + 1e-9))) {
			++si;
			cur_section = secs[si].section;
			section_beat = secs[si].beat;
			++seq;
		}
		pd::ClockSample c;
		c.ok = true;
		c.beat = static_cast<int64_t>(std::floor(t + 1e-9));
		c.phrase = static_cast<int64_t>(std::floor(t / phrase_beats + 1e-9));
		c.phrase_beats = phrase_beats;
		c.section = cur_section;
		c.section_seq = seq;
		c.section_beat = section_beat;
		c.epoch = 0;
		c.beats_now = t;
		c.bpm = bpm;
		c.confidence = 0.8;
		c.bass = 0.5;
		c.silent = false;
		c.paused = false;
		c.now_ns = now;
		now += static_cast<int64_t>(step * ns_per_beat);
		out.push_back(c);
	}
	return out;
}

inline void run(pd::PaletteDirector &d, const std::vector<pd::ClockSample> &samples)
{
	for (const auto &s : samples)
		d.on_clock(s);
}

// Run samples [from, to) (used to interleave a command between clock samples in tests).
inline void run(pd::PaletteDirector &d, const std::vector<pd::ClockSample> &samples, std::size_t from, std::size_t to)
{
	for (std::size_t i = from; i < to && i < samples.size(); ++i)
		d.on_clock(samples[i]);
}

inline std::vector<pd::Event> palette_changes(const std::vector<pd::Event> &evs)
{
	std::vector<pd::Event> out;
	for (const auto &e : evs)
		if (e.type == pd::Event::Type::PaletteChange)
			out.push_back(e);
	return out;
}

inline std::vector<pd::Event> section_changes(const std::vector<pd::Event> &evs)
{
	std::vector<pd::Event> out;
	for (const auto &e : evs)
		if (e.type == pd::Event::Type::SectionChange)
			out.push_back(e);
	return out;
}

inline std::vector<pd::Event> lock_offs(const std::vector<pd::Event> &evs)
{
	std::vector<pd::Event> out;
	for (const auto &e : evs)
		if (e.type == pd::Event::Type::LockOff)
			out.push_back(e);
	return out;
}

inline std::vector<pd::Event> lock_ons(const std::vector<pd::Event> &evs)
{
	std::vector<pd::Event> out;
	for (const auto &e : evs)
		if (e.type == pd::Event::Type::LockOn)
			out.push_back(e);
	return out;
}

// Find the beat of the first event with the given section.
inline int64_t section_beat(const std::vector<pd::Event> &evs, pd::Section sec)
{
	for (const auto &e : evs)
		if (e.type == pd::Event::Type::SectionChange && e.section == sec)
			return static_cast<int64_t>(e.beat);
	return -1;
}

// Deterministic serialization of an event log.
inline std::string summarize(const std::vector<pd::Event> &evs)
{
	std::ostringstream os;
	for (const auto &e : evs) {
		os << static_cast<int>(e.type) << '|' << e.beat << '|' << e.phrase << '|' << static_cast<int>(e.section)
		   << '|' << e.palette << '|' << e.prev_palette << '|' << e.detail << ';';
	}
	return os.str();
}

} // namespace pdtest
