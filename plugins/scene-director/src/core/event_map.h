#pragma once
// Scene Director core: the user-editable event map (event-map.json, schema v1). Pure C++17, no
// libobs/Qt, no sockets. Parses, validates and serialises the map that decides which OSC actions
// fire on which musical events (spec 4.3).
//
// Design notes:
//   - Parsing never throws and never crashes on any input. A syntax error returns a result that
//     leaves the previous map untouched (the caller keeps the last good map). A bad entry is
//     rejected alone and the rest load.
//   - Unknown keys are kept and written back verbatim: the serialiser re-emits the original scalar
//     text of anything it did not touch, so only the toggles below change values on disk.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cd {

// One typed OSC argument (int32 / float32 / string). A string arg may be exactly one placeholder
// (e.g. "{section}" or "{bpm}"); the engine expands those, everything else is a string literal.
struct EventArg {
	enum class Type { Int, Float, String };

	Type type = Type::Float;
	int32_t i = 0;
	float f = 0.0f;
	std::string s;
};

// One action: a route plus typed args. When `use_id` is non-empty the action is {"use": <id>}
// (reuse another mapping's actions); those are resolved to real actions at load time.
struct EventAction {
	std::string route;
	std::vector<EventArg> args;
	std::string use_id; // non-empty => {"use": id}
};

// Where a planned send is aimed (spec 4.3).
enum class Quantize { SectionBeat, Now, Beat, Bar, Phrase };

const char *quantize_name(Quantize q);
bool quantize_from_string(const std::string &s, Quantize &out);

// One validated mapping entry.
struct Mapping {
	std::string id;
	std::string event; // one of the 4.2 event names
	bool enabled = true;
	Quantize quantize = Quantize::Now;
	int offset_ms = 0;                // -500..500, added to the target time (negative = earlier)
	bool lead = false;                // subtract the source's Scene lead (syn_lead_ms)
	int min_interval_beats = 0;       // 0..4096, cooldown between fires (automatic section edges)
	bool forced_bypass = true;        // forced sections ignore the cooldown
	int debounce_ms = 0;              // 0..5000, wall-clock minimum between fires
	std::vector<EventAction> actions; // resolved (use expanded), 1..16
	size_t index = 0;                 // its position in the mappings array
};

// A single rejected entry: the reason the mapping at `index` was skipped.
struct ParseReject {
	size_t index = 0;
	std::string id;
	std::string reason;
};

// The one-line load summary ("N mappings, M enabled, K rejected").
struct LoadSummary {
	size_t total = 0;
	size_t enabled = 0;
	size_t rejected = 0;
	std::string text() const;
};

class EventMap {
public:
	EventMap() = default;

	// The shipped defaults (the JSON in spec 4.3), reproducing 2.0: Next on breakdown and drop at
	// section_beat minus Scene lead, 16-beat cooldown on automatic edges with forced bypass, manual
	// Next/Prev immediate. Only these four entries are enabled; the groove / drop-scene / phrase
	// examples ship disabled.
	static EventMap defaults();

	// The defaults with 2.0's keys applied (first-run seeding): trigger_breakdown / trigger_drop ->
	// breakdown-next / drop-next `enabled`, cooldown_beats -> their `min_interval_beats`.
	static EventMap defaults_seeded(bool trigger_breakdown, bool trigger_drop, int cooldown_beats);

	struct ParseResult {
		bool syntax_ok = false;
		std::string syntax_error; // "<reason> at line L col C" on a syntax error
		std::vector<ParseReject> rejects;
	};

	// Parses and validates `text`. Never throws. On a syntax error returns a result with
	// syntax_ok == false and leaves *this untouched. On success *this is replaced; each bad entry
	// is listed in rejects and skipped, the rest load.
	ParseResult parse(const std::string &text);

	// Serialises the map back to JSON (2-space indent). Unknown keys are kept verbatim; only the
	// values changed by the toggles below are rewritten.
	std::string to_json() const;

	// Dock toggles: rewrite only `enabled` / `min_interval_beats` of one mapping, preserving
	// everything else (including unknown keys) verbatim. False if the id is not present.
	bool set_enabled(const std::string &id, bool enabled);
	bool set_min_interval_beats(const std::string &id, int beats);

	const std::vector<Mapping> &mappings() const { return mappings_; }
	const Mapping *find(const std::string &id) const;
	const LoadSummary &summary() const { return summary_; }

private:
	struct Dom; // the JSON tree kept for verbatim round-trip (event_map.cpp)
	std::shared_ptr<Dom> dom_;

	std::vector<Mapping> mappings_;
	LoadSummary summary_;
};

} // namespace cd
