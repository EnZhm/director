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

#include "midi_legacy.h"

#include "hotkey_names.h"

namespace md {

namespace {

// A small bounded JSON reader: just enough to walk the v1 document and skip anything else.
class Reader {
public:
	explicit Reader(const std::string &s) : s_(s) {}

	bool ok() const { return ok_; }
	bool at_end()
	{
		ws();
		return pos_ >= s_.size();
	}

	bool peek(char c)
	{
		ws();
		return pos_ < s_.size() && s_[pos_] == c;
	}

	bool peek_number()
	{
		ws();
		return pos_ < s_.size() && (s_[pos_] == '-' || (s_[pos_] >= '0' && s_[pos_] <= '9'));
	}

	bool eat(char c)
	{
		if (!peek(c))
			return false;
		++pos_;
		return true;
	}

	bool string(std::string &out)
	{
		out.clear();
		if (!eat('"'))
			return fail();
		while (pos_ < s_.size()) {
			const char c = s_[pos_++];
			if (c == '"')
				return true;
			if (c != '\\') {
				out += c;
				continue;
			}
			if (pos_ >= s_.size())
				return fail();
			const char e = s_[pos_++];
			switch (e) {
			case 'n':
				out += '\n';
				break;
			case 't':
				out += '\t';
				break;
			case 'r':
				out += '\r';
				break;
			case 'b':
				out += '\b';
				break;
			case 'f':
				out += '\f';
				break;
			case 'u': {
				if (pos_ + 4 > s_.size())
					return fail();
				unsigned v = 0;
				for (int i = 0; i < 4; ++i) {
					const char h = s_[pos_++];
					v <<= 4;
					if (h >= '0' && h <= '9')
						v |= static_cast<unsigned>(h - '0');
					else if (h >= 'a' && h <= 'f')
						v |= static_cast<unsigned>(h - 'a' + 10);
					else if (h >= 'A' && h <= 'F')
						v |= static_cast<unsigned>(h - 'A' + 10);
					else
						return fail();
				}
				out += v < 0x80 ? static_cast<char>(v) : '?'; // labels only; no UTF-16 decoding
				break;
			}
			default:
				out += e; // \" \\ \/
				break;
			}
		}
		return fail();
	}

	bool number(long long &out)
	{
		ws();
		const size_t start = pos_;
		bool neg = false;
		if (pos_ < s_.size() && s_[pos_] == '-') {
			neg = true;
			++pos_;
		}
		long long v = 0;
		bool digits = false;
		while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') {
			if (v < 100000000000LL)
				v = v * 10 + (s_[pos_] - '0');
			++pos_;
			digits = true;
		}
		// tolerate a fraction / exponent (skipped)
		while (pos_ < s_.size() && (s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' ||
					    s_[pos_] == '-' || (s_[pos_] >= '0' && s_[pos_] <= '9')))
			++pos_;
		if (!digits) {
			pos_ = start;
			return fail();
		}
		out = neg ? -v : v;
		return true;
	}

	bool skip_value(int depth = 0)
	{
		if (depth > 64)
			return fail();
		ws();
		if (pos_ >= s_.size())
			return fail();
		const char c = s_[pos_];
		if (c == '"') {
			std::string tmp;
			return string(tmp);
		}
		if (c == '{') {
			++pos_;
			if (eat('}'))
				return true;
			do {
				std::string key;
				if (!string(key) || !eat(':') || !skip_value(depth + 1))
					return fail();
			} while (eat(','));
			return eat('}') || fail();
		}
		if (c == '[') {
			++pos_;
			if (eat(']'))
				return true;
			do {
				if (!skip_value(depth + 1))
					return fail();
			} while (eat(','));
			return eat(']') || fail();
		}
		if (c == '-' || (c >= '0' && c <= '9')) {
			long long n = 0;
			return number(n);
		}
		for (const char *lit : {"true", "false", "null"}) {
			const std::string l(lit);
			if (s_.compare(pos_, l.size(), l) == 0) {
				pos_ += l.size();
				return true;
			}
		}
		return fail();
	}

private:
	void ws()
	{
		while (pos_ < s_.size() &&
		       (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t'))
			++pos_;
	}
	bool fail()
	{
		ok_ = false;
		return false;
	}

	const std::string &s_;
	size_t pos_ = 0;
	bool ok_ = true;
};

// One mappings[] row. False on malformed JSON; a well-formed row without a name sets keep=false.
bool read_row(Reader &r, LegacyMapping &out, bool &keep)
{
	keep = false;
	if (!r.eat('{'))
		return false;
	std::string type, name, description;
	long long channel = -1, number = -1;
	if (!r.eat('}')) {
		do {
			std::string key;
			if (!r.string(key) || !r.eat(':'))
				return false;
			bool ok = true;
			if (key == "type" && r.peek('"'))
				ok = r.string(type);
			else if (key == "name" && r.peek('"'))
				ok = r.string(name);
			else if (key == "description" && r.peek('"'))
				ok = r.string(description);
			else if (key == "channel" && r.peek_number())
				ok = r.number(channel);
			else if (key == "number" && r.peek_number())
				ok = r.number(number);
			else
				ok = r.skip_value();
			if (!ok)
				return false;
		} while (r.eat(','));
		if (!r.eat('}'))
			return false;
	}
	if (name.empty())
		return true;
	std::string label = (type == "cc" || type == "control") ? "CC " : (type == "note" ? "Note " : "Control ");
	label += number >= 0 ? std::to_string(number) : std::string("?");
	label += " ch ";
	label += channel >= 0 ? std::to_string(channel) : std::string("?");
	out.control = label;
	out.target = name;
	out.description = description;
	keep = true;
	return true;
}

LegacyMidi parse(const std::string &json)
{
	LegacyMidi result;
	Reader r(json);
	std::vector<LegacyMapping> rows;
	if (!r.eat('{'))
		return result;
	if (!r.eat('}')) {
		do {
			std::string key;
			if (!r.string(key) || !r.eat(':'))
				return result;
			if (key == "mappings" && r.peek('[')) {
				r.eat('[');
				if (!r.eat(']')) {
					do {
						LegacyMapping m;
						bool keep = false;
						if (!read_row(r, m, keep))
							return result;
						if (keep)
							rows.push_back(m);
					} while (r.eat(','));
					if (!r.eat(']'))
						return result;
				}
			} else if (!r.skip_value()) {
				return result;
			}
		} while (r.eat(','));
		if (!r.eat('}'))
			return result;
	}
	if (!r.at_end())
		return result;
	result.parsed = true;
	result.mappings = rows;
	return result;
}

bool is_synesthesia_target(const std::string &t)
{
	return t == "scene_director_next" || t == "scene_director_prev";
}

} // namespace

LegacyMidi parse_legacy_midi(const std::string &json) noexcept
{
	try {
		return parse(json);
	} catch (...) {
		return LegacyMidi{};
	}
}

std::string legacy_target_label(const LegacyMapping &m)
{
	if (const mdhotkey::Name *n = mdhotkey::find(m.target.c_str()))
		return n->label;
	if (!m.description.empty())
		return m.description;
	return m.target;
}

std::string rebind_hint(const LegacyMapping &m)
{
	if (m.target == mdhotkey::kDropNow)
		return "bind Drop now in Settings > Hotkeys or on a Stream Deck";
	if (is_synesthesia_target(m.target))
		return "map the control in Synesthesia's MIDI panel";
	return "bind " + legacy_target_label(m) + " in Settings > Hotkeys";
}

std::string rebind_line(const LegacyMapping &m)
{
	return "MIDI Director mapping '" + m.control + "' -> " + m.target + " (" + legacy_target_label(m) +
	       ") is inactive: " + rebind_hint(m);
}

std::string removal_line(size_t count, const std::string &copy_path)
{
	return "MIDI Director was removed in 2.1: " + std::to_string(count) +
	       " mappings kept in the scene collection (inactive), copy at " + copy_path;
}

std::string legacy_copy_name(const std::string &collection)
{
	std::string safe;
	for (const char c : collection) {
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == ' ' || c == '.' || c == '_' || c == '-';
		safe += ok ? c : '_';
	}
	if (safe.empty() || safe.find_first_not_of('.') == std::string::npos)
		safe = "_";
	return "midi-director-" + safe + ".json";
}

void LegacyMidiStore::load(const char *raw)
{
	raw_ = raw ? raw : "";
}

bool LegacyMidiStore::first_report(const std::string &collection)
{
	return reported_.insert(collection).second;
}

} // namespace md
