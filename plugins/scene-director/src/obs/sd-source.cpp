/*
Scene Director source: follows Music Director's clock and drives Synesthesia's playlist over OSC.

The source renders nothing (0 x 0, a no-op video_render): it is the settings holder for the set. Its
video tick drives the event engine, and the Scene Director dock drives the same engine from its
refresh timer when the source is not being ticked (a source outside the active scene), so the
feature works either way. The decision logic is pure: core/event_engine.h and core/confirm.h; the
OSC codec and transport live in common/osc.

One source id is registered: scene_director_source. The old clip_director_source alias is gone -
Clip Director owns that id now - and the old clip keys in the settings are ignored, never deleted.
The 2.0 MIDI out is gone (decision 2026-10-30): Synesthesia is driven over OSC only, and the 2.0
MIDI keys are kept verbatim and never read.

With Music Director missing the source idles: no automatic switches, one log line, and the dock
reads "Waiting for Music Director". Manual Next / Prev still work. With several Scene Director
sources sharing the clock, one section edge sends exactly one Next (a process-wide event guard).
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
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "director-clock.h"
#include "director-follow.h"
#include "osc.h"
#include "osc-transport.h"
#include "core/confirm.h"
#include "core/event_engine.h"
#include "core/event_guard.h"
#include "core/event_map.h"
#include "core/settings.h"
#include "core/types.h"
#include "obs/sd-api.h"

namespace {

constexpr const char *kSourceId = "scene_director_source";

// The dock is the main driver; its timer only steps in when the source's own tick has been quiet
// for this long. Half a frame at 120 fps is enough to tell the two apart.
constexpr uint64_t kDriveQuietNs = 25ull * 1000000ull;

// One guard for the whole process: with several Scene Director sources on the one Music Director
// clock, the first source to claim an event's (kind, key) is the one that sends.
cd::EventGuard g_guard;

// The shared event map (one file for every SD source; ports stay per source, decision 2026-10-30).
std::mutex g_map_mx;
std::string g_map_path;
cd::EventMap g_map;
std::string g_map_summary;
bool g_map_loaded = false; // a map has been read from disk (or seeded + written)

struct SdSource {
	obs_source_t *self = nullptr;
	std::mutex mx;

	cd::SourceConfig cfg;
	std::unique_ptr<cd::EventEngine> engine;
	std::unique_ptr<director_osc::OscTransport> osc;
	director_follow::Follower follower;
	cd::ConfirmTracker confirm;
	cd::SourceClaims claims; // the events this source owns in the process-wide guard

	double beats = 0.0, bpm = 128.0;
	bool clock_ok = false, clock_state_known = false;
	cd::Section section = cd::Section::Groove;

	std::string osc_host; // the transport's last applied host/ports
	int osc_send_port = 0, osc_feedback_port = 0;
	uint64_t drive_ns = 0;     // last time the scheduler ran
	uint64_t last_dropped = 0; // last seen send-queue drop count
	uint64_t switches = 0;
	std::string last_reason;
	double last_switch_beat = 0.0;
	bool migration_logged = false;

	std::vector<cd::QueuedEvent> queued; // manual / scene_loaded events for the next tick

	std::deque<std::string> log;
	uint64_t log_seq = 0;
};

std::mutex g_sources_mx;
std::vector<SdSource *> g_sources;

/* ------------------------------------------------------------------------- */
/* shared event map (spec 4.3)                                                */

std::string map_path()
{
	char *p = obs_module_config_path("event-map.json");
	std::string s = p ? p : "";
	bfree(p);
	return s;
}

void ensure_parent_dir(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos)
		return;
	os_mkdirs(path.substr(0, slash).c_str());
}

bool read_file_text(const std::string &path, std::string &out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

bool write_file_text(const std::string &path, const std::string &text)
{
	ensure_parent_dir(path);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << text;
	out.flush();
	return out.good();
}

/* g_map_mx held: read event-map.json into g_map. Returns false when the file is missing (the
 * previous map is left untouched). A syntax error keeps the last good map and logs the reject
 * (spec 4.3). */
bool map_read_locked()
{
	std::string text;
	if (!read_file_text(g_map_path, text))
		return false;

	cd::EventMap candidate;
	const cd::EventMap::ParseResult r = candidate.parse(text);
	if (!r.syntax_ok) {
		obs_log(LOG_WARNING, "[scene-director] event-map.json: %s; keeping the previous map",
			r.syntax_error.c_str());
		return true; // kept the previous (or default) map
	}
	for (const cd::ParseReject &rej : r.rejects)
		obs_log(LOG_WARNING, "[scene-director] event-map.json: mapping %zu '%s': %s; skipped", rej.index,
			rej.id.c_str(), rej.reason.c_str());
	g_map = candidate;
	g_map_summary = candidate.summary().text();
	obs_log(LOG_INFO, "[scene-director] event-map.json: %s", g_map_summary.c_str());
	return true;
}

/* g_map_mx held: first-run seeding (spec 4.3): write the default map with the 2.0 trigger keys
 * applied and adopt it. */
void map_seed_locked(bool trigger_breakdown, bool trigger_drop, int cooldown_beats)
{
	g_map = cd::EventMap::defaults_seeded(trigger_breakdown, trigger_drop, cooldown_beats);
	g_map_summary = g_map.summary().text();
	write_file_text(g_map_path, g_map.to_json());
	g_map_loaded = true;
	obs_log(LOG_INFO, "[scene-director] event-map.json: %s", g_map_summary.c_str());
}

/* g_map_mx held: write the map back to disk (only the toggled fields changed; everything else is
 * preserved verbatim). */
void map_write_locked()
{
	write_file_text(g_map_path, g_map.to_json());
}

/* g_map_mx held: push the current map into every registered source's engine. */
void push_map_to_sources()
{
	std::lock_guard<std::mutex> sg(g_sources_mx);
	for (SdSource *c : g_sources) {
		std::lock_guard<std::mutex> cg(c->mx);
		c->engine->set_map(g_map);
	}
}

void map_init()
{
	std::lock_guard<std::mutex> g(g_map_mx);
	if (!g_map_path.empty())
		return;
	g_map_path = map_path();
	g_map = cd::EventMap::defaults();
	g_map_summary = g_map.summary().text();
	if (map_read_locked())
		g_map_loaded = true; // else: first source seeds + writes below
}

/* ------------------------------------------------------------------------- */
/* logging                                                                    */

/* mx held: one line for the dock log (and mirrored to the OBS log by the caller) */
void note(SdSource *c, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	c->log.push_back(buf);
	if (c->log.size() > 200)
		c->log.pop_front();
	c->log_seq++;
}

std::string arg_text(const cd::EventArg &a)
{
	char buf[48];
	switch (a.type) {
	case cd::EventArg::Type::Int:
		return std::to_string(a.i);
	case cd::EventArg::Type::Float:
		std::snprintf(buf, sizeof(buf), "%.6g", (double)a.f);
		return buf;
	case cd::EventArg::Type::String:
		return a.s;
	}
	return "";
}

std::string args_text(const std::vector<std::string> &args)
{
	std::string s;
	for (const std::string &a : args) {
		if (!s.empty())
			s += " ";
		s += a;
	}
	return s;
}

bool section_event(const std::string &e)
{
	return e == "breakdown" || e == "drop" || e == "groove" || e == "section";
}

/* ------------------------------------------------------------------------- */
/* clock                                                                      */

/* mx held: read Music Director's clock into a ClockSample. Leaves `valid` false when Music
 * Director is not loaded (no free-running fallback: the engine idles). */
void read_clock(SdSource *c, double seconds, cd::ClockSample &out)
{
	(void)seconds; /* frame length no longer matters: sends are deadline-scheduled */
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
	out.phrase_beat = k.phrase_beat;
	/* No frame lookahead: the engine releases a plan up to 50 ms before its send time and the
	 * transport holds it to its deadline (spec 4.5), so sends land on time, not on a frame. */
	out.valid = ok;
}

/* ------------------------------------------------------------------------- */
/* feedback                                                                   */

/* mx held: drain the receiver queue. /scenes/{name} -> confirm tracker + scene_loaded event into
 * the engine; every datagram feeds the 5 s link window (spec 4.6). */
void drain_feedback(SdSource *c, int64_t now_ns)
{
	if (!c->osc || !c->osc->running())
		return;
	std::vector<uint8_t> msg;
	while (c->osc->pop(msg)) {
		director_osc::OscMessage m;
		if (!director_osc::osc_decode(msg, m)) {
			c->confirm.on_datagram(now_ns); // still a datagram: feeds the link window
			continue;
		}
		c->confirm.on_datagram(now_ns);
		if (m.address.rfind("/scenes/", 0) == 0 && m.address.size() > 8) {
			const std::string name = m.address.substr(8);
			for (const cd::ConfirmEvent &ev : c->confirm.on_scene(name, now_ns)) {
				obs_log(LOG_INFO, "[scene-director] %s", ev.text.c_str());
				note(c, "%s", ev.text.c_str());
			}
			/* scene_loaded fires even for an external change (spec 4.6, decision A). */
			cd::QueuedEvent q;
			q.kind = cd::QueuedEvent::Kind::SceneLoaded;
			q.scene = name;
			c->queued.push_back(std::move(q));
		}
	}
}

/* ------------------------------------------------------------------------- */
/* sends                                                                      */

/* mx held: encode a planned send and enqueue it on the transport with its wall-clock deadline in
 * the osc_now_ns() domain (spec 4.5). No socket call happens here: send_at only queues. */
void send_plan(SdSource *c, const cd::PlannedSend &p, int64_t now_ns)
{
	if (!c->osc || !c->osc->running()) {
		note(c, "OSC not started: %s dropped", p.route.c_str());
		return;
	}

	director_osc::OscMessage msg;
	msg.address = p.route;
	std::vector<std::string> args;
	for (const cd::EventArg &a : p.args) {
		if (a.type == cd::EventArg::Type::Int)
			msg.add(a.i);
		else if (a.type == cd::EventArg::Type::Float)
			msg.add(a.f);
		else
			msg.add(a.s);
		args.push_back(arg_text(a));
	}
	std::vector<uint8_t> wire;
	if (!director_osc::osc_encode(msg, wire)) {
		note(c, "encode failed for %s", p.route.c_str());
		return;
	}

	/* The engine released this plan at most 50 ms (the release horizon) before its send time and
	 * stamped the absolute deadline in the osc_now_ns() domain (now_ns is osc_now_ns()); the
	 * transport's sender thread holds the datagram until then (spec 4.5). */
	const uint64_t deadline = p.has_send_at_ns ? (uint64_t)p.send_at_ns : director_osc::osc_now_ns();

	if (!c->osc->send_at(wire.data(), wire.size(), deadline)) {
		c->confirm.set_error(true);
		note(c, "OSC send queue overflow: %s", p.route.c_str());
		obs_log(LOG_WARNING, "[scene-director] send queue overflow: %s", p.route.c_str());
		return;
	}

	/* Arm the confirmation expectation for scene-changing sends (spec 4.6). */
	for (const cd::ConfirmEvent &ev : c->confirm.on_sent(p.route, args, now_ns, c->cfg.confirm_timeout_ms))
		note(c, "%s", ev.text.c_str());

	c->switches++;
	c->last_reason = p.mapping_id;
	c->last_switch_beat = p.target_beat;

	/* Log the send. Section-triggered sends carry seq=<S> beat=<B> (spec 4.6, gate-parser
	 * compatibility with the 2.0 "MIDI Next seq=S beat=B" line). */
	const std::string at = args_text(args);
	std::string line = "OSC " + p.route;
	if (!at.empty())
		line += " " + at;
	if (section_event(p.event)) {
		const int64_t seq = p.key.empty() ? 0 : std::strtoll(p.key.c_str(), nullptr, 10);
		line += " seq=" + std::to_string(seq) +
			" beat=" + std::to_string((long long)std::llround(p.target_beat));
	}
	line += " [" + p.mapping_id + "]";
	obs_log(LOG_INFO, "[scene-director] %s", line.c_str());
	note(c, "%s", line.c_str());
}

/* ------------------------------------------------------------------------- */
/* drive                                                                      */

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
		note(c, "%s", clk.valid ? "following Music Director" : "Music Director not found");
	}

	const int64_t now_ns = (int64_t)director_osc::osc_now_ns();

	drain_feedback(c, now_ns);

	/* expire confirmation deadlines -> "no confirmation" */
	for (const cd::ConfirmEvent &ev : c->confirm.tick(now_ns)) {
		obs_log(LOG_WARNING, "[scene-director] %s", ev.text.c_str());
		note(c, "%s", ev.text.c_str());
	}

	/* a send-queue overflow surfaced since last tick is an error (spec 4.6) */
	if (c->osc && c->osc->running()) {
		const uint64_t d = c->osc->dropped();
		if (d != c->last_dropped) {
			c->last_dropped = d;
			c->confirm.set_error(true);
		}
	}

	director_follow::Sample s;
	s.ok = clk.valid;
	s.beat = clk.beat;
	s.phrase = clk.phrase;
	s.phrase_beat = clk.phrase_beat;
	s.section = (int)clk.section;
	s.section_seq = clk.section_seq;
	s.section_beat = clk.section_beat;
	s.section_forced = clk.section_forced;
	s.epoch = clk.epoch;
	const director_follow::Edges e = c->follower.update(s);

	std::vector<cd::QueuedEvent> queued;
	queued.swap(c->queued);

	std::vector<cd::PlannedSend> plans;
	if (clk.valid)
		plans = c->engine->tick(clk, e, queued, now_ns);
	else if (!queued.empty())
		plans = c->engine->tick(clk, {}, queued, now_ns); /* manual events still send without MD */

	for (const cd::PlannedSend &p : plans) {
		/* exactly-once across every Scene Director source: the first source to claim an event owns
		 * it and sends ALL of its own plans for it (several actions / mappings); other sources skip.
		 * Manual presses and scene_loaded carry an empty key and are always this source's own. */
		if (!c->claims.may_send(g_guard, p.event, p.key)) {
			obs_log(LOG_DEBUG, "'%s': %s key %s owned by another source; skip",
				obs_source_get_name(c->self), p.event.c_str(), p.key.c_str());
			continue;
		}
		send_plan(c, p, now_ns);
	}
}

/* mx held: a manual switch, from the dock or a hotkey */
void manual_locked(SdSource *c, bool prev)
{
	cd::QueuedEvent q;
	q.kind = prev ? cd::QueuedEvent::Kind::ManualPrev : cd::QueuedEvent::Kind::ManualNext;
	c->queued.push_back(std::move(q));
	drive_locked(c, 1.0 / 60.0); /* fire immediately (debounce applies in the engine) */
}

/* ------------------------------------------------------------------------- */
/* settings                                                                   */

void sd_defaults(obs_data_t *s)
{
	/* clock_source is kept in saved data (never read or deleted); Scene Director follows Music
	 * Director unconditionally now. */
	obs_data_set_default_string(s, "clock_source", "");
	obs_data_set_default_int(s, "sync_offset_ms", 0);

	obs_data_set_default_string(s, "osc_host", "127.0.0.1");
	obs_data_set_default_int(s, "osc_send_port", 7000);
	obs_data_set_default_int(s, "osc_feedback_port", 7001);
	obs_data_set_default_int(s, "confirm_timeout_ms", 2000);
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

/* mx held: the migration log lines (spec 4.1), once per source. */
void log_migration(SdSource *c, const cd::SourceConfig &n)
{
	if (c->migration_logged)
		return;
	c->migration_logged = true;
	const char *name = obs_source_get_name(c->self);
	/* the syn_* -> osc_* seeding is persisted and logged by persist_seeded_osc() in create */
	if (n.midi_out_present)
		obs_log(LOG_INFO,
			"[scene-director] '%s': MIDI out was removed in 2.1; Synesthesia is driven over OSC (%s:%d)",
			name, n.osc_host.c_str(), n.osc_send_port);
}

/* mx held: does the transport need a (re)start for the current config? */
bool transport_stale(const SdSource *c)
{
	return !c->osc || !c->osc->running() || c->osc_host != c->cfg.osc_host ||
	       c->osc_send_port != c->cfg.osc_send_port || c->osc_feedback_port != c->cfg.osc_feedback_port;
}

/* mx held: start a fresh transport for the current config. */
void start_transport_locked(SdSource *c)
{
	c->osc = std::make_unique<director_osc::OscTransport>();
	c->last_dropped = 0;
	std::string err;
	if (!c->osc->start(c->cfg.osc_host, c->cfg.osc_send_port, c->cfg.osc_feedback_port, &err)) {
		obs_log(LOG_WARNING, "'%s': OSC transport: %s", obs_source_get_name(c->self), err.c_str());
		note(c, "OSC: %s", err.c_str());
		c->confirm.set_error(true);
		return;
	}
	c->confirm.set_error(false);
	c->osc_host = c->cfg.osc_host;
	c->osc_send_port = c->cfg.osc_send_port;
	c->osc_feedback_port = c->cfg.osc_feedback_port;
	obs_log(LOG_INFO, "'%s': OSC -> %s:%d (feedback %d)", obs_source_get_name(c->self), c->cfg.osc_host.c_str(),
		c->cfg.osc_send_port, c->cfg.osc_feedback_port);
}

/* Applies settings. The transport is (re)started only from create/update (UI thread), never from
 * video_tick (spec 4.5). A running transport is stopped OUTSIDE mx: stop() joins two threads (up to
 * ~250 ms) and must never hold up video_tick, which takes the same mutex. The old transport is
 * stopped before the new one starts so the feedback port is free to bind again; sends planned in
 * that short window are logged as dropped. */
void apply(SdSource *c, obs_data_t *settings)
{
	const cd::SourceConfig n = read_settings(settings);
	std::unique_ptr<director_osc::OscTransport> old;
	{
		std::lock_guard<std::mutex> g(c->mx);
		c->cfg = n;
		c->engine->set_lead_ms(n.lead_ms);
		log_migration(c, n);
		if (!transport_stale(c))
			return;
		old = std::move(c->osc); /* c->osc is null until restarted: drain/send skip it */
	}
	if (old)
		old->stop();
	std::lock_guard<std::mutex> g(c->mx);
	if (transport_stale(c))
		start_transport_locked(c);
}

/* ------------------------------------------------------------------------- */
/* source callbacks                                                           */

const char *sd_name(void *)
{
	return obs_module_text("SceneDirector");
}

/* Migration (spec 4.1): when osc_* are absent and Clip Director 1.1's syn_* are present, the
 * seeded values are written into the source's own settings (only the absent osc_* keys; syn_* stay
 * verbatim), so the properties dialog shows what the transport uses and later edits start from
 * them. Logged once, here. */
void persist_seeded_osc(obs_source_t *source, obs_data_t *settings)
{
	const cd::SourceConfig n = read_settings(settings);
	if (!n.osc_seeded)
		return;
	if (!obs_data_has_user_value(settings, "osc_host"))
		obs_data_set_string(settings, "osc_host", n.osc_host.c_str());
	if (!obs_data_has_user_value(settings, "osc_send_port"))
		obs_data_set_int(settings, "osc_send_port", n.osc_send_port);
	if (!obs_data_has_user_value(settings, "osc_feedback_port"))
		obs_data_set_int(settings, "osc_feedback_port", n.osc_feedback_port);
	obs_log(LOG_INFO, "[scene-director] '%s': OSC ports from Clip Director 1.1 settings: %s %d/%d",
		obs_source_get_name(source), n.osc_host.c_str(), n.osc_send_port, n.osc_feedback_port);
}

void *sd_create(obs_data_t *settings, obs_source_t *source)
{
	SdSource *c = new SdSource();
	c->self = source;
	c->engine = std::make_unique<cd::EventEngine>(cd::EventMap::defaults(), 0.0);
	c->engine->set_release_horizon_ms(50.0); /* spec 4.5: release within 50 ms, deadline-scheduled */
	persist_seeded_osc(source, settings);
	apply(c, settings);

	/* Register and adopt the map under g_map_mx (lock order g_map_mx -> g_sources_mx -> c->mx), so a
	 * reload or a dock toggle can never fall between adopting the map and joining g_sources. The
	 * first source to arrive writes the seeded default map (spec 4.3). */
	{
		std::lock_guard<std::mutex> mg(g_map_mx);
		if (!g_map_loaded)
			map_seed_locked(c->cfg.trigger_breakdown, c->cfg.trigger_drop, c->cfg.cooldown_beats);
		std::lock_guard<std::mutex> sg(g_sources_mx);
		g_sources.push_back(c);
		std::lock_guard<std::mutex> cg(c->mx);
		c->engine->set_map(g_map);
	}
	obs_log(LOG_INFO, "'%s': source ready (renders nothing: it drives Synesthesia over OSC)",
		obs_source_get_name(source));
	return c;
}

void sd_destroy(void *data)
{
	SdSource *c = static_cast<SdSource *>(data);
	{
		std::lock_guard<std::mutex> g(g_sources_mx);
		g_sources.erase(std::remove(g_sources.begin(), g_sources.end(), c), g_sources.end());
	}
	std::unique_ptr<director_osc::OscTransport> osc;
	{
		std::lock_guard<std::mutex> g(c->mx);
		osc = std::move(c->osc);
	}
	if (osc)
		osc->stop(); /* stop the transport before anything else is freed (spec 4.5) */
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

obs_properties_t *sd_properties(void *)
{
	obs_properties_t *pr = obs_properties_create();

	obs_properties_t *ck = obs_properties_create();
	/* clock_source is no longer shown (Scene Director always follows Music Director); the key
	 * stays in the saved settings, it is just not read or deleted. */
	obs_properties_add_int_slider(ck, "sync_offset_ms", obs_module_text("SyncOffset"), -200, 200, 1);
	obs_properties_add_group(pr, "g_clock", obs_module_text("Group.Clock"), OBS_GROUP_NORMAL, ck);

	obs_properties_t *os = obs_properties_create();
	obs_properties_add_text(os, "osc_host", obs_module_text("OscHost"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(os, "osc_send_port", obs_module_text("OscSendPort"), 1, 65535, 1);
	obs_properties_add_int(os, "osc_feedback_port", obs_module_text("OscFeedbackPort"), 0, 65535, 1);
	obs_properties_add_int(os, "confirm_timeout_ms", obs_module_text("ConfirmTimeout"), 100, 10000, 10);
	obs_properties_add_int_slider(os, "syn_lead_ms", obs_module_text("SceneLead"), 0, 500, 10);
	obs_properties_add_group(pr, "g_osc", obs_module_text("Group.Osc"), OBS_GROUP_NORMAL, os);

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
	map_init();
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
	o = Status();
	/* read the map info without holding c->mx, to keep a single lock order (g_map_mx first). */
	{
		std::lock_guard<std::mutex> g(g_map_mx);
		o.map_path = g_map_path;
		o.map_summary = g_map_summary;
		for (const cd::Mapping &m : g_map.mappings()) {
			Status::MappingInfo mi;
			mi.id = m.id;
			mi.event = m.event;
			mi.enabled = m.enabled;
			mi.min_interval_beats = m.min_interval_beats;
			o.mappings.push_back(std::move(mi));
		}
	}
	std::lock_guard<std::mutex> g(c->mx);
	o.clock_following = c->clock_ok;
	o.bpm = c->bpm;
	o.beats = c->beats;
	o.section = (int)c->section;

	const int64_t now_ns = (int64_t)director_osc::osc_now_ns();
	o.link_state = (int)c->confirm.link_state(now_ns);
	o.osc_host = c->cfg.osc_host;
	o.osc_send_port = c->cfg.osc_send_port;
	o.osc_feedback_port = c->cfg.osc_feedback_port;
	o.feedback_bound = c->osc ? c->osc->feedback_bound() : false;
	o.sent = c->osc ? c->osc->sent() : 0;
	o.received = c->osc ? c->osc->received() : 0;
	o.dropped = c->osc ? c->osc->dropped() : 0;

	const cd::ConfirmCounters &cc = c->confirm.counters();
	o.confirmed = cc.confirmed;
	o.missed = cc.missed;
	o.mismatched = cc.mismatched;
	o.external = cc.external;
	o.last_latency_ns = c->confirm.last_latency_ns();
	o.median_latency_ns = c->confirm.median_latency_ns();
	o.p90_latency_ns = c->confirm.p90_latency_ns();

	o.lead_ms = c->cfg.lead_ms;
	o.confirm_timeout_ms = c->cfg.confirm_timeout_ms;
	o.last_switch_beat = c->last_switch_beat;
	o.last_reason = c->last_reason;
	o.switches = c->switches;

	o.log_seq = c->log_seq;
	o.log.assign(c->log.begin(), c->log.end());
	return true;
}

bool reload_map(std::string *error)
{
	std::lock_guard<std::mutex> g(g_map_mx);
	if (map_read_locked()) {
		push_map_to_sources();
		return true;
	}
	if (error)
		*error = "event-map.json not found";
	obs_log(LOG_WARNING, "[scene-director] event-map.json: not found; keeping the previous map");
	return false;
}

bool set_mapping_enabled(const std::string &id, bool enabled)
{
	std::lock_guard<std::mutex> g(g_map_mx);
	if (!g_map.set_enabled(id, enabled))
		return false;
	map_write_locked();
	push_map_to_sources();
	return true;
}

bool set_min_interval(const std::string &id, int beats)
{
	std::lock_guard<std::mutex> g(g_map_mx);
	if (!g_map.set_min_interval_beats(id, beats))
		return false;
	map_write_locked();
	push_map_to_sources();
	return true;
}

void set_scene_lead(obs_source_t *src, double lead_ms)
{
	write_settings(src,
		       [&](obs_data_t *s) { obs_data_set_double(s, "syn_lead_ms", std::clamp(lead_ms, 0.0, 500.0)); });
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
