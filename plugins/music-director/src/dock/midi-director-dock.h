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

#pragma once
/* MIDI Director dock (Qt): maps MIDI controls to OBS hotkeys (music, palette, clips, Synesthesia).
 * Moved from Scene Director 2.0.0. A no-op when built without the frontend API / Qt. */
#ifdef __cplusplus
extern "C" {
#endif

void music_director_midi_dock_load(void);
void music_director_midi_dock_unload(void);

#ifdef __cplusplus
}
#endif
