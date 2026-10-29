/*
Scene Director source: follows Music Director's clock and sends MIDI Next / Prev to a
virtual MIDI port, so a Synesthesia playlist switches on the beat.

The source renders nothing (0 x 0, a no-op video_render): it is the settings holder for the set. Its
video tick drives the scheduler, and the Scene Director dock drives the same scheduler from its
refresh timer when the source is not being ticked (a source outside the active scene), so the
feature works either way. The decision logic itself is pure: core/switch_sched.h.

One source id is registered: scene_director_source. The old clip_director_source alias is gone -
Clip Director owns that id now - and the old clip keys in the settings are ignored, never deleted.

With Music Director missing the source idles: no automatic switches, one log line, and the dock
reads "Waiting for Music Director". Manual Next / Prev still work. With several Scene Director
sources sharing the clock, one section edge sends exactly one Next (core/seq_guard.h).
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

#include "director-clock.h"
#include "director-follow.h"
#include "midi_msg.h"
#include "core/seq_guard.h"
#include "core/settings.h"
#include "core/switch_sched.h"
#include "core/types.h"
#include "obs/sd-api.h"
#include "director-midi.h"

namespace {

constexpr const char *kSourceId = "scene_director_source";

// The dock is the main driver; its timer only steps in when the source's own tick has been quiet
// for this long. Half a frame at 120 fps is enough to tell the two apart.
constexpr uint64_t kDriveQuietNs = 25ull * 1000000ull;
constexpr uint64_t kOutputRetryNs = 1000ull * 1000000ull;

// One guard for the whole process: with several Scene Director sources on the one Music Director
// clock, the first source to claim a section_seq is the one that sends its Next.
cd::SeqGuard g_seq_guard;

struct SdSource {
	obs_source_t *self = nullptr;
	std::mutex mx;

	cd::SourceConfig cfg;
	std::unique_ptr<cd::SwitchScheduler> sched;
	std::unique_ptr<director_midi::Output> out;
	director_follow::Follower follower;

	double beats = 0.0, bpm = 128.0;
	bool clock_ok = false, clock_state_known = false;
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
	director_midi::Output &o = *c->out;
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

/* mx held: read Music Director's clock into a ClockSample. Leaves `valid` false when Music
 * Director is not loaded (no free-running fallback: the scheduler idles). */
void read_clock(SdSource *c, double seconds, cd::ClockSample &out)
{
	director_clock k = {};
	const bool ok = director_clock_read(&k);
	if (ok) {
		c->bpm = k.bpm > 1.0 ? k.bpm : 128.0;
		c->beats = k.beats_now + c->cfg.sync_offset_ms / 1000.0 * c->bpm / 60.0;
		c->section = (cd::Section)std::clamp(k.section, 0, 2);
	}
	out.beats = c->beats;
	out.bpm = c->bpm;
	out.section = c->section;
	out.phrase_beats = ok ? std::max(1, (int)k.phrase_beats) : 32;
	out.paused = ok && k.paused;
	out.section_seq = k.section_seq;
	out.section_beat = k.section_beat;
	out.section_forced = k.section_forced;
	out.epoch = k.epoch;
	out.beat = k.beat;
	out.phrase = k.phrase;
	/* half a frame of lookahead: a switch fires on the frame nearest its beat, not the first one
	 * after */
	out.beats = c->beats + (ok ? 0.5 * std::clamp(seconds, 0.0, 0.05) * c->bpm / 60.0 : 0.0);
	out.valid = ok;
}

/* mx held */
void drive_locked(SdSource *c, double seconds)
{
	c->drive_ns = os_gettime_ns();
	cd::ClockSample clk;
	read_clock(c, seconds, clk);

	/* one clear line when Music Director appears or goes away */
	if (!c->clock_state_known || c->clock_ok != clk.valid) {
		c->clock_state_known = true;
		c->clock_ok = clk.valid;
		obs_log(LOG_INFO, "[scene-director] %s",
			clk.valid ? "following Music Director" : "Music Director not found: Synesthesia holds");
	}
	if (!clk.valid)
		return; /* idle: no automatic switches (manual Next / Prev still work) */

	director_follow::Sample s;
	s.ok = clk.valid;
	s.beat = clk.beat;
	s.phrase = clk.phrase;
	s.section = (int)clk.section;
	s.section_seq = clk.section_seq;
	s.section_beat = clk.section_beat;
	s.section_forced = clk.section_forced;
	s.epoch = clk.epoch;
	const director_follow::Edges e = c->follower.update(s);

	ensure_output(c);
	if (e.resync) {
		/* first sample, Music Director found again, or an epoch change: re-anchor, no edge */
		c->sched->resync(clk);
		return;
	}
	for (const cd::SwitchEvent &ev : c->sched->tick(clk)) {
		/* one section edge, one Next across every Scene Director source */
		if (ev.section_edge && !g_seq_guard.claim(ev.section_seq)) {
			obs_log(LOG_DEBUG, "'%s': section seq %lld already sent by another source; skip",
				obs_source_get_name(c->self), (long long)ev.section_seq);
			continue;
		}
		if (ev.section_edge)
			obs_log(LOG_INFO, "[scene-director] MIDI Next seq=%lld beat=%lld", (long long)ev.section_seq,
				(long long)ev.section_beat);
		send_event(c, ev);
	}
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
	/* clock_source is kept in saved data (never read or deleted); Scene Director follows Music
	 * Director unconditionally now. */
	obs_data_set_default_string(s, "clock_source", "");
	obs_data_set_default_int(s, "sync_offset_ms", 0);

	obs_data_set_default_string(s, "midi_out_device", "");
	obs_data_set_default_string(s, "next_type", "note");
	obs_data_set_default_int(s, "next_channel", 16);
	obs_data_set_default_int(s, "next_number", 60);
	obs_data_set_default_string(s, "prev_type", "note");
	obs_data_set_default_int(s, "prev_channel", 16);
	obs_data_set_default_int(s, "prev_number", 59);

	obs_data_set_default_bool(s, "trigger_breakdown", true);
	obs_data_set_default_bool(s, "trigger_drop", true);
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
	c->out = std::make_unique<director_midi::Output>();
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
	for (const director_midi::Device &d : director_midi::output_devices())
		obs_property_list_add_string(p, d.name.c_str(), d.name.c_str());
	return true;
}

obs_properties_t *sd_properties(void *)
{
	obs_properties_t *pr = obs_properties_create();

	obs_properties_t *ck = obs_properties_create();
	/* clock_source is no longer shown (Scene Director always follows Music Director); the key
	 * stays in the saved settings, it is just not read or deleted. */
	obs_properties_add_int_slider(ck, "sync_offset_ms", obs_module_text("SyncOffset"), -200, 200, 1);
	obs_properties_add_group(pr, "g_clock", obs_module_text("Group.Clock"), OBS_GROUP_NORMAL, ck);

	obs_properties_t *mi = obs_properties_create();
	obs_property_t *dev = obs_properties_add_list(mi, "midi_out_device", obs_module_text("MidiOutDevice"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(dev, obs_module_text("MidiOutDevice.None"), "");
	for (const director_midi::Device &d : director_midi::output_devices())
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
	obs_properties_add_int(tr, "cooldown_beats", obs_module_text("Cooldown"), 0, 256, 1);
	obs_properties_add_int_slider(tr, "syn_lead_ms", obs_module_text("SceneLead"), 0, 500, 10);
	obs_properties_add_group(pr, "g_triggers", obs_module_text("Group.Triggers"), OBS_GROUP_NORMAL, tr);
	return pr;
}

void register_source(const char *id)
{
	struct obs_source_info si = {};
	si.id = id;
	si.type = OBS_SOURCE_TYPE_INPUT;
	si.output_flags = OBS_SOURCE_VIDEO;
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
	register_source(kSourceId);
}

namespace sdapi {

bool is_scene_director(obs_source_t *src)
{
	const char *id = src ? obs_source_get_unversioned_id(src) : nullptr;
	if (!id)
		return false;
	return strcmp(id, kSourceId) == 0;
}

bool status(obs_source_t *src, Status &o)
{
	SdSource *c = sd_of(src);
	if (!c)
		return false;
	std::lock_guard<std::mutex> g(c->mx);
	o = Status();
	o.clock_following = c->clock_ok;
	o.bpm = c->bpm;
	o.beats = c->beats;
	o.section = (int)c->section;

	o.midi_available = director_midi::available();
	o.midi_open = c->out->is_open();
	o.midi_device = c->cfg.midi_out_device;
	o.midi_open_name = c->out->device_name();
	o.midi_failed = c->out->failed_lookups();
	o.midi_sent = c->out->sent();

	const cd::SwitchConfig &sc = c->sched->config();
	o.trigger_breakdown = sc.trigger_breakdown;
	o.trigger_drop = sc.trigger_drop;
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

void set_triggers(obs_source_t *src, bool breakdown, bool drop, int cooldown_beats, double lead_ms)
{
	write_settings(src, [&](obs_data_t *s) {
		obs_data_set_bool(s, "trigger_breakdown", breakdown);
		obs_data_set_bool(s, "trigger_drop", drop);
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
