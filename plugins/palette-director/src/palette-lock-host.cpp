/*
Palette Director
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

/*
Per-instance runtime for the Palette Lock filter (spec 4, Palette Director follower).

   worker thread : ~every 5 ms reads director_clock_read() (Music Director is the suite clock),
                   runs the sample through pd::PaletteDirector (director_follow::Follower ->
                   pd::Scheduler in PD 1.2.0's order), drains commands, runs the lock-mix fade,
                   publishes snapshots, logs director decisions
   render thread : pd_host_render_state() reads a lock-free triple buffer
   hotkey/UI     : pd_host_command() -> SPSC command queues drained by the worker

Palette Director no longer captures or analyses audio (that moved to Music Director). When Music
Director is not loaded, director_clock_read() returns false and the director idles: no crash, one
log line, palettes hold and the filter keeps rendering the live palette.
*/

#include "palette-lock-host.h"
#include "palette-lock-host.hpp"

#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "director-clock.h"
#include "director/palette-director.h"

namespace {

struct HostCmd {
	int cmd = 0;
	int idx = -1;
	int origin = 0;
};

// Commands: single consumer (worker), lock-free on the consumer side. Producers (hotkey
// thread, UI thread, dock) are serialised by a small producer-side mutex, so the ring is
// used strictly single-producer/single-consumer. Never touched by render/audio threads.
class CmdQueue {
public:
	bool push(const HostCmd &c)
	{
		std::lock_guard<std::mutex> g(prod_);
		const size_t t = tail_.load(std::memory_order_relaxed);
		const size_t h = head_.load(std::memory_order_acquire);
		if (t - h >= kCap)
			return false;
		buf_[t % kCap] = c;
		tail_.store(t + 1, std::memory_order_release);
		return true;
	}
	bool pop(HostCmd &c)
	{
		const size_t h = head_.load(std::memory_order_relaxed);
		const size_t t = tail_.load(std::memory_order_acquire);
		if (h == t)
			return false;
		c = buf_[h % kCap];
		head_.store(h + 1, std::memory_order_release);
		return true;
	}

private:
	static constexpr size_t kCap = 256;
	HostCmd buf_[kCap];
	std::mutex prod_;
	std::atomic<size_t> head_{0};
	std::atomic<size_t> tail_{0};
};

struct Config {
	unsigned long long gen = 0;
	struct pl_set set;
	int force_live = -1;
};

const char *section_name(pd::Section s)
{
	switch (s) {
	case pd::Section::Groove:
		return "GROOVE";
	case pd::Section::Breakdown:
		return "BREAKDOWN";
	case pd::Section::Drop:
		return "DROP";
	}
	return "?";
}

const char *cmd_name(int c)
{
	switch (c) {
	case PD_CMD_FORCE_ON:
		return "force on (toggle)";
	case PD_CMD_FORCE_OFF:
		return "force off (pressed)";
	case PD_CMD_RELEASE_FORCE:
		return "force off (released)";
	case PD_CMD_PAUSE:
		return "pause";
	case PD_CMD_RESUME:
		return "resume";
	case PD_CMD_NEXT:
		return "next palette";
	case PD_CMD_PREV:
		return "previous palette";
	case PD_CMD_GO_LIVE:
		return "palette go live";
	case PD_CMD_ROTATE_NOW:
		return "rotate colours now";
	case PD_CMD_PANIC:
		return "PANIC (toggle)";
	case PD_CMD_TOGGLE_PAUSE:
		return "pause/resume";
	}
	return "?";
}

pd::SetFile to_setfile(const struct pl_set &s)
{
	pd::SetFile f;
	f.name = s.name;
	f.synesthesia_playlist = s.synesthesia_playlist;
	for (int i = 0; i < s.n_palettes; i++) {
		const struct pl_palette &p = s.palettes[i];
		pd::PaletteSpec ps;
		ps.name = p.name;
		ps.mode = p.mode == PL_MODE_DUOTONE ? pd::RecolourMode::Duotone : pd::RecolourMode::GradientMap;
		for (int j = 0; j < p.n_colours; j++)
			ps.colours.push_back(pd::Colour{p.colours[j].r, p.colours[j].g, p.colours[j].b});
		f.palettes.push_back(ps);
	}
	f.colour.strength = s.strength;
	f.colour.contrast = s.contrast;
	f.colour.keep_brightness = s.preserve;
	switch (s.trigger) {
	case PL_TRIGGER_PHRASES:
		f.palette_changes.trigger = pd::ChangeTrigger::Phrases;
		break;
	case PL_TRIGGER_RANDOM:
		f.palette_changes.trigger = pd::ChangeTrigger::Random;
		break;
	case PL_TRIGGER_MANUAL:
		f.palette_changes.trigger = pd::ChangeTrigger::Manual;
		break;
	default:
		f.palette_changes.trigger = pd::ChangeTrigger::Drop;
		break;
	}
	f.palette_changes.every_phrases = std::max(1, s.every_phrases);
	f.palette_changes.chance = s.change_chance;
	f.palette_changes.order = s.order == PL_ORDER_SHUFFLE ? pd::ChangeOrder::Shuffle : pd::ChangeOrder::Sequence;
	f.palette_changes.crossfade_beats = s.crossfade_beats;
	f.palette_changes.breakdown_palette = s.breakdown;
	f.director.enabled = s.dir_enabled;
	f.director.phrase_beats = s.phrase_beats == 16 ? 16 : 32;
	f.director.fade_beats = s.lock_fade_beats;
	f.director.fallback_bpm = s.fallback_bpm;
	f.director.random_off = s.random_off;
	f.director.random_chance = s.random_chance;
	f.director.random_bars_min = s.bars_min;
	f.director.random_bars_max = s.bars_max;
	f.director.cooldown_phrases = s.cooldown_phrases;
	f.director.off_on_breakdown = s.off_on_breakdown;
	f.director.off_on_drop = s.off_on_drop;
	f.director.drop_raw_bars = s.drop_bars;
	f.director.breathing = s.breathing;
	f.director.rotation = s.rotation;
	f.director.seed = s.seed;
	/* retired audio keys: carried through so nothing is lost (spec 4.1) */
	f.director.input_gain_db = s.input_gain_db;
	f.director.kick_sensitivity = s.kick_sensitivity;
	f.director.drop_cycle = s.drop_cycle;
	f.director.drop_cycle_chance = s.drop_cycle_chance;
	f.director.drop_cycle_bars = s.drop_cycle_bars;
	f.director.drop_cycle_steps_per_beat = s.drop_cycle_steps_per_beat;
	return f;
}

} // namespace

/* ------------------------------------------------------------------------- */

struct pd_host {
	obs_source_t *filter = nullptr;
	uint64_t id = 0;

	std::atomic<Config *> pending{nullptr};
	unsigned long long layout_gen = 0; // pd_host_update caller only
	struct pl_set last_layout;         // pd_host_update caller only
	bool have_layout = false;
	std::atomic<bool> attached{true};
	std::atomic<bool> stop{false};
	std::atomic<int> live{-1};
	CmdQueue cmds;

	/* render triple buffer */
	pd_render_state slots[3] = {};
	std::atomic<int> middle{1};
	int back = 0;  // worker only
	int front = 2; // render only
	std::atomic<bool> published{false};

	/* drop colour cycle signal (spec 4.4): palette_director_drop_cycle reads these */
	std::atomic<bool> drop_active{false};
	std::atomic<int> drop_steps{2};

	/* dock view (worker writes, dock reads; never the render/audio threads) */
	std::mutex view_mx;
	pd::DirectorState v_st;
	float v_mix = 1.0f;
	double v_beats = 0.0;
	struct pl_set v_set;
	bool v_md_found = false;
	float v_clk_bpm = 0.0f;
	int v_clk_section = 0;
	std::deque<pdhost::LogLine> v_log;
	uint64_t v_log_seq = 0;
	std::deque<pdhost::TimelineSample> v_tl;

	/* worker-only state */
	std::unique_ptr<pd::PaletteDirector> dir;
	struct pl_set set;
	bool md_found_prev = false;

	std::thread worker;
};

static void publish_render(pd_host *h, const pd_render_state &rs)
{
	h->slots[h->back] = rs;
	h->back = h->middle.exchange(h->back | 4, std::memory_order_acq_rel) & 3;
	h->published.store(true, std::memory_order_release);
}

static void push_log(pd_host *h, uint64_t phrase, const std::string &text)
{
	pdhost::LogLine l;
	l.seq = ++h->v_log_seq;
	l.phrase = phrase;
	l.text = text;
	h->v_log.push_back(l);
	while (h->v_log.size() > 200)
		h->v_log.pop_front();
}

static void worker_main(pd_host *h)
{
	using clock = std::chrono::steady_clock;
	const auto t0 = clock::now();
	auto last = t0;
	double last_status = 0.0;
	double tl_last_beat = -1.0;
	float mix = 1.0f;
	unsigned long long applied_gen = 0;
	int log_tokens = 20;
	double token_t = 0.0;
	std::vector<pd::Event> evs;

	while (!h->stop.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		const auto tnow = clock::now();
		const double now = std::chrono::duration<double>(tnow - t0).count();
		const double dt = std::min(0.1, std::chrono::duration<double>(tnow - last).count());
		last = tnow;

		/* 1. settings */
		if (Config *c = h->pending.exchange(nullptr)) {
			const pd::SetFile sf = to_setfile(c->set);
			if (!h->dir) {
				h->dir.reset(new pd::PaletteDirector(sf));
				h->dir->set_keep_history(false);
				std::vector<pd::Event> tmp;
				h->dir->drain_events(tmp); // enable streaming
				if (c->force_live >= 0)
					h->dir->update_set(sf, c->force_live);
			} else {
				h->dir->update_set(sf, c->force_live);
			}
			h->set = c->set;
			applied_gen = c->gen;
			delete c;
		}
		if (!h->dir)
			continue;

		/* 2. commands (palette commands only; the clock commands moved to Music Director) */
		HostCmd hc;
		while (h->cmds.pop(hc)) {
			const pd::DirectorState cur = h->dir->snapshot();
			int c = hc.cmd;
			if (c == PD_CMD_TOGGLE_PAUSE)
				c = cur.paused ? PD_CMD_RESUME : PD_CMD_PAUSE;
			pd::Command dc = pd::Command::ForceOn;
			switch (c) {
			case PD_CMD_FORCE_ON:
				dc = pd::Command::ForceOn;
				break;
			case PD_CMD_FORCE_OFF:
				dc = pd::Command::ForceOff;
				break;
			case PD_CMD_RELEASE_FORCE:
				dc = pd::Command::ReleaseForce;
				break;
			case PD_CMD_PAUSE:
				dc = pd::Command::Pause;
				break;
			case PD_CMD_RESUME:
				dc = pd::Command::Resume;
				break;
			case PD_CMD_NEXT:
				dc = pd::Command::Next;
				break;
			case PD_CMD_PREV:
				dc = pd::Command::Prev;
				break;
			case PD_CMD_GO_LIVE:
				dc = pd::Command::GoLive;
				break;
			case PD_CMD_ROTATE_NOW:
				dc = pd::Command::RotateNow;
				break;
			case PD_CMD_PANIC:
				dc = pd::Command::Panic;
				break;
			default:
				continue;
			}
			char line[160];
			if (c == PD_CMD_GO_LIVE)
				snprintf(line, sizeof(line), "%s: %s %d",
					 hc.origin == PD_ORIGIN_HOTKEY ? "hotkey" : "dock", cmd_name(c), hc.idx + 1);
			else
				snprintf(line, sizeof(line), "%s: %s",
					 hc.origin == PD_ORIGIN_HOTKEY ? "hotkey" : "dock", cmd_name(c));
			blog(LOG_INFO, "[palette-director] '%s': %s", obs_source_get_name(h->filter), line);
			h->dir->push_command(dc, hc.idx);
		}

		/* 3. read the clock (Music Director is the suite clock) and drive the follower core */
		struct director_clock k;
		const bool ok = director_clock_read(&k);
		if (ok != h->md_found_prev) {
			h->md_found_prev = ok;
			if (!ok)
				blog(LOG_INFO,
				     "[palette-director] Music Director not found: palettes hold, director idle");
			else
				blog(LOG_INFO, "[palette-director] Music Director found: director resuming");
		}
		pd::ClockSample cs;
		cs.ok = ok;
		cs.beat = k.beat;
		cs.phrase = k.phrase;
		cs.section = k.section;
		cs.section_seq = k.section_seq;
		cs.section_beat = k.section_beat;
		cs.epoch = k.epoch;
		cs.phrase_beats = k.phrase_beats;
		cs.beats_now = k.beats_now;
		cs.bpm = k.bpm;
		cs.confidence = k.confidence;
		cs.bass = k.bass;
		cs.silent = ok ? k.silent : true;
		cs.paused = k.paused;
		cs.now_ns = k.now_ns;
		h->dir->on_clock(cs);

		/* 4. snapshot, lock-mix fade (mock: rate = (bpm/60)/max(0.25, fade)) */
		const pd::DirectorState st = h->dir->snapshot();
		const float target = st.mix_target;
		const float bpm = st.bpm > 1.0f ? st.bpm : h->set.fallback_bpm;
		const float rate = (bpm / 60.0f) / std::max(0.25f, h->set.lock_fade_beats);
		const float diff = target - mix;
		const float stepv = std::min(std::fabs(diff), rate * static_cast<float>(dt));
		mix += diff > 0 ? stepv : -stepv;
		h->live.store(st.live_palette);
		h->drop_active.store(st.drop_cycle);
		h->drop_steps.store(h->set.drop_cycle_steps_per_beat);

		pd_render_state rs;
		rs.gen = applied_gen;
		rs.live = st.live_palette;
		rs.prev = st.prev_palette;
		rs.pfade = st.pfade;
		rs.rotation = st.rotation_offset;
		rs.mix = mix;
		rs.bass = st.bass;
		rs.silent = st.silent ? 1 : 0;
		publish_render(h, rs);

		/* 5. decision log (rate-limited LOG_INFO) + dock view */
		evs.clear();
		h->dir->drain_events(evs);
		token_t += dt;
		if (token_t >= 0.1) {
			log_tokens = std::min(20, log_tokens + static_cast<int>(token_t * 10));
			token_t = 0.0;
		}
		const double beats_now = static_cast<double>(st.beat) + st.beat_frac;
		{
			std::lock_guard<std::mutex> g(h->view_mx);
			for (const auto &e : evs) {
				push_log(h, e.phrase, e.detail);
				if (log_tokens > 0) {
					log_tokens--;
					blog(LOG_INFO, "[palette-director] '%s': %s (beat %llu, phrase %llu)",
					     obs_source_get_name(h->filter), e.detail.c_str(),
					     (unsigned long long)e.beat, (unsigned long long)e.phrase);
				}
			}
			h->v_st = st;
			h->v_mix = mix;
			h->v_beats = beats_now;
			h->v_set = h->set;
			h->v_md_found = ok;
			h->v_clk_bpm = static_cast<float>(bpm);
			h->v_clk_section = static_cast<int>(st.section);
			if (tl_last_beat < 0 || beats_now < tl_last_beat || beats_now - tl_last_beat >= 0.125) {
				tl_last_beat = beats_now;
				pdhost::TimelineSample s;
				s.beat = beats_now;
				s.mix = mix;
				s.section = static_cast<int>(st.section);
				s.bass = st.bass;
				if (st.live_palette >= 0 && st.live_palette < h->set.n_palettes) {
					const struct pl_palette &p = h->set.palettes[st.live_palette];
					const struct pl_colour &cc = p.colours[std::min(2, p.n_colours - 1)];
					s.r = cc.r;
					s.g = cc.g;
					s.b = cc.b;
				}
				h->v_tl.push_back(s);
			}
			while (!h->v_tl.empty() &&
			       (beats_now - h->v_tl.front().beat > 64.0 || h->v_tl.front().beat > beats_now))
				h->v_tl.pop_front();
		}

		if (now - last_status >= 4.0) {
			last_status = now;
			const char *lname = (st.live_palette >= 0 && st.live_palette < h->set.n_palettes)
						    ? h->set.palettes[st.live_palette].name
						    : "?";
			blog(LOG_INFO,
			     "[palette-director] '%s': status bpm %.1f %s beat %llu phrase %llu live '%s' mix %.2f%s",
			     obs_source_get_name(h->filter), bpm, section_name(st.section), (unsigned long long)st.beat,
			     (unsigned long long)st.phrase, lname, mix, ok ? "" : " (Music Director absent: idle)");
		}
	}
}

/* ------------------------------------------------------------------------- */
/* C API                                                                      */

/* ---- registry ---- */
static std::mutex g_reg_mx;
static std::map<uint64_t, pd_host *> g_reg;
static uint64_t g_next_id = 1;

extern "C" struct pd_host *pd_host_create(obs_source_t *filter)
{
	pd_host *h = new pd_host();
	h->filter = filter;
	pl_set_default(&h->set);
	pl_set_default(&h->v_set);
	{
		std::lock_guard<std::mutex> g(g_reg_mx);
		h->id = g_next_id++;
		g_reg[h->id] = h;
	}
	h->worker = std::thread(worker_main, h);
	return h;
}

extern "C" void pd_host_destroy(struct pd_host *h)
{
	if (!h)
		return;
	{
		std::lock_guard<std::mutex> g(g_reg_mx);
		g_reg.erase(h->id);
	}
	h->stop.store(true);
	if (h->worker.joinable())
		h->worker.join();
	delete h->pending.exchange(nullptr);
	delete h;
}

extern "C" unsigned long long pd_host_update(struct pd_host *h, const struct pl_set *set, int force_live)
{
	bool same = h->have_layout && h->last_layout.n_palettes == set->n_palettes;
	for (int i = 0; same && i < set->n_palettes; i++)
		same = strcmp(h->last_layout.palettes[i].name, set->palettes[i].name) == 0;
	if (!same || force_live >= 0)
		h->layout_gen++;
	h->last_layout = *set;
	h->have_layout = true;

	Config *c = new Config();
	c->gen = h->layout_gen;
	c->set = *set;
	c->force_live = force_live;
	Config *old = h->pending.exchange(c);
	if (old) {
		/* not consumed yet: keep a pending forced live palette */
		if (c->force_live < 0)
			c->force_live = old->force_live;
		delete old;
	}
	return c->gen;
}

extern "C" void pd_host_set_attached(struct pd_host *h, bool attached)
{
	h->attached.store(attached);
}

extern "C" void pd_host_command(struct pd_host *h, int cmd, int idx, int origin)
{
	HostCmd c;
	c.cmd = cmd;
	c.idx = idx;
	c.origin = origin;
	h->cmds.push(c);
}

extern "C" bool pd_host_render_state(struct pd_host *h, struct pd_render_state *out)
{
	if (!h->published.load(std::memory_order_acquire))
		return false;
	if (h->middle.load(std::memory_order_acquire) & 4)
		h->front = h->middle.exchange(h->front, std::memory_order_acq_rel) & 3;
	*out = h->slots[h->front];
	return true;
}

extern "C" int pd_host_live_palette(struct pd_host *h)
{
	return h->live.load();
}

/* ------------------------------------------------------------------------- */
/* dock API                                                                   */

namespace pdhost {

std::vector<InstanceInfo> list_instances()
{
	std::vector<InstanceInfo> out;
	std::lock_guard<std::mutex> g(g_reg_mx);
	for (const auto &kv : g_reg) {
		InstanceInfo ii;
		ii.id = kv.first;
		const char *fn = obs_source_get_name(kv.second->filter);
		ii.filter_name = fn ? fn : "";
		obs_source_t *parent = obs_filter_get_parent(kv.second->filter);
		const char *pn = parent ? obs_source_get_name(parent) : nullptr;
		ii.parent_name = pn ? pn : "";
		out.push_back(ii);
	}
	return out;
}

bool read_view(uint64_t id, DockView &out, uint64_t log_since)
{
	std::lock_guard<std::mutex> g(g_reg_mx);
	auto it = g_reg.find(id);
	if (it == g_reg.end())
		return false;
	pd_host *h = it->second;
	std::lock_guard<std::mutex> g2(h->view_mx);
	out.st = h->v_st;
	out.mix = h->v_mix;
	out.beats = h->v_beats;
	out.set = h->v_set;
	out.md_found = h->v_md_found;
	out.clk_bpm = h->v_clk_bpm;
	out.clk_section = h->v_clk_section;
	out.log.clear();
	for (const auto &l : h->v_log)
		if (l.seq > log_since)
			out.log.push_back(l);
	out.timeline.assign(h->v_tl.begin(), h->v_tl.end());
	return true;
}

bool command(uint64_t id, int cmd, int idx)
{
	std::lock_guard<std::mutex> g(g_reg_mx);
	auto it = g_reg.find(id);
	if (it == g_reg.end())
		return false;
	pd_host_command(it->second, cmd, idx, PD_ORIGIN_UI);
	return true;
}

obs_source_t *filter_source(uint64_t id)
{
	std::lock_guard<std::mutex> g(g_reg_mx);
	auto it = g_reg.find(id);
	if (it == g_reg.end())
		return nullptr;
	return obs_source_get_ref(it->second->filter);
}

uint64_t id_for_filter(obs_source_t *filter)
{
	std::lock_guard<std::mutex> g(g_reg_mx);
	for (const auto &kv : g_reg)
		if (kv.second->filter == filter)
			return kv.first;
	return 0;
}

} // namespace pdhost

/* ------------------------------------------------------------------------- */
/* drop colour cycle signal (spec 4.4; not a clock)                           */

static void proc_drop_cycle(void *, calldata_t *cd)
{
	bool active = false;
	int steps = 2;
	{
		std::lock_guard<std::mutex> g(g_reg_mx);
		for (const auto &kv : g_reg) {
			if (kv.second->drop_active.load()) {
				active = true;
				steps = kv.second->drop_steps.load();
				break;
			}
		}
	}
	calldata_set_bool(cd, "active", active);
	calldata_set_int(cd, "steps_per_beat", steps);
}

extern "C" void pd_drop_cycle_api_register(void)
{
	proc_handler_t *ph = obs_get_proc_handler();
	if (!ph) {
		blog(LOG_WARNING, "[palette-director] drop cycle signal: no global proc handler");
		return;
	}
	proc_handler_add(ph, "void palette_director_drop_cycle(out bool active, out int steps_per_beat)",
			 proc_drop_cycle, nullptr);
	blog(LOG_INFO, "[palette-director] drop cycle signal registered (palette_director_drop_cycle)");
}
