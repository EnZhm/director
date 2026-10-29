#include "migration.h"

#include <cmath>

namespace md {

namespace {

bool same_audio(const AudioSettings &a, const AudioSettings &b)
{
	return a.audio_source == b.audio_source && std::fabs(a.input_gain_db - b.input_gain_db) < 1e-4f &&
	       std::fabs(a.kick_sensitivity - b.kick_sensitivity) < 1e-4f &&
	       std::fabs(a.fallback_bpm - b.fallback_bpm) < 1e-4f && a.phrase_beats == b.phrase_beats;
}

} // namespace

Migration pick_migration(const std::vector<PaletteLock> &filters)
{
	Migration m;

	int pick = -1;
	for (std::size_t i = 0; i < filters.size(); ++i) {
		if (filters[i].enabled && filters[i].has_audio_source) {
			pick = static_cast<int>(i);
			break;
		}
	}
	if (pick < 0 && !filters.empty())
		pick = 0; // no enabled filter with a user audio_source: the first Palette Lock
	if (pick < 0)
		return m; // no Palette Lock at all: MD defaults

	m.found = true;
	m.index = pick;
	m.source = filters[pick].source;
	m.filter = filters[pick].filter;
	m.values = filters[pick].values;

	// Step 6: warn about the other Palette Locks that hold different audio values.
	for (std::size_t i = 0; i < filters.size(); ++i) {
		if (static_cast<int>(i) == pick)
			continue;
		if (!same_audio(filters[i].values, m.values))
			m.differing.push_back(static_cast<int>(i));
	}
	return m;
}

} // namespace md
