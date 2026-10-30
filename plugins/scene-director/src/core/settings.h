#pragma once
// Scene Director core: the source settings and their defaults. Pure C++17, no libobs/Qt.
//
// The source keeps its settings in the OBS data object of the source, which Scene Director reads
// as JSON (obs_data_get_json). Settings is a flat key -> JSON value store for that object:
//
//   - the new keys get defaults here, so a source that never saw them still works;
//   - every other key is kept verbatim, whatever it is. A scene collection written by Clip
//     Director 1.1.0 is full of clip and OSC keys (folders, clip_overrides, normalize, syn_*);
//     they are ignored on read and never dropped. The 2.0 MIDI keys (midi_out_device, next_*,
//     prev_*) are likewise kept verbatim and never read (spec 4.1).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cd {

class Settings {
public:
	bool has(const std::string &key) const;

	// Defaults for a key; the value is used when the object does not carry that key.
	void set_default_bool(const std::string &key, bool value);
	void set_default_int(const std::string &key, int value);
	void set_default_double(const std::string &key, double value);
	void set_default_string(const std::string &key, const std::string &value);

	bool get_bool(const std::string &key) const;
	int get_int(const std::string &key) const;
	double get_double(const std::string &key) const;
	std::string get_string(const std::string &key) const;

	void set_bool(const std::string &key, bool value);
	void set_int(const std::string &key, int value);
	void set_double(const std::string &key, double value);
	void set_string(const std::string &key, const std::string &value);

	// Parses a flat JSON object; every value is kept as its raw JSON text. An empty string is
	// an empty object. Returns false (leaving the store untouched) on a syntax error.
	bool from_json(const std::string &text, std::string *error = nullptr);
	// Writes the store back as a JSON object: every key read or set, plus the defaults that were
	// never present, sorted by key.
	std::string to_json() const;

	std::vector<std::string> keys() const;

private:
	std::string raw_of(const std::string &key) const;

	std::map<std::string, std::string> raw_;      // key -> raw JSON value text (as read)
	std::map<std::string, std::string> defaults_; // key -> raw JSON default
};

// The Scene Director source's keys and defaults. New keys are added here; the removed Clip
// Director keys and the 2.0 MIDI keys are not touched.
void apply_settings_defaults(Settings &s);

// The source config, read out of a Settings store. The migration facts (`osc_seeded`,
// `midi_out_present`) drive the source's one-time migration log lines (spec 4.1); the 2.0
// trigger keys are carried verbatim for first-run event-map seeding (spec 4.3).
struct SourceConfig {
	std::string osc_host = "127.0.0.1"; // Synesthesia machine
	int osc_send_port = 7000;           // Synesthesia Settings > OSC > Input port
	int osc_feedback_port = 7001;       // Synesthesia Settings > OSC > Output port; 0 = no feedback
	int confirm_timeout_ms = 2000;      // 100..10000

	int sync_offset_ms = 0; // 2.0 key, unchanged
	double lead_ms = 0.0;   // Scene lead (syn_lead_ms), 0..500, unchanged

	// 2.0 keys kept verbatim; they seed the event map only on first run (spec 4.1, 4.3).
	bool trigger_breakdown = true;
	bool trigger_drop = true;
	int cooldown_beats = 16;

	// Migration facts for the source's one-time log lines.
	bool osc_seeded = false;       // osc_* were absent and seeded from CD 1.1.0 syn_*
	bool midi_out_present = false; // midi_out_device was non-empty ("MIDI out removed in 2.1")
};

SourceConfig read_config(const Settings &s);

} // namespace cd
