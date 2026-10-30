/*
Music Director
Copyright (C) 2026 EnZhm

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/* MIDI Director was removed in 2.1 (spec 6). Scene collections saved by 2.0 still hold its
 * mappings under the "midi_director" key: a JSON string, format v1
 *   {"version":1,"mappings":[{"type":"note"|"cc","channel":1..16,"number":0..127,"mode":...,
 *     "name":"<hotkey name>","registerer":...,"registerer_name":...,"description":...}]}
 * This pure layer (no libobs) keeps that value verbatim across load/save, reads it tolerantly for
 * the removal log, and words one rebind hint per mapped target. Nothing here throws. */

#pragma once

#include <set>
#include <string>
#include <vector>

namespace md {

struct LegacyMapping {
	std::string control;     // "Note 36 ch 1" / "CC 7 ch 2" (as MIDI Director labelled it)
	std::string target;      // hotkey name, e.g. "md_drop_now"
	std::string description; // the description saved with the mapping (may be empty)
};

struct LegacyMidi {
	bool parsed = false; // the value was well-formed JSON with a top-level object
	std::vector<LegacyMapping> mappings;
};

// Tolerant v1 parse. Malformed input yields parsed=false and no mappings; unknown keys are skipped;
// a mapping row without a hotkey name is skipped. Never throws.
LegacyMidi parse_legacy_midi(const std::string &json) noexcept;

// The action a target stands for in hints: the md_* label ("Drop now"), else the saved
// description, else the hotkey name.
std::string legacy_target_label(const LegacyMapping &m);

// What to do instead, per spec 6.2:
//   md_drop_now                       -> "bind Drop now in Settings > Hotkeys or on a Stream Deck"
//   scene_director_next / _prev       -> "map the control in Synesthesia's MIDI panel"
//   anything else                     -> "bind <label> in Settings > Hotkeys"
std::string rebind_hint(const LegacyMapping &m);

// "MIDI Director mapping '<control>' -> <target> (<label>) is inactive: <hint>"
std::string rebind_line(const LegacyMapping &m);

// "MIDI Director was removed in 2.1: <N> mappings kept in the scene collection (inactive), copy at <path>"
std::string removal_line(size_t count, const std::string &copy_path);

// File name of the one-time JSON copy for a collection: "midi-director-<collection>.json", with
// characters outside [A-Za-z0-9 ._-] replaced by '_' (and an empty name as "_").
std::string legacy_copy_name(const std::string &collection);

// The collection's raw "midi_director" value, held so it is written back byte for byte on save
// (OBS rebuilds save data from the callbacks; an unread key would be lost).
class LegacyMidiStore {
public:
	// Called on every collection load / switch: replaces the held value (nullptr / "" = none).
	void load(const char *raw);
	bool present() const { return !raw_.empty(); }
	const std::string &raw() const { return raw_; }

	// True the first time a collection is reported in this OBS session (log + copy once).
	bool first_report(const std::string &collection);

private:
	std::string raw_;
	std::set<std::string> reported_;
};

} // namespace md
