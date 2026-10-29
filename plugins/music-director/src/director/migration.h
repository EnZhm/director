#pragma once

#include <string>
#include <vector>

namespace md {

// The audio settings a Palette Lock filter carries, and the values Music Director inherits from it.
// The defaults are Palette Director 1.2.0's get_defaults values (pl_set_default + the filter's
// audio_source default "").
struct AudioSettings {
	std::string audio_source;
	float input_gain_db = 0.0f;
	float kick_sensitivity = 0.6f;
	float fallback_bpm = 128.0f;
	int phrase_beats = 32;
};

// One Palette Lock filter seen in the scene collection (spec 3.1 step 1). `values` holds the
// filter's effective audio values: the user value where the filter has one, else the PD 1.2.0
// default. `has_audio_source` is the "carries a user value for audio_source" test of step 2.
struct PaletteLock {
	std::string source; // parent source name
	std::string filter; // filter name
	bool enabled = false;
	bool has_audio_source = false;
	AudioSettings values;
};

// The migration decision (spec 3.1). Pure data: the OBS layer reads the filters into PaletteLock
// records, calls this, then applies `values` and copies the hotkeys.
struct Migration {
	bool found = false; // false: no Palette Lock filter -> Music Director defaults
	int index = -1;     // index into the input list, -1 if none
	std::string source, filter;
	AudioSettings values;
	std::vector<int> differing; // other filters whose audio values differ (warning, step 6)
};

// Pick the source Palette Lock: the first ENABLED filter that carries a user audio_source; if there
// is none, the first Palette Lock. `differing` lists the other filters whose audio values differ.
Migration pick_migration(const std::vector<PaletteLock> &filters);

} // namespace md
