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
 * md_nudge_back, md_nudge_fwd as OBS frontend hotkeys (Settings > Hotkeys, or Stream Deck's OBS
 * hotkey action). The name table is director/hotkey_names.h; registering needs the frontend API. */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void music_director_hotkey_load(void);
void music_director_hotkey_unload(void);

#ifdef __cplusplus
}

#include <obs.h>

#include "director/hotkey_names.h"

namespace mdhotkey {

// Migration (spec 3.1 step 4): copy one bound clock hotkey from a Palette Lock filter's
// hotkey_bindings ("pd_*") onto the matching md_*. False when the pd_* name is not a clock hotkey.
bool load_pd_binding(const char *pd_name, obs_data_array_t *arr);

// Persistence: the md_* bindings are saved in the scene collection under music_director.hotkeys.
void save_bindings(obs_data_t *obj);
void load_bindings(obs_data_t *obj);

} // namespace mdhotkey

#endif
