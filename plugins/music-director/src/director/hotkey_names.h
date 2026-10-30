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

/* The md_* frontend hotkey table: names, locale keys and the Palette Director 1.2.0 names they
 * migrate from. Pure (no libobs) so the table is unit-tested; obs/md-hotkeys.cpp registers it.
 * The names are stable: saved bindings (music_director.hotkeys), Stream Deck's OBS hotkey action
 * and the 2.0 MIDI Director mappings kept in old scene collections all refer to them. */

#pragma once

namespace mdhotkey {

constexpr const char *kPhraseNow = "md_phrase_now";
constexpr const char *kDropNow = "md_drop_now";
constexpr const char *kForceBreakdown = "md_force_breakdown";
constexpr const char *kTapTempo = "md_tap_tempo";
constexpr const char *kLockBpm = "md_lock_bpm";
constexpr const char *kNudgeBack = "md_nudge_back";
constexpr const char *kNudgeFwd = "md_nudge_fwd";

struct Name {
	const char *name;     // OBS hotkey name, saved with bindings
	const char *text_key; // locale key of the Settings > Hotkeys description
	const char *pd_name;  // Palette Director 1.2.0 clock hotkey it migrates from
	const char *label;    // short action name used in rebind hints
};

constexpr Name kNames[] = {
	{kPhraseNow, "HotkeyPhraseNow", "pd_phrase_now", "Phrase is now"},
	{kDropNow, "HotkeyDropNow", "pd_drop_now", "Drop now"},
	{kForceBreakdown, "HotkeyForceBreakdown", "pd_force_breakdown", "Force breakdown"},
	{kTapTempo, "HotkeyTapTempo", "pd_tap_tempo", "Tap tempo"},
	{kLockBpm, "HotkeyLockBpm", "pd_lock_bpm", "Lock BPM"},
	{kNudgeBack, "HotkeyNudgeBack", "pd_nudge_back", "Nudge back"},
	{kNudgeFwd, "HotkeyNudgeFwd", "pd_nudge_fwd", "Nudge forward"},
};
constexpr int kCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

// The table entry for an md_* name, or nullptr.
inline const Name *find(const char *name)
{
	if (!name)
		return nullptr;
	for (const Name &n : kNames) {
		const char *a = n.name;
		const char *b = name;
		while (*a && *a == *b) {
			++a;
			++b;
		}
		if (*a == *b)
			return &n;
	}
	return nullptr;
}

} // namespace mdhotkey
