#include "doctest.h"

#include <string>
#include <vector>

#include "settings.h"

using namespace cd;

namespace {

// A settings object as Clip Director 1.1.0 wrote it: clip pipeline keys, Synesthesia OSC keys and
// the MIDI Direct-related keys that survive the rewrite.
const char *kOldStyle = R"({
  "folders": [{"value": "/media/clips/groove", "hidden": false}],
  "folder_meta": [{"path": "/media/clips/groove", "tag": 1, "weight": 2.0}],
  "clip_overrides": [{"path": "/media/clips/groove/a.mp4", "ban": true, "polarity": 1}],
  "clock_source": "",
  "sync_offset_ms": 0,
  "seed": 7,
  "speed_min_pct": 100,
  "normalize": true,
  "normalize_strength": 0.7,
  "output_mode": 1,
  "alpha_lo": 0.02,
  "alpha_hi": 0.2,
  "syn_enabled": true,
  "syn_host": "127.0.0.1",
  "syn_send_port": 7000,
  "syn_feedback_port": 7001,
  "syn_on_breakdown": true,
  "syn_on_drop": true,
  "syn_pools": "{\"groove\":[{\"scene\":\"tunnel\"}]}",
  "syn_lead_ms": 250.0,
  "midi_out_device": "Port A"
})";

Settings loaded(const char *json)
{
	Settings s;
	apply_settings_defaults(s);
	std::string error;
	REQUIRE(s.from_json(json, &error));
	return s;
}

} // namespace

TEST_CASE("an empty settings object falls back to the defaults")
{
	Settings s;
	apply_settings_defaults(s);
	const SourceConfig c = read_config(s);
	CHECK(c.midi_out_device.empty());
	CHECK(c.trigger_breakdown);
	CHECK(c.trigger_drop);
	CHECK(c.cooldown_beats == 16);
	CHECK(c.lead_ms == doctest::Approx(0.0));
	CHECK(c.next.type == midi::Type::Note);
	CHECK(c.next.channel == 16);
	CHECK(c.next.number == 60);
	CHECK(c.prev.type == midi::Type::Note);
	CHECK(c.prev.channel == 16);
	CHECK(c.prev.number == 59);
}

TEST_CASE("an empty string is an empty object")
{
	Settings s;
	apply_settings_defaults(s);
	std::string error;
	CHECK(s.from_json("", &error));
	CHECK(read_config(s).cooldown_beats == 16);
}

TEST_CASE("a Clip Director 1.1.0 settings object loads with the new defaults")
{
	const Settings s = loaded(kOldStyle);
	const SourceConfig c = read_config(s);

	/* the new keys are not in the old object: the defaults hold */
	CHECK(c.trigger_breakdown);
	CHECK(c.trigger_drop);
	CHECK(c.cooldown_beats == 16);
	CHECK(c.next.number == 60);
	CHECK(c.prev.number == 59);

	/* the keys the old object does carry are read */
	CHECK(c.midi_out_device == "Port A");
	CHECK(c.lead_ms == doctest::Approx(250.0)); /* syn_lead_ms, the key Clip Director used */
	CHECK(c.clock.empty());
}

TEST_CASE("the removed clip and OSC keys are kept, not dropped")
{
	const Settings s = loaded(kOldStyle);
	for (const char *key :
	     {"folders", "folder_meta", "clip_overrides", "normalize", "normalize_strength", "output_mode", "alpha_lo",
	      "alpha_hi", "seed", "speed_min_pct", "syn_enabled", "syn_host", "syn_send_port", "syn_feedback_port",
	      "syn_on_breakdown", "syn_on_drop", "syn_pools"})
		CHECK(s.has(key));

	/* and they survive the round trip through JSON unchanged */
	Settings again;
	apply_settings_defaults(again);
	std::string error;
	REQUIRE(again.from_json(s.to_json(), &error));
	CHECK(again.get_bool("normalize"));
	CHECK(again.get_int("output_mode") == 1);
	CHECK(again.get_string("syn_host") == "127.0.0.1");
	CHECK(again.get_int("syn_feedback_port") == 7001);
	CHECK(again.get_double("syn_lead_ms") == doctest::Approx(250.0));
	CHECK(again.get_string("syn_pools") == "{\"groove\":[{\"scene\":\"tunnel\"}]}");

	/* nested values are copied whole */
	const Settings fresh = loaded("{\"folders\":[{\"value\":\"a\",\"hidden\":true}],\"x\":{\"y\":[1,2]}}");
	CHECK(fresh.has("folders"));
	CHECK(fresh.has("x"));
}

TEST_CASE("a round trip through JSON keeps every key and the config")
{
	const Settings s = loaded(kOldStyle);
	const std::string json = s.to_json();
	Settings back;
	std::string error;
	REQUIRE(back.from_json(json, &error));
	/* to_json only writes keys that have a value (read or set), so the untouched defaults are
	 * absent; what the object did carry is all there */
	CHECK(back.has("midi_out_device"));
	CHECK(back.has("syn_lead_ms"));
	CHECK(read_config(back).midi_out_device == "Port A");
}

TEST_CASE("explicitly set values win over the defaults and are clamped on read")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_int("next_channel", 0);  /* clamps up to 1 */
	s.set_int("next_number", 999); /* clamps down to 127 */
	s.set_string("next_type", "cc");
	s.set_int("prev_channel", 99); /* clamps to 16 */
	s.set_int("prev_number", -4);  /* clamps to 0 */
	s.set_int("cooldown_beats", 4);
	s.set_double("syn_lead_ms", 999.0); /* clamps to 500 */
	s.set_string("midi_out_device", "loopback 1");

	const SourceConfig c = read_config(s);
	CHECK(c.next.channel == 1);
	CHECK(c.next.number == 127);
	CHECK(c.next.type == midi::Type::Control);
	CHECK(c.prev.channel == 16);
	CHECK(c.prev.number == 0);
	CHECK(c.cooldown_beats == 4);
	CHECK(c.lead_ms == doctest::Approx(500.0));
	CHECK(c.midi_out_device == "loopback 1");
}

TEST_CASE("an unknown type string falls back to Note, and lead_ms is a fallback key")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_string("next_type", "pitchbend");
	s.set_double("lead_ms", 120.0); /* syn_lead_ms absent: read the fallback */
	const SourceConfig c = read_config(s);
	CHECK(c.next.type == midi::Type::Note);
	CHECK(c.lead_ms == doctest::Approx(120.0));
}

TEST_CASE("malformed JSON leaves the store alone")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_string("midi_out_device", "keep me");
	std::string error;
	CHECK(!s.from_json("{\"broken\": ", &error));
	CHECK(!error.empty());
	CHECK(s.get_string("midi_out_device") == "keep me");
	CHECK(!s.has("broken"));

	CHECK(!s.from_json("[1,2,3]", &error));
	CHECK(s.get_string("midi_out_device") == "keep me");
}
