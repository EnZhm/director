#include "event_map.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace cd {

// ---------------------------------------------------------------------------
// JSON DOM: a minimal, order-preserving tree that keeps the raw scalar text, so
// serialisation re-emits exactly what was read (except the fields the dock
// toggles). Parsing is recursive with a depth cap and never throws; every error
// carries a line and column.
// ---------------------------------------------------------------------------
namespace {

enum class JType { Null, Bool, Number, String, Array, Object };

struct JNode {
	JType type = JType::Null;
	bool b = false;
	double num = 0.0;
	std::string raw; // scalar text exactly as written (strings include the quotes)
	std::string str; // decoded string
	int line = 1;
	int col = 1;
	std::vector<JNode> arr;
	std::vector<std::pair<std::string, JNode>> obj;

	const JNode *get(const char *k) const
	{
		if (type != JType::Object)
			return nullptr;
		for (const auto &kv : obj)
			if (kv.first == k)
				return &kv.second;
		return nullptr;
	}
	JNode *get(const char *k)
	{
		if (type != JType::Object)
			return nullptr;
		for (auto &kv : obj)
			if (kv.first == k)
				return &kv.second;
		return nullptr;
	}
};

class JParser {
public:
	explicit JParser(const char *text) : p_(text), start_(text) {}

	bool parse(JNode &out, std::string &err)
	{
		ws();
		if (!value(out, 0)) {
			err = err_;
			return false;
		}
		ws();
		if (*p_) {
			fail_at("trailing characters after the JSON value", p_);
			err = err_;
			return false;
		}
		return true;
	}

private:
	const char *p_;
	const char *start_;
	std::string err_;

	void pos_of(const char *at, int &l, int &c) const
	{
		l = 1;
		c = 1;
		for (const char *q = start_; q < at; ++q) {
			if (*q == '\n') {
				++l;
				c = 1;
			} else {
				++c;
			}
		}
	}
	bool fail_at(const char *m, const char *at)
	{
		int l, c;
		pos_of(at, l, c);
		char buf[192];
		std::snprintf(buf, sizeof(buf), "%s at line %d col %d", m, l, c);
		err_ = buf;
		return false;
	}
	bool fail(const char *m) { return fail_at(m, p_); }
	void ws()
	{
		while (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')
			++p_;
	}
	static void utf8(std::string &o, unsigned cp)
	{
		if (cp < 0x80) {
			o += static_cast<char>(cp);
		} else if (cp < 0x800) {
			o += static_cast<char>(0xC0 | (cp >> 6));
			o += static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			o += static_cast<char>(0xE0 | (cp >> 12));
			o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			o += static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			o += static_cast<char>(0xF0 | (cp >> 18));
			o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			o += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	bool hex4(unsigned &v)
	{
		v = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = *p_++;
			v <<= 4;
			if (c >= '0' && c <= '9')
				v |= static_cast<unsigned>(c - '0');
			else if (c >= 'a' && c <= 'f')
				v |= static_cast<unsigned>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				v |= static_cast<unsigned>(c - 'A' + 10);
			else
				return fail("bad \\u escape");
		}
		return true;
	}
	bool string(std::string &o)
	{
		const char *begin = p_;
		if (*p_ != '"')
			return fail("expected a string");
		++p_;
		while (*p_ && *p_ != '"') {
			if (static_cast<unsigned char>(*p_) < 0x20)
				return fail("control character in string");
			if (*p_ != '\\') {
				o += *p_++;
				continue;
			}
			++p_;
			switch (*p_++) {
			case '"':
				o += '"';
				break;
			case '\\':
				o += '\\';
				break;
			case '/':
				o += '/';
				break;
			case 'b':
				o += '\b';
				break;
			case 'f':
				o += '\f';
				break;
			case 'n':
				o += '\n';
				break;
			case 'r':
				o += '\r';
				break;
			case 't':
				o += '\t';
				break;
			case 'u': {
				unsigned cp;
				if (!hex4(cp))
					return false;
				if (cp >= 0xD800 && cp < 0xDC00 && p_[0] == '\\' && p_[1] == 'u') {
					p_ += 2;
					unsigned lo;
					if (!hex4(lo))
						return false;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				utf8(o, cp);
				break;
			}
			default:
				return fail("bad escape");
			}
		}
		if (*p_ != '"')
			return fail_at("unterminated string", begin);
		++p_;
		return true;
	}
	bool number(JNode &v)
	{
		const char *begin = p_;
		if (*p_ == '-')
			++p_;
		if (*p_ == '0') {
			++p_;
			if (*p_ >= '0' && *p_ <= '9')
				return fail("bad number");
		} else if (*p_ >= '1' && *p_ <= '9') {
			while (*p_ >= '0' && *p_ <= '9')
				++p_;
		} else {
			return fail("expected a number");
		}
		if (*p_ == '.') {
			++p_;
			if (*p_ < '0' || *p_ > '9')
				return fail("bad number");
			while (*p_ >= '0' && *p_ <= '9')
				++p_;
		}
		if (*p_ == 'e' || *p_ == 'E') {
			++p_;
			if (*p_ == '+' || *p_ == '-')
				++p_;
			if (*p_ < '0' || *p_ > '9')
				return fail("bad number");
			while (*p_ >= '0' && *p_ <= '9')
				++p_;
		}
		const std::string tok(begin, static_cast<size_t>(p_ - begin));
		char *end = nullptr;
		const double d = std::strtod(tok.c_str(), &end);
		if (!end || *end != '\0')
			return fail_at("bad number", begin);
		v.raw = tok;
		v.num = d;
		return true;
	}
	bool value(JNode &v, int depth)
	{
		if (depth > 64)
			return fail("nesting too deep");
		ws();
		pos_of(p_, v.line, v.col);
		if (*p_ == '{') {
			++p_;
			v.type = JType::Object;
			ws();
			if (*p_ == '}') {
				++p_;
				return true;
			}
			for (;;) {
				ws();
				std::string k;
				if (!string(k))
					return false;
				ws();
				if (*p_++ != ':')
					return fail("expected ':'");
				JNode item;
				if (!value(item, depth + 1))
					return false;
				v.obj.emplace_back(std::move(k), std::move(item));
				ws();
				if (*p_ == ',') {
					++p_;
					continue;
				}
				if (*p_ == '}') {
					++p_;
					return true;
				}
				return fail("expected ',' or '}'");
			}
		}
		if (*p_ == '[') {
			++p_;
			v.type = JType::Array;
			ws();
			if (*p_ == ']') {
				++p_;
				return true;
			}
			for (;;) {
				JNode item;
				if (!value(item, depth + 1))
					return false;
				v.arr.push_back(std::move(item));
				ws();
				if (*p_ == ',') {
					++p_;
					continue;
				}
				if (*p_ == ']') {
					++p_;
					return true;
				}
				return fail("expected ',' or ']'");
			}
		}
		if (*p_ == '"') {
			const char *begin = p_;
			v.type = JType::String;
			if (!string(v.str))
				return false;
			v.raw.assign(begin, static_cast<size_t>(p_ - begin));
			return true;
		}
		if (std::strncmp(p_, "true", 4) == 0) {
			p_ += 4;
			v.type = JType::Bool;
			v.b = true;
			v.raw = "true";
			return true;
		}
		if (std::strncmp(p_, "false", 5) == 0) {
			p_ += 5;
			v.type = JType::Bool;
			v.b = false;
			v.raw = "false";
			return true;
		}
		if (std::strncmp(p_, "null", 4) == 0) {
			p_ += 4;
			v.type = JType::Null;
			v.raw = "null";
			return true;
		}
		if (*p_ == '-' || (*p_ >= '0' && *p_ <= '9')) {
			v.type = JType::Number;
			return number(v);
		}
		return fail("unexpected character");
	}
};

// ---- writer: 2-space indent, "key": value, one item per line, raw scalars ----
void dump_string(std::string &o, const std::string &s)
{
	o += '"';
	for (const char c : s) {
		switch (c) {
		case '"':
			o += "\\\"";
			break;
		case '\\':
			o += "\\\\";
			break;
		case '\n':
			o += "\\n";
			break;
		case '\r':
			o += "\\r";
			break;
		case '\t':
			o += "\\t";
			break;
		case '\b':
			o += "\\b";
			break;
		case '\f':
			o += "\\f";
			break;
		default:
			if (static_cast<unsigned char>(c) < 0x20) {
				char buf[8];
				std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
				o += buf;
			} else {
				o += c;
			}
		}
	}
	o += '"';
}

void dump(std::string &o, const JNode &n, int depth)
{
	const std::string pad(static_cast<size_t>(depth) * 2, ' ');
	const std::string pad1(static_cast<size_t>(depth + 1) * 2, ' ');
	switch (n.type) {
	case JType::Null:
		o += "null";
		break;
	case JType::Bool:
	case JType::Number:
	case JType::String:
		o += n.raw;
		break;
	case JType::Array:
		if (n.arr.empty()) {
			o += "[]";
			break;
		}
		o += "[";
		for (size_t i = 0; i < n.arr.size(); ++i) {
			o += "\n" + pad1;
			dump(o, n.arr[i], depth + 1);
			o += i + 1 < n.arr.size() ? "," : "";
		}
		o += "\n" + pad + "]";
		break;
	case JType::Object:
		if (n.obj.empty()) {
			o += "{}";
			break;
		}
		o += "{";
		for (size_t i = 0; i < n.obj.size(); ++i) {
			o += "\n" + pad1;
			dump_string(o, n.obj[i].first);
			o += ": ";
			dump(o, n.obj[i].second, depth + 1);
			o += i + 1 < n.obj.size() ? "," : "";
		}
		o += "\n" + pad + "}";
		break;
	}
}

// ---- event / quantize vocabulary ----
bool valid_event(const std::string &s)
{
	static const char *kNames[] = {"breakdown", "drop",        "groove",      "section",     "phrase",
				       "bar",       "manual_next", "manual_prev", "scene_loaded"};
	for (const char *n : kNames)
		if (s == n)
			return true;
	return false;
}

bool is_section_event(const std::string &s)
{
	return s == "breakdown" || s == "drop" || s == "groove" || s == "section";
}

bool valid_route(const std::string &s)
{
	if (s.empty() || s[0] != '/')
		return false;
	for (const char c : s)
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '_'))
			return false;
	return true;
}

} // namespace

const char *quantize_name(Quantize q)
{
	switch (q) {
	case Quantize::SectionBeat:
		return "section_beat";
	case Quantize::Now:
		return "now";
	case Quantize::Beat:
		return "beat";
	case Quantize::Bar:
		return "bar";
	case Quantize::Phrase:
		return "phrase";
	}
	return "now";
}

bool quantize_from_string(const std::string &s, Quantize &out)
{
	if (s == "section_beat")
		out = Quantize::SectionBeat;
	else if (s == "now")
		out = Quantize::Now;
	else if (s == "beat")
		out = Quantize::Beat;
	else if (s == "bar")
		out = Quantize::Bar;
	else if (s == "phrase")
		out = Quantize::Phrase;
	else
		return false;
	return true;
}

std::string LoadSummary::text() const
{
	return std::to_string(total) + " mappings, " + std::to_string(enabled) + " enabled, " +
	       std::to_string(rejected) + " rejected";
}

// ---------------------------------------------------------------------------
// Dom: the JSON tree plus the parsed mappings, for verbatim round-trip.
// ---------------------------------------------------------------------------
struct EventMap::Dom {
	JNode root;
};

namespace {

// Reads a numeric field with a range. Returns false and fills `reason` when the
// field is present but not a number in [lo, hi].
bool read_int(const JNode *n, int def, int lo, int hi, const char *what, int &out, std::string &reason)
{
	if (!n) {
		out = def;
		return true;
	}
	if (n->type != JType::Number) {
		reason = std::string(what) + " must be a number";
		return false;
	}
	const double d = n->num;
	if (d < static_cast<double>(lo) || d > static_cast<double>(hi)) {
		reason = std::string(what) + " out of range (" + std::to_string(lo) + ".." + std::to_string(hi) + ")";
		return false;
	}
	out = static_cast<int>(std::lround(d));
	return true;
}

bool read_bool(const JNode *n, bool def, const char *what, bool &out, std::string &reason)
{
	if (!n) {
		out = def;
		return true;
	}
	if (n->type != JType::Bool) {
		reason = std::string(what) + " must be a bool";
		return false;
	}
	out = n->b;
	return true;
}

// Parses one action's args (0..8). Returns false with a reason on a bad arg.
bool read_args(const JNode *arr, EventAction &a, std::string &reason)
{
	if (!arr)
		return true; // no args
	if (arr->type != JType::Array) {
		reason = "args must be an array";
		return false;
	}
	if (arr->arr.size() > 8) {
		reason = "args has more than 8 elements";
		return false;
	}
	for (const JNode &e : arr->arr) {
		EventArg arg;
		if (e.type == JType::Number) {
			arg.type = EventArg::Type::Float;
			arg.f = static_cast<float>(e.num);
		} else if (e.type == JType::String) {
			arg.type = EventArg::Type::String;
			arg.s = e.str;
		} else if (e.type == JType::Object) {
			// {"int": N}
			const JNode *iv = e.get("int");
			if (!iv || iv->type != JType::Number || e.obj.size() != 1) {
				reason = "arg object must be {\"int\": N}";
				return false;
			}
			if (std::floor(iv->num) != iv->num) {
				reason = "{\"int\": N} requires an integer";
				return false;
			}
			arg.type = EventArg::Type::Int;
			arg.i = static_cast<int32_t>(iv->num);
		} else {
			reason = "arg must be a number, string, or {\"int\": N}";
			return false;
		}
		a.args.push_back(std::move(arg));
	}
	return true;
}

} // namespace

EventMap EventMap::defaults()
{
	static const char kDefaults[] = R"json({
  "version": 1,
  "mappings": [
    {
      "id": "breakdown-next",
      "event": "breakdown",
      "enabled": true,
      "quantize": "section_beat",
      "offset_ms": 0,
      "lead": true,
      "min_interval_beats": 16,
      "forced_bypass": true,
      "debounce_ms": 0,
      "actions": [
        { "route": "/playlist/next" }
      ]
    },
    {
      "id": "drop-next",
      "event": "drop",
      "enabled": true,
      "quantize": "section_beat",
      "offset_ms": 0,
      "lead": true,
      "min_interval_beats": 16,
      "forced_bypass": true,
      "debounce_ms": 0,
      "actions": [
        { "route": "/playlist/next" }
      ]
    },
    {
      "id": "manual-next",
      "event": "manual_next",
      "enabled": true,
      "quantize": "now",
      "debounce_ms": 150,
      "actions": [
        { "route": "/playlist/next" }
      ]
    },
    {
      "id": "manual-prev",
      "event": "manual_prev",
      "enabled": true,
      "quantize": "now",
      "debounce_ms": 150,
      "actions": [
        { "route": "/playlist/previous" }
      ]
    },
    {
      "id": "groove-example",
      "event": "groove",
      "enabled": false,
      "quantize": "section_beat",
      "actions": [
        { "route": "/playlist/next" }
      ]
    },
    {
      "id": "drop-scene-example",
      "event": "drop",
      "enabled": false,
      "quantize": "section_beat",
      "lead": true,
      "actions": [
        { "route": "/scenes/hexarray", "args": ["Peak"] }
      ]
    },
    {
      "id": "phrase-example",
      "event": "phrase",
      "enabled": false,
      "quantize": "phrase",
      "actions": [
        { "route": "/controls/global/bang/1", "args": [1.0] }
      ]
    }
  ]
}
)json";

	EventMap m;
	m.parse(kDefaults);
	return m;
}

EventMap EventMap::defaults_seeded(bool trigger_breakdown, bool trigger_drop, int cooldown_beats)
{
	EventMap m = defaults();
	m.set_enabled("breakdown-next", trigger_breakdown);
	m.set_enabled("drop-next", trigger_drop);
	m.set_min_interval_beats("breakdown-next", cooldown_beats);
	m.set_min_interval_beats("drop-next", cooldown_beats);
	return m;
}

EventMap::ParseResult EventMap::parse(const std::string &text)
{
	ParseResult result;
	result.syntax_ok = false;

	JParser parser(text.c_str());
	JNode root;
	std::string err;
	if (!parser.parse(root, err)) {
		result.syntax_error = err;
		return result;
	}
	if (root.type != JType::Object) {
		result.syntax_error = "event-map.json must be a JSON object at line " + std::to_string(root.line) +
				      " col " + std::to_string(root.col);
		return result;
	}
	const JNode *mappings = root.get("mappings");
	if (!mappings || mappings->type != JType::Array) {
		result.syntax_error = "event-map.json: \"mappings\" must be an array at line " +
				      std::to_string(mappings ? mappings->line : root.line) + " col " +
				      std::to_string(mappings ? mappings->col : root.col);
		return result;
	}

	std::vector<Mapping> parsed;
	parsed.reserve(mappings->arr.size());
	std::vector<ParseReject> rejects;
	std::set<std::string> seen_ids;

	size_t idx = 0;
	for (const JNode &mn : mappings->arr) {
		Mapping m;
		m.index = idx;
		std::string reason;
		bool bad = false;

		if (mn.type != JType::Object) {
			reason = "not an object";
			bad = true;
		}
		const JNode *idn = mn.get("id");
		if (!bad && (!idn || idn->type != JType::String || idn->str.empty())) {
			reason = "id must be a non-empty string";
			bad = true;
		}
		if (!bad) {
			m.id = idn->str;
			if (!seen_ids.insert(m.id).second) {
				reason = "duplicate id '" + m.id + "'";
				bad = true;
			}
		}
		const JNode *evn = mn.get("event");
		if (!bad && (!evn || evn->type != JType::String || !valid_event(evn->str))) {
			reason = "event must be one of breakdown/drop/groove/section/phrase/bar/palette/"
				 "manual_next/manual_prev/scene_loaded";
			bad = true;
		}
		if (!bad)
			m.event = evn->str;

		if (!bad && !read_bool(mn.get("enabled"), true, "enabled", m.enabled, reason))
			bad = true;

		if (!bad) {
			const JNode *qn = mn.get("quantize");
			if (qn) {
				if (qn->type != JType::String || !quantize_from_string(qn->str, m.quantize)) {
					reason = "quantize must be section_beat/now/beat/bar/phrase";
					bad = true;
				}
			} else {
				m.quantize = is_section_event(m.event) ? Quantize::SectionBeat : Quantize::Now;
			}
		}
		if (!bad && !read_int(mn.get("offset_ms"), 0, -500, 500, "offset_ms", m.offset_ms, reason))
			bad = true;
		if (!bad && !read_bool(mn.get("lead"), is_section_event(m.event), "lead", m.lead, reason))
			bad = true;
		if (!bad && !read_int(mn.get("min_interval_beats"), 0, 0, 4096, "min_interval_beats",
				      m.min_interval_beats, reason))
			bad = true;
		if (!bad && !read_bool(mn.get("forced_bypass"), true, "forced_bypass", m.forced_bypass, reason))
			bad = true;
		if (!bad && !read_int(mn.get("debounce_ms"), 0, 0, 5000, "debounce_ms", m.debounce_ms, reason))
			bad = true;

		const JNode *an = mn.get("actions");
		if (!bad && (!an || an->type != JType::Array || an->arr.empty())) {
			reason = "actions must be a non-empty array";
			bad = true;
		}
		if (!bad && an->arr.size() > 16) {
			reason = "actions has more than 16 entries";
			bad = true;
		}
		if (!bad) {
			for (const JNode &action : an->arr) {
				EventAction a;
				if (action.type != JType::Object) {
					reason = "action must be an object";
					bad = true;
					break;
				}
				const JNode *use = action.get("use");
				const JNode *route = action.get("route");
				if (use && route) {
					reason = "action has both \"use\" and \"route\"";
					bad = true;
					break;
				}
				if (use) {
					if (use->type != JType::String || use->str.empty()) {
						reason = "\"use\" must be a non-empty string";
						bad = true;
						break;
					}
					a.use_id = use->str;
				} else {
					if (!route || route->type != JType::String || !valid_route(route->str)) {
						reason = "route must start with '/' and use only [a-z0-9/_]";
						bad = true;
						break;
					}
					a.route = route->str;
					if (!read_args(action.get("args"), a, reason)) {
						bad = true;
						break;
					}
				}
				m.actions.push_back(std::move(a));
			}
		}

		if (bad) {
			rejects.push_back(ParseReject{idx, m.id.empty() ? std::string() : m.id, reason});
		} else {
			parsed.push_back(std::move(m));
		}
		++idx;
	}

	// Resolve {"use": id} references: one level, no cycles. A mapping whose reference is missing,
	// rejected, self-referential or deeper than one level is rejected.
	std::map<std::string, size_t> by_id;
	for (size_t i = 0; i < parsed.size(); ++i)
		by_id[parsed[i].id] = i;

	std::vector<Mapping> resolved;
	resolved.reserve(parsed.size());
	for (size_t i = 0; i < parsed.size(); ++i) {
		Mapping m = parsed[i];
		std::string reason;
		bool bad = false;
		std::vector<EventAction> expanded;
		for (const EventAction &a : m.actions) {
			if (a.use_id.empty()) {
				expanded.push_back(a);
				continue;
			}
			auto it = by_id.find(a.use_id);
			if (it == by_id.end()) {
				reason = "use: unknown id '" + a.use_id + "'";
				bad = true;
				break;
			}
			if (it->second == i) {
				reason = "use: cycle back to '" + a.use_id + "'";
				bad = true;
				break;
			}
			const Mapping &ref = parsed[it->second];
			bool ref_has_use = false;
			for (const EventAction &ra : ref.actions)
				if (!ra.use_id.empty()) {
					ref_has_use = true;
					break;
				}
			if (ref_has_use) {
				reason = "use: '" + a.use_id + "' itself uses another mapping (one level only)";
				bad = true;
				break;
			}
			for (const EventAction &ra : ref.actions)
				expanded.push_back(ra);
		}
		if (!bad && (expanded.empty() || expanded.size() > 16)) {
			reason = expanded.empty() ? "use: no actions after resolution"
						  : "use: more than 16 actions after resolution";
			bad = true;
		}
		if (bad) {
			rejects.push_back(ParseReject{m.index, m.id, reason});
		} else {
			m.actions = std::move(expanded);
			resolved.push_back(std::move(m));
		}
	}

	// Success: replace *this and keep the DOM for verbatim round-trip.
	LoadSummary summary;
	summary.total = mappings->arr.size();
	summary.rejected = rejects.size();
	summary.enabled = 0;
	for (const Mapping &m : resolved)
		if (m.enabled)
			++summary.enabled;

	std::shared_ptr<Dom> dom(new Dom);
	dom->root = std::move(root);

	mappings_ = std::move(resolved);
	summary_ = summary;
	dom_ = std::move(dom);
	result.rejects = std::move(rejects);
	result.syntax_ok = true;
	return result;
}

const Mapping *EventMap::find(const std::string &id) const
{
	for (const Mapping &m : mappings_)
		if (m.id == id)
			return &m;
	return nullptr;
}

std::string EventMap::to_json() const
{
	if (!dom_)
		return "{}\n";
	std::string out;
	dump(out, dom_->root, 0);
	out += "\n";
	return out;
}

bool EventMap::set_enabled(const std::string &id, bool enabled)
{
	if (!dom_)
		return false;
	Mapping *m = nullptr;
	for (Mapping &mm : mappings_)
		if (mm.id == id)
			m = &mm;
	if (!m)
		return false;
	JNode *arr = dom_->root.get("mappings");
	if (!arr || arr->type != JType::Array || m->index >= arr->arr.size())
		return false;
	JNode &entry = arr->arr[m->index];
	JNode *field = entry.get("enabled");
	if (field) {
		field->type = JType::Bool;
		field->b = enabled;
		field->raw = enabled ? "true" : "false";
	} else {
		JNode v;
		v.type = JType::Bool;
		v.b = enabled;
		v.raw = enabled ? "true" : "false";
		v.line = entry.line;
		v.col = entry.col;
		entry.obj.emplace_back("enabled", std::move(v));
	}
	m->enabled = enabled;
	return true;
}

bool EventMap::set_min_interval_beats(const std::string &id, int beats)
{
	if (!dom_)
		return false;
	Mapping *m = nullptr;
	for (Mapping &mm : mappings_)
		if (mm.id == id)
			m = &mm;
	if (!m)
		return false;
	JNode *arr = dom_->root.get("mappings");
	if (!arr || arr->type != JType::Array || m->index >= arr->arr.size())
		return false;
	JNode &entry = arr->arr[m->index];
	JNode *field = entry.get("min_interval_beats");
	if (field) {
		field->type = JType::Number;
		field->num = beats;
		field->raw = std::to_string(beats);
	} else {
		JNode v;
		v.type = JType::Number;
		v.num = beats;
		v.raw = std::to_string(beats);
		v.line = entry.line;
		v.col = entry.col;
		entry.obj.emplace_back("min_interval_beats", std::move(v));
	}
	m->min_interval_beats = beats;
	return true;
}

} // namespace cd
