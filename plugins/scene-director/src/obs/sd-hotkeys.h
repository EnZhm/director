#pragma once
/* Scene Director hotkeys: Next / Prev as OBS frontend hotkeys. A no-op when built without the
 * frontend API. The MIDI Director dock (now part of Music Director) drives these like any other
 * hotkey through its own mapping. */
#ifdef __cplusplus
extern "C" {
#endif

void scene_director_hotkey_load(void);
void scene_director_hotkey_unload(void);

#ifdef __cplusplus
}

namespace sdhotkey {

/* Stable hotkey names: mappings are saved with them, so they never change. */
constexpr const char *kNext = "scene_director_next";
constexpr const char *kPrev = "scene_director_prev";

} // namespace sdhotkey

#endif
