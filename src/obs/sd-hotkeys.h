#pragma once
/* Scene Director hotkeys: Next / Prev as OBS frontend hotkeys, plus the listing and triggering
 * helpers the MIDI Director dock uses to drive any registered hotkey by identity. A no-op when
 * built without the frontend API. */
#ifdef __cplusplus
extern "C" {
#endif

void scene_director_hotkey_load(void);
void scene_director_hotkey_unload(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include <string>
#include <vector>

namespace sdhotkey {

// Stable hotkey names: MIDI mappings are saved with them, so they never change.
constexpr const char *kNext = "scene_director_next";
constexpr const char *kPrev = "scene_director_prev";

// A hotkey identity as the MIDI Director dock stores it (mirrors cd::midi::Target).
struct Entry {
	std::string name;            // hotkey name, e.g. "scene_director_next"
	std::string registerer;      // "frontend", "source", "output", "encoder", "service"
	std::string registerer_name; // source name for source-level hotkeys, empty otherwise
	std::string description;     // localised description shown in the picker
};

// Every hotkey OBS currently knows about, sorted for the picker.
std::vector<Entry> list();

// Triggers one hotkey by identity (re-resolved against the live list, so a re-registered hotkey
// still matches). False when no hotkey matches any more.
bool trigger(const Entry &entry, bool pressed);

} // namespace sdhotkey

#endif
