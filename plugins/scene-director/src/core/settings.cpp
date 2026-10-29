#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace cd {

namespace {

void skip_ws(const std::string &s, size_t &i)
{
	while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
		++i;
}

// Reads one JSON string; `i` points at the opening quote on entry, just past the closing quote
// on success.
bool read_string(const std::string &s, size_t &i, std::string &out)
{
	if (i >= s.size() || s[i] != '"')
		return false;
	++i;
	out.clear();
	while (i < s.size()) {
		const char c = s[i++];
		if (c == '"')
			return true;
		if (c != '\\') {
			out += c;
			continue;
		}
		if (i >= s.size())
			return false;
		const char e = s[i++];
		switch (e) {
		case '"':
			out += '"';
			break;
		case '\\':
			out += '\\';
			break;
		case '/':
			out += '/';
			break;
		case 'b':
			out += '\b';
			break;
		case 'f':
			out += '\f';
			break;
		case 'n':
			out += '\n';
			break;
		case 'r':
			out += '\r';
			break;
		case 't':
			out += '\t';
			break;
		case 'u': {
			if (i + 4 > s.size())
				return false;
			unsigned cp = 0;
			for (int k = 0; k < 4; ++k) {
				const char h = s[i++];
				cp <<= 4;
				if (h >= '0' && h <= '9')
					cp |= (unsigned)(h - '0');
				else if (h >= 'a' && h <= 'f')
					cp |= (unsigned)(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F')
					cp |= (unsigned)(h - 'A' + 10);
				else
					return false;
			}
			if (cp < 0x80) {
				out += (char)cp;
			} else if (cp < 0x800) {
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			} else {
				out += (char)(0xE0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			break;
		}
		default:
			return false;
		}
	}
	return false;
}

// Captures the raw text of one JSON value starting at `i` (objects and arrays are copied whole,
// so an unknown key keeps whatever shape it had).
bool read_value(const std::string &s, size_t &i, std::string &out)
{
	skip_ws(s, i);
	if (i >= s.size())
		return false;
	const size_t start = i;
	const char c = s[i];
	if (c == '"') {
		std::string ignored;
		if (!read_string(s, i, ignored))
			return false;
		out = s.substr(start, i - start);
		return true;
	}
	if (c == '{' || c == '[') {
		const char close = c == '{' ? '}' : ']';
		int depth = 0;
		while (i < s.size()) {
			const char d = s[i];
			if (d == '"') {
				std::string ignored;
				if (!read_string(s, i, ignored))
					return false;
				continue;
			}
			if (d == c) {
				++depth;
			} else if (d == close) {
				if (--depth == 0) {
					++i;
					out = s.substr(start, i - start);
					return true;
				}
			}
			++i;
		}
		return false;
	}
	while (i < s.size() && s[i] != ',' && s[i] != '}')
		++i;
	size_t end = i;
	while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r'))
		--end;
	out = s.substr(start, end - start);
	return !out.empty();
}

void escape(const std::string &in, std::string &out)
{
	for (const char c : in) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			out += c;
		}
	}
}

std::string quoted(const std::string &s)
{
	std::string out = "\"";
	escape(s, out);
	out += "\"";
	return out;
}

std::string number_text(double value)
{
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%.6g", value);
	return buf;
}

} // namespace

bool Settings::has(const std::string &key) const
{
	return raw_.find(key) != raw_.end();
}

void Settings::set_default_bool(const std::string &key, bool value)
{
	defaults_[key] = value ? "true" : "false";
}

void Settings::set_default_int(const std::string &key, int value)
{
	defaults_[key] = number_text(value);
}

void Settings::set_default_double(const std::string &key, double value)
{
	defaults_[key] = number_text(value);
}

void Settings::set_default_string(const std::string &key, const std::string &value)
{
	defaults_[key] = quoted(value);
}

void Settings::set_bool(const std::string &key, bool value)
{
	raw_[key] = value ? "true" : "false";
}

void Settings::set_int(const std::string &key, int value)
{
	raw_[key] = std::to_string(value);
}

void Settings::set_double(const std::string &key, double value)
{
	raw_[key] = number_text(value);
}

void Settings::set_string(const std::string &key, const std::string &value)
{
	raw_[key] = quoted(value);
}

std::string Settings::raw_of(const std::string &key) const
{
	auto it = raw_.find(key);
	if (it != raw_.end())
		return it->second;
	auto d = defaults_.find(key);
	return d == defaults_.end() ? std::string() : d->second;
}

bool Settings::get_bool(const std::string &key) const
{
	return raw_of(key) == "true";
}

int Settings::get_int(const std::string &key) const
{
	const std::string v = raw_of(key);
	return v.empty() ? 0 : (int)std::strtol(v.c_str(), nullptr, 10);
}

double Settings::get_double(const std::string &key) const
{
	const std::string v = raw_of(key);
	return v.empty() ? 0.0 : std::strtod(v.c_str(), nullptr);
}

std::string Settings::get_string(const std::string &key) const
{
	const std::string v = raw_of(key);
	if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
		std::string out;
		size_t i = 0;
		read_string(v, i, out);
		return out;
	}
	return v;
}

bool Settings::from_json(const std::string &text, std::string *error)
{
	std::map<std::string, std::string> parsed;
	size_t i = 0;
	skip_ws(text, i);
	if (i >= text.size()) { /* an empty object */
		if (error)
			error->clear();
		raw_ = parsed;
		return true;
	}
	auto fail = [&](const char *what) {
		if (error)
			*error = std::string(what) + " at byte " + std::to_string(i);
		return false;
	};
	if (text[i] != '{')
		return fail("expected {");
	++i;
	skip_ws(text, i);
	if (i < text.size() && text[i] == '}') {
		++i;
	} else {
		for (;;) {
			std::string key;
			skip_ws(text, i);
			if (!read_string(text, i, key))
				return fail("expected a key");
			skip_ws(text, i);
			if (i >= text.size() || text[i] != ':')
				return fail("expected :");
			++i;
			std::string value;
			if (!read_value(text, i, value))
				return fail("expected a value");
			parsed[key] = value;
			skip_ws(text, i);
			if (i < text.size() && text[i] == ',') {
				++i;
				continue;
			}
			if (i < text.size() && text[i] == '}') {
				++i;
				break;
			}
			return fail("expected , or }");
		}
	}
	skip_ws(text, i);
	if (i != text.size())
		return fail("trailing data");
	if (error)
		error->clear();
	raw_ = parsed; /* the defaults are kept */
	return true;
}

std::string Settings::to_json() const
{
	std::set<std::string> all;
	for (const auto &kv : defaults_)
		all.insert(kv.first);
	for (const auto &kv : raw_)
		all.insert(kv.first);

	std::string out = "{";
	bool first = true;
	for (const std::string &key : all) {
		auto it = raw_.find(key);
		if (it == raw_.end())
			continue; /* a default with no value is not written out */
		out += first ? "\n  " : ",\n  ";
		first = false;
		out += quoted(key);
		out += ":";
		out += it->second;
	}
	out += first ? "}" : "\n}";
	return out;
}

std::vector<std::string> Settings::keys() const
{
	std::vector<std::string> out;
	for (const auto &kv : raw_)
		out.push_back(kv.first);
	return out;
}

void apply_settings_defaults(Settings &s)
{
	s.set_default_string("clock_source", "");
	s.set_default_int("sync_offset_ms", 0);
	s.set_default_double("fallback_bpm", 128.0);

	s.set_default_string("midi_out_device", "");
	s.set_default_string("next_type", "note");
	s.set_default_int("next_channel", 16);
	s.set_default_int("next_number", 60);
	s.set_default_string("prev_type", "note");
	s.set_default_int("prev_channel", 16);
	s.set_default_int("prev_number", 59);

	s.set_default_bool("trigger_breakdown", true);
	s.set_default_bool("trigger_drop", true);
	s.set_default_bool("trigger_groove_phrase", false);
	s.set_default_int("cooldown_beats", 16);
	/* Scene lead keeps the key name Clip Director 1.1.0 used; lead_ms is read as a fallback */
	s.set_default_double("syn_lead_ms", 0.0);
}

namespace {

bool type_of(const Settings &s, const std::string &key, midi::Type &out)
{
	midi::Type parsed = midi::Type::Note;
	if (midi::type_from_string(s.get_string(key), parsed)) {
		out = parsed;
		return true;
	}
	return false;
}

} // namespace

SourceConfig read_config(const Settings &s)
{
	SourceConfig c;
	c.clock = s.get_string("clock_source");
	c.sync_offset_ms = (int)std::clamp((long long)s.get_int("sync_offset_ms"), -200ll, 200ll);
	c.fallback_bpm = std::clamp(s.get_double("fallback_bpm"), 60.0, 200.0);

	c.midi_out_device = s.get_string("midi_out_device");

	type_of(s, "next_type", c.next.type);
	c.next.channel = (int)midi::clamp_channel(s.get_int("next_channel"));
	c.next.number = (int)midi::clamp_number(s.get_int("next_number"));
	type_of(s, "prev_type", c.prev.type);
	c.prev.channel = (int)midi::clamp_channel(s.get_int("prev_channel"));
	c.prev.number = (int)midi::clamp_number(s.get_int("prev_number"));

	c.trigger_breakdown = s.get_bool("trigger_breakdown");
	c.trigger_drop = s.get_bool("trigger_drop");
	c.trigger_groove_phrase = s.get_bool("trigger_groove_phrase");
	c.cooldown_beats = (int)std::clamp((long long)s.get_int("cooldown_beats"), 0ll, 4096ll);

	const double lead = s.has("syn_lead_ms") ? s.get_double("syn_lead_ms") : s.get_double("lead_ms");
	c.lead_ms = std::clamp(lead, 0.0, 500.0);
	return c;
}

} // namespace cd
