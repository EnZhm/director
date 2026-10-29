#pragma once
/* MIDI Director dock (Qt): maps MIDI controls to OBS hotkeys.
 * A no-op when built without the frontend API / Qt. */
#ifdef __cplusplus
extern "C" {
#endif

void scene_director_midi_dock_load(void);
void scene_director_midi_dock_unload(void);

#ifdef __cplusplus
}
#endif
