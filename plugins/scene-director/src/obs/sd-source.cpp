/*
Scene Director source: follows Palette Director's beat clock and sends MIDI Next / Prev to a
virtual MIDI port, so a Synesthesia playlist switches on the beat.

The source renders nothing (0 x 0, a no-op video_render): it is the settings holder for the set. Its
video tick drives the scheduler, and the Scene Director dock drives the same scheduler from its
refresh timer when the source is not being ticked (a source outside the active scene), so the
feature works either way. The decision logic itself is pure: core/switch_sched.h.

Two source ids are registered: scene_director_source (this plugin) and clip_director_source, the
Clip Director 1.1.0 id, marked deprecated and hidden from the Add menu, so an existing scene
collection keeps loading unchanged. The old clip keys in the settings are ignored and never
deleted.
*/

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "pd-clock-api.h"
#include "core/midi_msg.h"
#include "core/settings.h"
#include "core/switch_sched.h"
#include "core/types.h"
#include "obs/sd-api.h"
#include "obs/sd-midi.h"

namespace {

constexpr const char *kSourceId = "scene_director_source";
constexpr const char *kSourceIdDeprecated = "clip_director_source";
constexpr const char *kClockAuto = "";
constexpr const char *kClockFree = "\x01free";

// The dock is the main driver; its timer only steps in when the source's own tick has been quiet
// for this long. Half a frame at 120 fps is enough to tell the two apart.
constexpr uint64_t kDriveQuietNs = 25ull * 1000000ull;
constexpr uint64_t kOutputRetryNs = 1000ull * 1000000ull;

struct SdSource {
	obs_source_t *self = nullptr;
	std::mutex mx;

	cd::SourceConfig cfg;
	std::unique_ptr<cd::SwitchScheduler> sched;
	std::unique_ptr<sdmidi::Output> out;

	double beats = 0.0, bpm = 128.0;
	bool clock_ok = false, clock_was_ok = false;
	double free_beats = 0.0;
	cd::Section section = cd::Section::Groove;

	std::string out_want;    // the device the settings last asked for
	uint64_t drive_ns = 0;   // last time the scheduler ran
	uint64_t out_try_ns = 0; // last quiet re-open attempt
	uint64_t switches = 0;
	std::string last_reason;
	double last_switch_beat = 0.0;

	std::deque<std::string> log;
	uint64_t log_seq = 0;
};

const char *type_name(cd::midi::Type t)
{
	return t == cd::midi::Type::Control ? "CC" : "Note";
}

/* mx held: one line for the dock log (and the OBS log) */
void note(SdSource *c, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	char ts[20];
	snprintf(ts, sizeof(ts), "%8.2f  ", c->beats);
	c->log.push_back(std::string(ts) + buf);
	if (c->log.size() > 200)
		c->log.pop_front();
	c->log_seq++;
}

/* mx held: make sure the settings' output device is the one we are aiming at */
void ensure_output(SdSource *c)
{
	sdmidi::Output &o = *c->out;
	if (c->cfg.midi_out_device != c->out_want) {
		c->out_want = c->cfg.midi_out_device;
		if (c->cfg.midi_out_device.empty()) {
			o.close();
			note(c, "MIDI out: none selected");
			return;
		}
		std::string error;
		if (o.open(c->cfg.midi_out_device, &error)) {
			note(c, "MIDI out: %s", o.device_name().c_str());
			obs_log(LOG_INFO, "'%s': MIDI out on '%s'", obs_source_get_name(c->self),
				o.device_name().c_str());
		} else {
			note(c, "MIDI out: %s", error.c_str());
			obs_log(LOG_WARNING, "'%s': MIDI out: %s", obs_source_get_name(c->self), error.c_str());
		}
		return;
	}
	if (o.is_open() || c->out_want.empty())
		return;
	const uint64_t now = os_gettime_ns();
	if (now - c->out_try_ns < kOutputRetryNs)
		return;
	c->out_try_ns = now;
	o.ensure_open(); /* quiet: a virtual port may simply not be running yet */
}

/* mx held: send one switch as MIDI */
void send_event(SdSource *c, const cd::SwitchEvent &e)
{
	const bool prev = e.reason == cd::SwitchEvent::Reason::ManualPrev;
	const cd::midi::MsgSpec &m = prev ? c->cfg.prev : c->cfg.next;
	const uint32_t bytes = m.wire(127); /* Note: velocity 127; CC: value 127 */
	bool ok = false;
	if (m.type == cd::midi::Type::Note) {
		const uint32_t off = cd::midi::encode_note_off(m.channel, m.number, 0);
		ok = c->out->send_with_note_off(bytes, off);
	} else {
		ok = c->out->send(bytes);
	}
	c->switches++;
	c->last_reason = e.reason_name();
	c->last_switch_beat = e.at_beat;
	note(c, "-> %s %s ch%d #%d  msg 0x%06X  at beat %.2f  [%s]%s%s", prev ? "PREV" : "NEXT", type_name(m.type),
	     m.channel, m.number, bytes, e.at_beat, e.reason_name(),
	     e.send_at_beat < e.at_beat - 1e-9 ? " (early)" : "", ok ? "" : " NOT SENT");
	obs_log(LOG_INFO, "'%s': %s at beat %.3f (send %.3f) %s ch%d #%d msg 0x%06X [%s]%s",
		obs_source_get_name(c->self), prev ? "PREV" : "NEXT", e.at_beat, e.send_at_beat, type_name(m.type),
		m.channel, m.number, bytes, e.reason_name(), ok ? "" : " (not sent)");
}

/* ------------------------------------------------------------------------- */
/* clock                                                                      */

/* mx held */
void read_clock(SdSource *c, double seconds, cd::ClockSample &out)
{
	pd_clock k = {};
	bool ok = false;
	const std::string &sel = c->cfg.clock;
	if (sel != kClockFree) {
		if (sel.empty()) {
			ok = pd_clock_read(0, obs_source_get_name(c->self), nullptr, &k);
			if (!ok)
				ok = pd_clock_read(0, nullptr, nullptr, &k);
		} else {
			const size_t bar = sel.find('|');
			const std::string src = sel.substr(0, bar);
			const std::string flt = bar == std::string::npos ? "" : sel.substr(bar + 1);
			ok = pd_clock_read(0, src.c_str(), flt.c_str(), &k);
		}
	}
	if (ok) {
		c->bpm = k.bpm > 1.0 ? k.bpm : c->cfg.fallback_bpm;
		c->beats = k.beats_now + c->cfg.sync_offset_ms / 1000.0 * c->bpm / 60.0;
		c->section = (cd::Section)std::clamp(k.section, 0, 2);
		c->free_beats = c->beats;
	} else {
		c->bpm = std::max(30.0, c->cfg.fallback_bpm);
		c->free_beats += seconds * c->bpm / 60.0;
		c->beats = c->free_beats;
		c->section = cd::Section::Groove;
	}
	if (ok != c->clock_was_ok)
		obs_log(LOG_INFO, "'%s': clock %s", obs_source_get_name(c->self),
			ok ? "following Palette Director" : "free-running (Palette Director clock not available)");
	c->clock_was_ok = ok;
	c->clock_ok = ok;
	/* half a frame of lookahead: a switch fires on the frame nearest its beat, not the first one
	 * after */
	out.beats = c->beats + 0.5 * std::clamp(seconds, 0.0, 0.05) * c->bpm / 60.0;
	out.bpm = c->bpm;
	out.section = c->section;
	out.phrase_beats = ok ? std::max(1, k.phrase_beats) : 32;
	out.paused = ok && k.paused;
	out.valid = true;
}

/* mx held */
void drive_locked(SdSource *c, double seconds)
{
	c->drive_ns = os_gettime_ns();
	cd::ClockSample clk;
	read_clock(c, seconds, clk);
	ensure_output(c);
	for (const cd::SwitchEvent &e : c->sched->tick(clk))
		send_event(c, e);
}

/* mx held: a manual switch, from the dock or a hotkey */
void manual_locked(SdSource *c, bool prev)
{
	cd::ClockSample clk;
	clk.valid = true;
	clk.beats = c->beats;
	clk.bpm = c->bpm;
	clk.section = c->section;
	const cd::SwitchEvent e = prev ? c->sched->manual_prev(clk) : c->sched->manual_next(clk);
	send_event(c, e);
}

/* ------------------------------------------------------------------------- */
/* settings                                                                   */

void sd_defaults(obs_data_t *s)
{
	obs_data_set_default_string(s, "clock_source", kClockAuto);
	obs_data_set_default_int(s, "sync_offset_ms", 0);
	obs_data_set_default_double(s, "fallback_bpm", 128.0);

	obs_data_set_default_string(s, "midi_out_device", "");
	obs_data_set_default_string(s, "next_type", "note");
	obs_data_set_default_int(s, "next_channel", 16);
	obs_data_set_default_int(s, "next_number", 60);
	obs_data_set_default_string(s, "prev_type", "note");
	obs_data_set_default_int(s, "prev_channel", 16);
	obs_data_set_default_int(s, "prev_number", 59);

	obs_data_set_default_bool(s, "trigger_breakdown", true);
	obs_data_set_default_bool(s, "trigger_drop", true);
	obs_data_set_default_bool(s, "trigger_groove_phrase", false);
	obs_data_set_default_int(s, "cooldown_beats", 16);
	obs_data_set_default_double(s, "syn_lead_ms", 0.0);
}

cd::SourceConfig read_settings(obs_data_t *s)
{
	cd::Settings st;
	cd::apply_settings_defaults(st);
	const char *json = obs_data_get_json(s);
	if (json) {
		std::string error;
		if (!st.from_json(json, &error))
			obs_log(LOG_WARNING, "settings are not readable JSON, using defaults: %s", error.c_str());
	}
	return cd::read_config(st);
}

void apply(SdSource *c, obs_data_t *settings)
{
	const cd::SourceConfig n = read_settings(settings);
	std::lock_guard<std::mutex> g(c->mx);
	c->cfg = n;
	cd::SwitchConfig sc;
	sc.trigger_breakdown = n.trigger_breakdown;
	sc.trigger_drop = n.trigger_drop;
	sc.trigger_groove_phrase = n.trigger_groove_phrase;
	sc.cooldown_beats = n.cooldown_beats;
	sc.lead_ms = n.lead_ms;
	c->sched->set_config(sc);
}

/* ------------------------------------------------------------------------- */
/* source callbacks                                                           */

const char *sd_name(void *)
{
	return obs_module_text("SceneDirector");
}

void *sd_create(obs_data_t *settings, obs_source_t *source)
{
	SdSource *c = new SdSource();
	c->self = source;
	c->out = std::make_unique<sdmidi::Output>();
	c->sched = std::make_unique<cd::SwitchScheduler>();
	apply(c, settings);
	obs_log(LOG_INFO, "'%s': source ready (renders nothing: it holds the set and drives MIDI)",
		obs_source_get_name(source));
	return c;
}

void sd_destroy(void *data)
{
	SdSource *c = static_cast<SdSource *>(data);
	{
		std::lock_guard<std::mutex> g(c->mx);
		c->out->close(); /* stops the note-off worker before anything else is freed */
	}
	delete c;
}

void sd_update(void *data, obs_data_t *settings)
{
	apply(static_cast<SdSource *>(data), settings);
}

uint32_t sd_width(void *)
{
	return 0;
}

uint32_t sd_height(void *)
{
	return 0;
}

/* Draws nothing, but must exist: the source declares OBS_SOURCE_VIDEO, and libobs calls
 * video_render without a NULL check when a filter on the source skips to its parent
 * (obs_source_skip_video_filter -> obs_source_default_render). Collections from Clip
 * Director 1.x can carry Spout or shader filters on this source, so a NULL here is a
 * graphics-thread crash at startup. Keep OBS_SOURCE_VIDEO so those filters stay valid. */
void sd_render(void *, gs_effect_t *) {}

void sd_tick(void *data, float seconds)
{
	SdSource *c = static_cast<SdSource *>(data);
	std::lock_guard<std::mutex> g(c->mx);
	drive_locked(c, std::clamp((double)seconds, 0.0, 0.25));
}

void add_clock_list(obs_property_t *p)
{
	obs_property_list_add_string(p, obs_module_text("Clock.Auto"), kClockAuto);
	proc_handler_t *ph = obs_get_proc_handler();
	calldata_t cd;
	calldata_init(&cd);
	if (ph && proc_handler_call(ph, PD_CLOCK_PROC_INSTANCES, &cd)) {
		obs_data_t *root = obs_data_create_from_json(
			(std::string("{\"a\":") + (calldata_string(&cd, "json") ? calldata_string(&cd, "json") : "[]") +
			 "}")
				.c_str());
		obs_data_array_t *arr = root ? obs_data_get_array(root, "a") : nullptr;
		for (size_t i = 0; arr && i < obs_data_array_count(arr); ++i) {
			obs_data_t *it = obs_data_array_item(arr, i);
			const std::string src = obs_data_get_string(it, "source");
			const std::string flt = obs_data_get_string(it, "filter");
			const std::string label = src + " / " + flt;
			const std::string value = src + "|" + flt;
			obs_property_list_add_string(p, label.c_str(), value.c_str());
			obs_data_release(it);
		}
		obs_data_array_release(arr);
		obs_data_release(root);
	}
	calldata_free(&cd);
	obs_property_list_add_string(p, obs_module_text("Clock.Free"), kClockFree);
}

void add_type_list(obs_properties_t *pr, const char *key, const char *label)
{
	obs_property_t *p = obs_properties_add_list(pr, key, label, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, obs_module_text("MidiType.Note"), "note");
	obs_property_list_add_string(p, obs_module_text("MidiType.Cc"), "cc");
}

bool on_refresh_devices(obs_properties_t *props, obs_property_t *, void *)
{
	obs_property_t *p = obs_properties_get(props, "midi_out_device");
	if (p)
		obs_property_list_clear(p);
	obs_property_list_add_string(p, obs_module_text("MidiOutDevice.None"), "");
	for (const sdmidi::Device &d : sdmidi::output_devices())
		obs_property_list_add_string(p, d.name.c_str(), d.name.c_str());
	return true;
}

obs_properties_t *sd_properties(void *)
{
	obs_properties_t *pr = obs_properties_create();

	obs_properties_t *ck = obs_properties_create();
	add_clock_list(obs_properties_add_list(ck, "clock_source", obs_module_text("ClockSource"), OBS_COMBO_TYPE_LIST,
					       OBS_COMBO_FORMAT_STRING));
	obs_properties_add_int_slider(ck, "sync_offset_ms", obs_module_text("SyncOffset"), -200, 200, 1);
	obs_properties_add_float(ck, "fallback_bpm", obs_module_text("FallbackBpm"), 60.0, 200.0, 0.5);
	obs_properties_add_group(pr, "g_clock", obs_module_text("Group.Clock"), OBS_GROUP_NORMAL, ck);

	obs_properties_t *mi = obs_properties_create();
	obs_property_t *dev = obs_properties_add_list(mi, "midi_out_device", obs_module_text("MidiOutDevice"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(dev, obs_module_text("MidiOutDevice.None"), "");
	for (const sdmidi::Device &d : sdmidi::output_devices())
		obs_property_list_add_string(dev, d.name.c_str(), d.name.c_str());
	obs_properties_add_button2(mi, "midi_rescan", obs_module_text("MidiRescan"), on_refresh_devices, nullptr);
	add_type_list(mi, "next_type", obs_module_text("MidiNext"));
	obs_properties_add_int(mi, "next_channel", obs_module_text("MidiChannel"), 1, 16, 1);
	obs_properties_add_int(mi, "next_number", obs_module_text("MidiNumber"), 0, 127, 1);
	add_type_list(mi, "prev_type", obs_module_text("MidiPrev"));
	obs_properties_add_int(mi, "prev_channel", obs_module_text("MidiChannel"), 1, 16, 1);
	obs_properties_add_int(mi, "prev_number", obs_module_text("MidiNumber"), 0, 127, 1);
	obs_properties_add_group(pr, "g_midi", obs_module_text("Group.Midi"), OBS_GROUP_NORMAL, mi);

	obs_properties_t *tr = obs_properties_create();
	obs_properties_add_bool(tr, "trigger_breakdown", obs_module_text("Trigger.Breakdown"));
	obs_properties_add_bool(tr, "trigger_drop", obs_module_text("Trigger.Drop"));
	obs_properties_add_bool(tr, "trigger_groove_phrase", obs_module_text("Trigger.GroovePhrase"));
	obs_properties_add_int(tr, "cooldown_beats", obs_module_text("Cooldown"), 0, 256, 1);
	obs_properties_add_int_slider(tr, "syn_lead_ms", obs_module_text("SceneLead"), 0, 500, 10);
	obs_properties_add_group(pr, "g_triggers", obs_module_text("Group.Triggers"), OBS_GROUP_NORMAL, tr);
	return pr;
}

void register_source(const char *id, uint32_t extra_flags)
{
	struct obs_source_info si = {};
	si.id = id;
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.output_flags = OBS_SOURCE_VIDEO | extra_flags;
	si.get_name = sd_name;
	si.create = sd_create;
	si.destroy = sd_destroy;
	si.update = sd_update;
	si.get_defaults = sd_defaults;
	si.get_properties = sd_properties;
	si.get_width = sd_width;
	si.get_height = sd_height;
	si.video_render = sd_render; /* required with OBS_SOURCE_VIDEO, see sd_render */
	si.video_tick = sd_tick;
	obs_register_source(&si);
}

/* ------------------------------------------------------------------------- */
/* dock API                                                                   */

SdSource *sd_of(obs_source_t *src)
{
	if (!sdapi::is_scene_director(src))
		return nullptr;
	return static_cast<SdSource *>(obs_obj_get_data(src));
}

template<typename F> void write_settings(obs_source_t *src, F &&fn)
{
	obs_data_t *st = obs_source_get_settings(src);
	fn(st);
	obs_source_update(src, nullptr);
	obs_data_release(st);
}

} // namespace

extern "C" void scene_director_source_load(void)
{
	/* clip_director_source is the id a Clip Director 1.1.0 scene collection holds. It is
	 * deprecated (hidden from the Add menu) but still registered with the same implementation, so
	 * those collections keep loading. */
	register_source(kSourceId, 0);
	register_source(kSourceIdDeprecated, OBS_SOURCE_DEPRECATED);
}

namespace sdapi {

bool is_scene_director(obs_source_t *src)
{
	const char *id = src ? obs_source_get_unversioned_id(src) : nullptr;
	if (!id)
		return false;
	return strcmp(id, kSourceId) == 0 || strcmp(id, kSourceIdDeprecated) == 0;
}

bool status(obs_source_t *src, Status &o)
{
	SdSource *c = sd_of(src);
	if (!c)
		return false;
	std::lock_guard<std::mutex> g(c->mx);
	o = Status();
	o.clock_pd = c->clock_ok;
	o.bpm = c->bpm;
	o.beats = c->beats;
	o.section = (int)c->section;

	o.midi_available = sdmidi::available();
	o.midi_open = c->out->is_open();
	o.midi_device = c->cfg.midi_out_device;
	o.midi_open_name = c->out->device_name();
	o.midi_failed = c->out->failed_lookups();
	o.midi_sent = c->out->sent();

	const cd::SwitchConfig &sc = c->sched->config();
	o.trigger_breakdown = sc.trigger_breakdown;
	o.trigger_drop = sc.trigger_drop;
	o.trigger_groove_phrase = sc.trigger_groove_phrase;
	o.cooldown_beats = sc.cooldown_beats;
	o.lead_ms = sc.lead_ms;
	o.last_switch_beat = c->sched->last_switch_beat();

	o.next = c->cfg.next;
	o.prev = c->cfg.prev;
	o.last_reason = c->last_reason;
	o.switches = c->switches;

	o.log_seq = c->log_seq;
	o.log.assign(c->log.begin(), c->log.end());
	return true;
}

void set_midi_out(obs_source_t *src, const std::string &device)
{
	write_settings(src, [&](obs_data_t *s) { obs_data_set_string(s, "midi_out_device", device.c_str()); });
}

void set_messages(obs_source_t *src, const cd::midi::MsgSpec &next, const cd::midi::MsgSpec &prev)
{
	write_settings(src, [&](obs_data_t *s) {
		obs_data_set_string(s, "next_type", cd::midi::type_to_string(next.type));
		obs_data_set_int(s, "next_channel", (int)cd::midi::clamp_channel(next.channel));
		obs_data_set_int(s, "next_number", (int)cd::midi::clamp_number(next.number));
		obs_data_set_string(s, "prev_type", cd::midi::type_to_string(prev.type));
		obs_data_set_int(s, "prev_channel", (int)cd::midi::clamp_channel(prev.channel));
		obs_data_set_int(s, "prev_number", (int)cd::midi::clamp_number(prev.number));
	});
}

void set_triggers(obs_source_t *src, bool breakdown, bool drop, bool groove_phrase, int cooldown_beats, double lead_ms)
{
	write_settings(src, [&](obs_data_t *s) {
		obs_data_set_bool(s, "trigger_breakdown", breakdown);
		obs_data_set_bool(s, "trigger_drop", drop);
		obs_data_set_bool(s, "trigger_groove_phrase", groove_phrase);
		obs_data_set_int(s, "cooldown_beats", std::clamp(cooldown_beats, 0, 4096));
		obs_data_set_double(s, "syn_lead_ms", std::clamp(lead_ms, 0.0, 500.0));
	});
}

void manual_next(obs_source_t *src)
{
	SdSource *c = sd_of(src);
	if (!c)
		return;
	std::lock_guard<std::mutex> g(c->mx);
	manual_locked(c, false);
}

void manual_prev(obs_source_t *src)
{
	SdSource *c = sd_of(src);
	if (!c)
		return;
	std::lock_guard<std::mutex> g(c->mx);
	manual_locked(c, true);
}

void tick(obs_source_t *src)
{
	SdSource *c = sd_of(src);
	if (!c)
		return;
	std::lock_guard<std::mutex> g(c->mx);
	if (os_gettime_ns() - c->drive_ns < kDriveQuietNs)
		return; /* the source's own video tick is running: nothing to do */
	drive_locked(c, 1.0 / 60.0);
}

} // namespace sdapi
