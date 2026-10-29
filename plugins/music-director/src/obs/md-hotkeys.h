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

/* Music Director hotkeys: md_phrase_now, md_drop_now, md_force_breakdown, md_tap_tempo, md_lock_bpm,
 * md_nudge_back, md_nudge_fwd as OBS frontend hotkeys, plus the listing and triggering helpers the
 * MIDI Director dock uses to drive ANY registered hotkey by identity (music, palette, clips,
 * Synesthesia). The list/trigger helpers are a no-op-free part of the core API; registering the md_*
 * hotkeys needs the frontend API. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void music_director_hotkey_load(void);
void music_director_hotkey_unload(void);

#ifdef __cplusplus
}

#include <obs.h>

#include <string>
#include <vector>

namespace mdhotkey {

// Stable hotkey names: MIDI mappings are saved with them, so they never change.
constexpr const char *kPhraseNow = "md_phrase_now";
constexpr const char *kDropNow = "md_drop_now";
constexpr const char *kForceBreakdown = "md_force_breakdown";
constexpr const char *kTapTempo = "md_tap_tempo";
constexpr const char *kLockBpm = "md_lock_bpm";
constexpr const char *kNudgeBack = "md_nudge_back";
constexpr const char *kNudgeFwd = "md_nudge_fwd";

// A hotkey identity as the MIDI Director dock stores it (mirrors cd::midi::Target).
struct Entry {
	std::string name;            // hotkey name, e.g. "md_drop_now"
	std::string registerer;      // "frontend", "source", "output", "encoder", "service"
	std::string registerer_name; // source name for source-level hotkeys, empty otherwise
	std::string description;     // localised description shown in the picker
};

// Every hotkey OBS currently knows about, sorted for the picker.
std::vector<Entry> list();

// Triggers one hotkey by identity (re-resolved against the live list). False when no hotkey matches.
bool trigger(const Entry &entry, bool pressed);

// Migration (spec 3.1 step 4): copy one bound clock hotkey from a Palette Lock filter's
// hotkey_bindings ("pd_*") onto the matching md_*. False when the pd_* name is not a clock hotkey.
bool load_pd_binding(const char *pd_name, obs_data_array_t *arr);

// Persistence: the md_* bindings are saved in the scene collection under music_director.hotkeys.
void save_bindings(obs_data_t *obj);
void load_bindings(obs_data_t *obj);

} // namespace mdhotkey

#endif
