#pragma once
// Scene Director core: the source settings and their defaults. Pure C++17, no libobs/Qt.
//
// The source keeps its settings in the OBS data object of the source, which Scene Director reads
// as JSON (obs_data_get_json). Settings is a flat key -> JSON value store for that object:
//
//   - the new keys get defaults here, so a source that never saw them still works;
//   - every other key is kept verbatim, whatever it is. A scene collection written by Clip
//     Director 1.1.0 is full of clip and OSC keys (folders, clip_overrides, normalize, syn_*);
//     they are ignored on read and never dropped, so nothing the user set is lost.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "midi_msg.h"

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
// Director keys are not touched.
void apply_settings_defaults(Settings &s);

// The source config, read out of a Settings store.
struct SourceConfig {
	std::string clock; // "" auto, "\x01free", or "source|filter"
	int sync_offset_ms = 0;

	std::string midi_out_device;
	midi::MsgSpec next = midi::next_msg_default();
	midi::MsgSpec prev = midi::prev_msg_default();

	bool trigger_breakdown = true;
	bool trigger_drop = true;
	int cooldown_beats = 16;
	double lead_ms = 0.0; // Scene lead, 0..500
};

SourceConfig read_config(const Settings &s);

} // namespace cd
