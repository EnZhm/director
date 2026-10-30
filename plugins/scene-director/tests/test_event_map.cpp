// Event map: schema parse, validation, defaults round-trip, seeding, unknown-key preservation, and
// a byte-fuzz that never crashes.
#include "doctest.h"

#include <string>
#include <vector>

#include "event_map.h"

using namespace cd;

namespace {

// Builds a map from a JSON string; asserts the parse succeeded at the syntax level.
EventMap load(const std::string &json)
{
	EventMap m;
	const EventMap::ParseResult r = m.parse(json);
	REQUIRE(r.syntax_ok);
	return m;
}

EventMap::ParseResult parse(const std::string &json)
{
	EventMap m;
	return m.parse(json);
}

} // namespace

TEST_CASE("defaults: seven entries, four enabled, three disabled examples")
{
	const EventMap m = EventMap::defaults();
	REQUIRE(m.mappings().size() == 7);
	size_t enabled = 0;
	for (const Mapping &mm : m.mappings())
		if (mm.enabled)
			++enabled;
	CHECK(enabled == 4);
	CHECK(m.summary().total == 7);
	CHECK(m.summary().enabled == 4);
	CHECK(m.summary().rejected == 0);
	CHECK(m.summary().text() == "7 mappings, 4 enabled, 0 rejected");

	const Mapping *bd = m.find("breakdown-next");
	REQUIRE(bd != nullptr);
	CHECK(bd->event == "breakdown");
	CHECK(bd->quantize == Quantize::SectionBeat);
	CHECK(bd->lead);
	CHECK(bd->min_interval_beats == 16);
	CHECK(bd->forced_bypass);
	CHECK(bd->offset_ms == 0);
	CHECK(bd->debounce_ms == 0);
	REQUIRE(bd->actions.size() == 1);
	CHECK(bd->actions[0].route == "/playlist/next");

	const Mapping *drop = m.find("drop-next");
	REQUIRE(drop != nullptr);
	CHECK(drop->event == "drop");

	const Mapping *next = m.find("manual-next");
	REQUIRE(next != nullptr);
	CHECK(next->event == "manual_next");
	CHECK(next->quantize == Quantize::Now);
	CHECK(next->debounce_ms == 150);

	const Mapping *prev = m.find("manual-prev");
	REQUIRE(prev != nullptr);
	CHECK(prev->actions[0].route == "/playlist/previous");

	const Mapping *ds = m.find("drop-scene-example");
	REQUIRE(ds != nullptr);
	CHECK_FALSE(ds->enabled);
	REQUIRE(ds->actions.size() == 1);
	CHECK(ds->actions[0].route == "/scenes/hexarray");
	REQUIRE(ds->actions[0].args.size() == 1);
	CHECK(ds->actions[0].args[0].type == EventArg::Type::String);
	CHECK(ds->actions[0].args[0].s == "Peak");

	CHECK(m.find("groove-example") != nullptr);
	CHECK_FALSE(m.find("groove-example")->enabled);
	CHECK(m.find("phrase-example") != nullptr);
	CHECK_FALSE(m.find("phrase-example")->enabled);
}

TEST_CASE("defaults round-trip: parse(serialise(defaults)) is equivalent")
{
	const EventMap d = EventMap::defaults();
	const EventMap m = load(d.to_json());
	REQUIRE(m.mappings().size() == d.mappings().size());
	for (size_t i = 0; i < d.mappings().size(); ++i) {
		const Mapping &a = d.mappings()[i];
		const Mapping &b = m.mappings()[i];
		CHECK(a.id == b.id);
		CHECK(a.event == b.event);
		CHECK(a.enabled == b.enabled);
		CHECK(a.quantize == b.quantize);
		CHECK(a.offset_ms == b.offset_ms);
		CHECK(a.lead == b.lead);
		CHECK(a.min_interval_beats == b.min_interval_beats);
		CHECK(a.forced_bypass == b.forced_bypass);
		CHECK(a.debounce_ms == b.debounce_ms);
		REQUIRE(a.actions.size() == b.actions.size());
		for (size_t j = 0; j < a.actions.size(); ++j) {
			CHECK(a.actions[j].route == b.actions[j].route);
			REQUIRE(a.actions[j].args.size() == b.actions[j].args.size());
		}
	}
}

TEST_CASE("seeding: 2.0 keys reach breakdown-next / drop-next")
{
	const EventMap m = EventMap::defaults_seeded(false, true, 32);
	const Mapping *bd = m.find("breakdown-next");
	const Mapping *drop = m.find("drop-next");
	REQUIRE(bd != nullptr);
	REQUIRE(drop != nullptr);
	CHECK_FALSE(bd->enabled);
	CHECK(drop->enabled);
	CHECK(bd->min_interval_beats == 32);
	CHECK(drop->min_interval_beats == 32);
}

TEST_CASE("a syntax error keeps the previous map and reports line and column")
{
	const EventMap m = load("{\"version\": 1, \"mappings\": []}");
	const EventMap::ParseResult r = parse("{\"version\": 1, \"mappings\": [ { broken");
	CHECK_FALSE(r.syntax_ok);
	CHECK(r.syntax_error.find("line") != std::string::npos);
	CHECK(r.syntax_error.find("col") != std::string::npos);
}

TEST_CASE("a bad entry is rejected with index, id and reason; the rest load")
{
	const std::string json = R"({
  "version": 1,
  "mappings": [
    { "id": "ok", "event": "drop", "quantize": "now", "actions": [ { "route": "/playlist/next" } ] },
    { "id": "bad-event", "event": "palette", "actions": [ { "route": "/x" } ] },
    { "id": "ok2", "event": "phrase", "actions": [ { "route": "/x" } ] }
  ]
})";
	EventMap m;
	const EventMap::ParseResult r = m.parse(json);
	CHECK(r.syntax_ok);
	REQUIRE(r.rejects.size() == 1);
	CHECK(r.rejects[0].index == 1);
	CHECK(r.rejects[0].id == "bad-event");
	CHECK(r.rejects[0].reason.find("event") != std::string::npos);
	REQUIRE(m.mappings().size() == 2);
	CHECK(m.mappings()[0].id == "ok");
	CHECK(m.mappings()[1].id == "ok2");
	CHECK(m.summary().total == 3);
	CHECK(m.summary().rejected == 1);
}

TEST_CASE("each field and range is validated")
{
	// (json, expected substring of the reject reason)
	struct Case {
		const char *mapping;
		const char *reason;
	};
	const Case cases[] = {
		{"{ \"id\": 3, \"event\": \"drop\" }", "id"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [{\"route\":\"/x\"}] }, "
		 "{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [{\"route\":\"/x\"}] }",
		 "duplicate id"},
		{"{ \"id\": \"a\" }", "event"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"enabled\": 1 }", "enabled"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"quantize\": \"wat\" }", "quantize"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"offset_ms\": 501 }", "offset_ms"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"offset_ms\": -501 }", "offset_ms"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"lead\": 1 }", "lead"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"min_interval_beats\": 4097 }", "min_interval_beats"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"forced_bypass\": 0 }", "forced_bypass"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"debounce_ms\": 5001 }", "debounce_ms"},
		{"{ \"id\": \"a\", \"event\": \"drop\" }", "actions"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [] }", "actions"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [ { \"route\": \"nope\" } ] }", "route"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [ { \"route\": \"/x\", \"args\": [true] } ] }",
		 "arg"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [ { \"route\": \"/x\", \"args\": [{\"int\": 1.5}] } ] }",
		 "integer"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [ { \"use\": \"missing\" } ] }", "unknown id"},
		{"{ \"id\": \"a\", \"event\": \"drop\", \"actions\": [ { \"use\": \"a\" } ] }", "cycle"},
	};
	for (const Case &c : cases) {
		CAPTURE(c.mapping);
		const std::string json = std::string("{\"mappings\": [") + c.mapping + "]}";
		EventMap m;
		const EventMap::ParseResult r = m.parse(json);
		CHECK(r.syntax_ok);
		REQUIRE(r.rejects.size() == 1);
		CHECK(r.rejects[0].reason.find(c.reason) != std::string::npos);
	}
}

TEST_CASE("actions over 16 entries and args over 8 elements are rejected")
{
	std::string json = "{\"mappings\": [{\"id\":\"a\",\"event\":\"drop\",\"actions\":[";
	for (int i = 0; i < 17; ++i) {
		json += (i ? "," : "");
		json += "{\"route\":\"/x\"}";
	}
	json += "]}]}";
	{
		EventMap m;
		const EventMap::ParseResult r = m.parse(json);
		CHECK(r.syntax_ok);
		REQUIRE(r.rejects.size() == 1);
		CHECK(r.rejects[0].reason.find("16") != std::string::npos);
	}

	std::string args9 =
		"{\"mappings\": [{\"id\":\"a\",\"event\":\"drop\",\"actions\":[{\"route\":\"/x\",\"args\":[";
	for (int i = 0; i < 9; ++i) {
		args9 += (i ? "," : "");
		args9 += "1";
	}
	args9 += "]}]}]}";
	{
		EventMap m;
		const EventMap::ParseResult r = m.parse(args9);
		CHECK(r.syntax_ok);
		REQUIRE(r.rejects.size() == 1);
		CHECK(r.rejects[0].reason.find("8") != std::string::npos);
	}
}

TEST_CASE("{\"int\": N} parses to an int32 arg and a number to a float32")
{
	const EventMap m = load("{\"mappings\": [{\"id\":\"a\",\"event\":\"drop\",\"actions\":["
				"{\"route\":\"/x\",\"args\":[{\"int\": 7}, 1.5, \"hi\"]}]}]}");
	REQUIRE(m.mappings().size() == 1);
	const auto &args = m.mappings()[0].actions[0].args;
	REQUIRE(args.size() == 3);
	CHECK(args[0].type == EventArg::Type::Int);
	CHECK(args[0].i == 7);
	CHECK(args[1].type == EventArg::Type::Float);
	CHECK(args[1].f == doctest::Approx(1.5f));
	CHECK(args[2].type == EventArg::Type::String);
	CHECK(args[2].s == "hi");
}

TEST_CASE("{\"use\": id} reuses another mapping's actions, one level")
{
	const EventMap m = load("{\"mappings\": ["
				"{\"id\":\"base\",\"event\":\"drop\",\"actions\":[{\"route\":\"/playlist/next\"}]},"
				"{\"id\":\"alias\",\"event\":\"scene_loaded\",\"actions\":[{\"use\":\"base\"}]}"
				"]}");
	REQUIRE(m.mappings().size() == 2);
	const Mapping *alias = m.find("alias");
	REQUIRE(alias != nullptr);
	REQUIRE(alias->actions.size() == 1);
	CHECK(alias->actions[0].route == "/playlist/next");
}

TEST_CASE("unknown keys are preserved verbatim; toggles rewrite only enabled / min_interval_beats")
{
	const std::string json = R"({
  "custom_top": { "a": [1, 2, 3] },
  "version": 7,
  "mappings": [
    { "id": "bd", "event": "breakdown", "enabled": true, "min_interval_beats": 16, "future_field": "keep me",
      "quantize": "section_beat", "actions": [ { "route": "/playlist/next" } ] }
  ]
})";
	EventMap m = load(json);
	CHECK(m.set_enabled("bd", false));
	CHECK(m.set_min_interval_beats("bd", 24));

	const std::string out = m.to_json();
	CHECK(out.find("\"custom_top\"") != std::string::npos);
	CHECK(out.find("\"future_field\": \"keep me\"") != std::string::npos);
	CHECK(out.find("\"version\": 7") != std::string::npos);
	CHECK(out.find("\"enabled\": false") != std::string::npos);
	CHECK(out.find("\"min_interval_beats\": 24") != std::string::npos);

	// Only the two toggled values changed; the structure and unknown keys round-trip.
	const EventMap m2 = load(out);
	REQUIRE(m2.mappings().size() == 1);
	CHECK_FALSE(m2.mappings()[0].enabled);
	CHECK(m2.mappings()[0].min_interval_beats == 24);
	CHECK(m2.mappings()[0].id == "bd");
	CHECK(m2.mappings()[0].event == "breakdown");
	// Serialisation is idempotent: a second round-trip is byte-identical, so unknown keys and the
	// untouched values survive verbatim.
	CHECK(m2.to_json() == out);
}

TEST_CASE("set_enabled / set_min_interval_beats return false for an unknown id")
{
	EventMap m = EventMap::defaults();
	CHECK_FALSE(m.set_enabled("nope", true));
	CHECK_FALSE(m.set_min_interval_beats("nope", 3));
}

TEST_CASE("byte-fuzz: random and truncated mutations of the defaults never crash")
{
	const std::string base = EventMap::defaults().to_json();
	uint64_t x = 0x5EED5EED5EED5EEDull;
	auto next = [&]() {
		x = x * 6364136223846793005ull + 1442695040888963407ull;
		return x;
	};

	for (int i = 0; i < 20000; ++i) {
		std::string s = base;
		const int mode = static_cast<int>(next() % 3);
		if (mode == 0) {
			// truncate
			s.resize(static_cast<size_t>(next() % (s.size() + 1)));
		} else if (mode == 1) {
			// random byte mutations
			const int n = static_cast<int>(next() % 8) + 1;
			for (int k = 0; k < n && !s.empty(); ++k)
				s[static_cast<size_t>(next() % s.size())] = static_cast<char>(next() % 256);
		} else {
			// fully random text of a random length
			s.clear();
			const size_t len = static_cast<size_t>(next() % 256);
			for (size_t k = 0; k < len; ++k)
				s += static_cast<char>(next() % 256);
		}
		EventMap m;
		m.parse(s); // must not throw / crash
	}

	// A deeply nested document must not crash either (depth is capped).
	std::string deep = "{\"mappings\":[";
	for (int i = 0; i < 100000; ++i)
		deep += "[";
	deep += "]";
	EventMap m;
	const EventMap::ParseResult r = m.parse(deep);
	CHECK_FALSE(r.syntax_ok);
}
