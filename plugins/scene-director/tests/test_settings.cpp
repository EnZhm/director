#include "doctest.h"

#include <string>
#include <vector>

#include "settings.h"

using namespace cd;

namespace {

// A settings object as Clip Director 1.1.0 wrote it: clip pipeline keys, Synesthesia OSC keys, the
// 2.0 MIDI out keys, and the 2.0 trigger keys that survive the rewrite.
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
  "syn_host": "10.0.0.7",
  "syn_send_port": 7000,
  "syn_feedback_port": 7001,
  "syn_on_breakdown": true,
  "syn_on_drop": true,
  "syn_pools": "{\"groove\":[{\"scene\":\"tunnel\"}]}",
  "syn_lead_ms": 250.0,
  "midi_out_device": "Port A",
  "next_type": "note",
  "next_channel": 16,
  "next_number": 60,
  "prev_type": "note",
  "prev_channel": 16,
  "prev_number": 59,
  "trigger_breakdown": true,
  "trigger_drop": false,
  "cooldown_beats": 8
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
	CHECK(c.osc_host == "127.0.0.1");
	CHECK(c.osc_send_port == 7000);
	CHECK(c.osc_feedback_port == 9001);
	CHECK(c.confirm_timeout_ms == 2000);
	CHECK(c.sync_offset_ms == 0);
	CHECK_FALSE(port_conflict(c));
	CHECK(c.lead_ms == doctest::Approx(0.0));
	CHECK(c.trigger_breakdown);
	CHECK(c.trigger_drop);
	CHECK(c.cooldown_beats == 16);
	CHECK_FALSE(c.osc_seeded);
	CHECK_FALSE(c.midi_out_present);
}

TEST_CASE("an empty string is an empty object")
{
	Settings s;
	apply_settings_defaults(s);
	std::string error;
	CHECK(s.from_json("", &error));
	CHECK(read_config(s).cooldown_beats == 16);
}

TEST_CASE("a Clip Director 1.1.0 settings object seeds osc_* from syn_*")
{
	const Settings s = loaded(kOldStyle);
	const SourceConfig c = read_config(s);

	/* osc_* are absent, syn_* present: seeded (spec 4.1) */
	CHECK(c.osc_host == "10.0.0.7");
	CHECK(c.osc_send_port == 7000);
	CHECK(c.osc_feedback_port == 7001);
	CHECK(c.osc_seeded);

	/* 2.0 MIDI out was non-empty: logged (never read into a field) */
	CHECK(c.midi_out_present);

	/* trigger keys kept verbatim (passed to event-map seeding) */
	CHECK(c.trigger_breakdown);
	CHECK_FALSE(c.trigger_drop);
	CHECK(c.cooldown_beats == 8);

	/* syn_lead_ms / sync_offset_ms unchanged */
	CHECK(c.lead_ms == doctest::Approx(250.0));
	CHECK(c.sync_offset_ms == 0);
}

TEST_CASE("explicit osc_* win over syn_* and are not seeded")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_string("osc_host", "192.168.1.9");
	s.set_int("osc_send_port", 7100);
	s.set_int("osc_feedback_port", 7101);
	s.set_string("syn_host", "10.0.0.7");
	s.set_int("syn_send_port", 7000);
	s.set_int("syn_feedback_port", 7001);
	const SourceConfig c = read_config(s);
	CHECK(c.osc_host == "192.168.1.9");
	CHECK(c.osc_send_port == 7100);
	CHECK(c.osc_feedback_port == 7101);
	CHECK_FALSE(c.osc_seeded);
}

TEST_CASE("partial seeding: only the absent osc_* keys come from syn_*")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_string("osc_host", "192.168.1.9"); /* present: kept */
	s.set_int("syn_send_port", 7000);        /* osc_send_port absent: seeded */
	s.set_int("syn_feedback_port", 7001);    /* osc_feedback_port absent: seeded */
	const SourceConfig c = read_config(s);
	CHECK(c.osc_host == "192.168.1.9");
	CHECK(c.osc_send_port == 7000);
	CHECK(c.osc_feedback_port == 7001);
	CHECK(c.osc_seeded);
}

TEST_CASE("confirm_timeout_ms clamps to 100..10000 and defaults to 2000")
{
	Settings s;
	apply_settings_defaults(s);
	CHECK(read_config(s).confirm_timeout_ms == 2000);
	s.set_int("confirm_timeout_ms", 10);
	CHECK(read_config(s).confirm_timeout_ms == 100);
	s.set_int("confirm_timeout_ms", 99999);
	CHECK(read_config(s).confirm_timeout_ms == 10000);
	s.set_int("confirm_timeout_ms", 350);
	CHECK(read_config(s).confirm_timeout_ms == 350);
}

TEST_CASE("an empty or absent midi_out_device is not flagged for the removal log")
{
	Settings s;
	apply_settings_defaults(s);
	CHECK_FALSE(read_config(s).midi_out_present);
	s.set_string("midi_out_device", "");
	CHECK_FALSE(read_config(s).midi_out_present);
	s.set_string("midi_out_device", "loopback 1");
	CHECK(read_config(s).midi_out_present);
}

TEST_CASE("the 2.0 MIDI keys are kept verbatim and round-trip unchanged")
{
	const Settings s = loaded(kOldStyle);
	CHECK(s.has("midi_out_device"));
	CHECK(s.has("next_type"));
	CHECK(s.has("next_channel"));
	CHECK(s.has("next_number"));
	CHECK(s.has("prev_type"));
	CHECK(s.has("prev_channel"));
	CHECK(s.has("prev_number"));

	Settings again;
	apply_settings_defaults(again);
	std::string error;
	REQUIRE(again.from_json(s.to_json(), &error));
	CHECK(again.get_string("midi_out_device") == "Port A");
	CHECK(again.get_string("next_type") == "note");
	CHECK(again.get_int("next_channel") == 16);
	CHECK(again.get_int("next_number") == 60);
	CHECK(again.get_string("prev_type") == "note");
	CHECK(again.get_int("prev_channel") == 16);
	CHECK(again.get_int("prev_number") == 59);
}

TEST_CASE("the removed clip and OSC keys are kept, not dropped")
{
	const Settings s = loaded(kOldStyle);
	for (const char *key :
	     {"folders", "folder_meta", "clip_overrides", "normalize", "normalize_strength", "output_mode", "alpha_lo",
	      "alpha_hi", "seed", "speed_min_pct", "syn_enabled", "syn_host", "syn_send_port", "syn_feedback_port",
	      "syn_on_breakdown", "syn_on_drop", "syn_pools"})
		CHECK(s.has(key));

	Settings again;
	apply_settings_defaults(again);
	std::string error;
	REQUIRE(again.from_json(s.to_json(), &error));
	CHECK(again.get_bool("normalize"));
	CHECK(again.get_int("output_mode") == 1);
	CHECK(again.get_string("syn_host") == "10.0.0.7");
	CHECK(again.get_int("syn_feedback_port") == 7001);
	CHECK(again.get_double("syn_lead_ms") == doctest::Approx(250.0));
	CHECK(again.get_string("syn_pools") == "{\"groove\":[{\"scene\":\"tunnel\"}]}");
}

TEST_CASE("trigger_breakdown / trigger_drop / cooldown_beats clamp on read")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_int("cooldown_beats", 9999);
	s.set_int("sync_offset_ms", 999);
	s.set_double("syn_lead_ms", 999.0);
	const SourceConfig c = read_config(s);
	CHECK(c.cooldown_beats == 4096);
	CHECK(c.sync_offset_ms == 200);
	CHECK(c.lead_ms == doctest::Approx(500.0));
}

TEST_CASE("lead_ms is a fallback key when syn_lead_ms is absent")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_double("lead_ms", 120.0);
	CHECK(read_config(s).lead_ms == doctest::Approx(120.0));
}

TEST_CASE("malformed JSON leaves the store alone")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_string("osc_host", "keep me");
	std::string error;
	CHECK(!s.from_json("{\"broken\": ", &error));
	CHECK(!error.empty());
	CHECK(s.get_string("osc_host") == "keep me");
	CHECK(!s.has("broken"));

	CHECK(!s.from_json("[1,2,3]", &error));
	CHECK(s.get_string("osc_host") == "keep me");
}

TEST_CASE("port guard: feedback == send (non-zero) is a conflict, with the spec 4.6 text")
{
	Settings s;
	apply_settings_defaults(s);
	s.set_int("osc_send_port", 7000);
	s.set_int("osc_feedback_port", 7000);
	SourceConfig c = read_config(s);
	CHECK(port_conflict(c));
	CHECK(port_conflict_text("Scene Director", 7000) ==
	      "'Scene Director': feedback port 7000 equals the send port; set Synesthesia's OSC Output port to a "
	      "different port (default 9001)");

	s.set_int("osc_feedback_port", 0); /* 0 = no feedback: never a conflict */
	CHECK_FALSE(port_conflict(read_config(s)));
	s.set_int("osc_feedback_port", 9001);
	CHECK_FALSE(port_conflict(read_config(s)));
}

TEST_CASE("port guard: a conflict seeded from Clip Director 1.1 syn_* ports is reported the same way")
{
	Settings s; /* a CD 1.1 object: syn_* only, no osc_* */
	s.set_string("syn_host", "127.0.0.1");
	s.set_int("syn_send_port", 7000);
	s.set_int("syn_feedback_port", 7000);
	const SourceConfig c = read_config(s);
	CHECK(c.osc_seeded);
	CHECK(c.osc_feedback_port == 7000);
	CHECK(port_conflict(c));
}
