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

#include "host/music-clock-host.h"

#include <media-io/audio-io.h>
#include <util/platform.h>
#include <util/util_uint64.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <plugin-support.h>

#include "director/clock_api.h"
#include "director/migration.h"
#include "obs/md-hotkeys.h"

#if defined(ENABLE_FRONTEND_API)
#include <obs-frontend-api.h>
#endif

#include "director-clock.h"

namespace {

/* ------------------------------------------------------------------------- */
/* lock-free SPSC float ring (audio thread -> worker)                         */

class FloatRing {
public:
	static constexpr size_t kCap = 1u << 17; // ~2.7 s at 48 kHz mono

	size_t write(const float *src, size_t n)
	{
		const size_t w = w_.load(std::memory_order_relaxed);
		const size_t r = r_.load(std::memory_order_acquire);
		const size_t room = kCap - (w - r);
		n = std::min(n, room);
		for (size_t i = 0; i < n; i++)
			buf_[(w + i) & (kCap - 1)] = src[i];
		w_.store(w + n, std::memory_order_release);
		return n;
	}

	size_t read(float *dst, size_t n)
	{
		const size_t r = r_.load(std::memory_order_relaxed);
		const size_t w = w_.load(std::memory_order_acquire);
		n = std::min(n, w - r);
		for (size_t i = 0; i < n; i++)
			dst[i] = buf_[(r + i) & (kCap - 1)];
		r_.store(r + n, std::memory_order_release);
		return n;
	}

	void clear() { r_.store(w_.load(std::memory_order_acquire), std::memory_order_release); }
	/* absolute (monotonic) sample positions, for the audio-timestamp mapping */
	size_t write_pos() const { return w_.load(std::memory_order_acquire); }
	size_t read_pos() const { return r_.load(std::memory_order_acquire); }

private:
	std::vector<float> buf_ = std::vector<float>(kCap);
	std::atomic<size_t> w_{0};
	std::atomic<size_t> r_{0};
};

/* ------------------------------------------------------------------------- */

struct HostCmd {
	mdhost::Cmd cmd = mdhost::Cmd::PhraseNow;
};

// Commands: single consumer (worker), lock-free on the consumer side. Producers are serialised by a
// small producer-side mutex, so the ring is strictly single-producer/single-consumer.
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

const char *section_name(md::Section s)
{
	switch (s) {
	case md::Section::Groove:
		return "GROOVE";
	case md::Section::Breakdown:
		return "BREAKDOWN";
	case md::Section::Drop:
		return "DROP";
	}
	return "?";
}

struct Config {
	mdhost::Settings settings;
};

// What the director_clock proc reads. One short mutex, never blocks on audio.
struct Snap {
	bool valid = false;
	md::ClockState st;
	double beats = 0.0; // beat position at the last analysed sample
	float bpm = 0.0f;   // tempo the grid runs at
	uint64_t ts = 0;    // wall-clock time `beats` refers to
	int ts_source = 0;  // 1 = from audio timestamps, 0 = wall clock
};

struct Host {
	FloatRing ring;
	std::atomic<uint32_t> channels{2};
	std::atomic<uint64_t> audio_frames{0};
	/* audio timestamp marker (seqlock; audio thread writes, worker reads) */
	std::atomic<uint32_t> ats_seq{0};
	std::atomic<uint64_t> ats_w{0};
	std::atomic<uint64_t> ats_ts{0};

	std::atomic<Config *> pending{nullptr};
	std::atomic<bool> stop{false};
	CmdQueue cmds;

	/* published snapshot (proc) */
	std::mutex snap_mx;
	Snap snap;

	/* dock view (worker writes, dock reads) */
	std::mutex view_mx;
	mdhost::View view;

	/* worker only */
	std::unique_ptr<md::MusicClock> clock;
	mdhost::Settings settings;
	std::string audio_name;
	obs_weak_source_t *audio_weak = nullptr;
	double last_audio_try = -10.0;
	md::AudioClockMap clk_map;

	std::thread worker;
};

Host *g_host = nullptr;
std::string g_migration_note; // "<filter> on <source>" after a migration, else empty

#if defined(ENABLE_FRONTEND_API)
obs_data_t *g_collection = nullptr; // strong ref to the current scene collection's data
#endif

/* ------------------------------------------------------------------------- */
/* audio capture (worker owns attach/detach)                                  */

void on_audio(void *param, obs_source_t *source, const struct audio_data *audio, bool muted)
{
	(void)source;
	(void)muted; // analyse the feed even if it is muted in the OBS mixer
	Host *h = static_cast<Host *>(param);
	const uint32_t ch = std::max<uint32_t>(1, std::min<uint32_t>(h->channels.load(std::memory_order_relaxed), 8));
	float mono[1024];
	uint32_t done = 0;
	while (done < audio->frames) {
		const uint32_t n = std::min<uint32_t>(1024, audio->frames - done);
		for (uint32_t i = 0; i < n; i++) {
			float acc = 0.0f;
			uint32_t used = 0;
			for (uint32_t c = 0; c < ch; c++) {
				if (!audio->data[c])
					continue;
				acc += reinterpret_cast<const float *>(audio->data[c])[done + i];
				used++;
			}
			mono[i] = used ? acc / static_cast<float>(used) : 0.0f;
		}
		h->ring.write(mono, n); // never blocks; drops on overflow
		done += n;
	}
	h->audio_frames.fetch_add(audio->frames, std::memory_order_relaxed);

	/* publish where the written samples sit on OBS's clock */
	uint32_t sr = audio_output_get_sample_rate(obs_get_audio());
	if (sr == 0)
		sr = 48000;
	const uint64_t end_ts = audio->timestamp + util_mul_div64(audio->frames, 1000000000ULL, sr);
	const uint32_t seq = h->ats_seq.load(std::memory_order_relaxed);
	h->ats_seq.store(seq + 1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);
	h->ats_w.store(h->ring.write_pos(), std::memory_order_relaxed);
	h->ats_ts.store(end_ts, std::memory_order_relaxed);
	h->ats_seq.store(seq + 2, std::memory_order_release);
}

/* OBS timestamp of ring read position `r` (worker thread). false if no marker yet. */
bool audio_ts_at(Host *h, size_t r, uint32_t sr, uint64_t *out)
{
	uint64_t w = 0, ts = 0;
	for (int tries = 0; tries < 8; ++tries) {
		const uint32_t s1 = h->ats_seq.load(std::memory_order_acquire);
		if (s1 & 1u)
			continue;
		w = h->ats_w.load(std::memory_order_relaxed);
		ts = h->ats_ts.load(std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_acquire);
		if (h->ats_seq.load(std::memory_order_relaxed) == s1) {
			if (ts == 0)
				return false;
			const int64_t diff = static_cast<int64_t>(r) - static_cast<int64_t>(w);
			*out = static_cast<uint64_t>(static_cast<int64_t>(ts) +
						     diff * 1000000000LL / static_cast<int64_t>(sr));
			return true;
		}
	}
	return false;
}

void detach_audio(Host *h)
{
	if (!h->audio_weak)
		return;
	obs_source_t *src = obs_weak_source_get_source(h->audio_weak);
	if (src) {
		obs_source_remove_audio_capture_callback(src, on_audio, h);
		obs_log(LOG_INFO, "[music-director] audio capture detached from '%s'", obs_source_get_name(src));
		obs_source_release(src);
	}
	obs_weak_source_release(h->audio_weak);
	h->audio_weak = nullptr;
	h->ring.clear();
}

void maintain_audio(Host *h, double now)
{
	const bool want = !h->settings.audio_source.empty();
	if (h->audio_weak) {
		obs_source_t *src = obs_weak_source_get_source(h->audio_weak);
		bool keep = want && src && !obs_source_removed(src) &&
			    h->settings.audio_source == (obs_source_get_name(src) ? obs_source_get_name(src) : "");
		if (src)
			obs_source_release(src);
		if (keep)
			return;
		detach_audio(h);
	}
	if (!want || now - h->last_audio_try < 1.0)
		return;
	h->last_audio_try = now;
	obs_source_t *src = obs_get_source_by_name(h->settings.audio_source.c_str());
	if (!src)
		return;
	if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) == 0 || obs_source_removed(src)) {
		obs_source_release(src);
		return;
	}
	const struct audio_output_info *ai = audio_output_get_info(obs_get_audio());
	if (ai)
		h->channels.store(get_audio_channels(ai->speakers));
	h->ring.clear();
	h->audio_weak = obs_source_get_weak_source(src);
	if (h->clock)
		h->clock->reset_audio_reference();
	obs_source_add_audio_capture_callback(src, on_audio, h);
	obs_log(LOG_INFO, "[music-director] audio capture attached to '%s' (%u Hz, %u ch)",
		h->settings.audio_source.c_str(), audio_output_get_sample_rate(obs_get_audio()), h->channels.load());
	obs_source_release(src);
}

/* ------------------------------------------------------------------------- */
/* worker                                                                     */

void publish_snap(Host *h, const md::ClockState &st, double beats, float bpm, uint64_t ts, int ts_source, bool valid)
{
	std::lock_guard<std::mutex> g(h->snap_mx);
	h->snap.valid = valid;
	h->snap.st = st;
	h->snap.beats = beats;
	h->snap.bpm = bpm;
	h->snap.ts = ts;
	h->snap.ts_source = ts_source;
}

void worker_main(Host *h)
{
	using clock = std::chrono::steady_clock;
	const auto t0 = clock::now();
	auto last = t0;
	double last_audio_seen = -10.0;
	uint64_t last_frames = 0;
	double last_status = 0.0;
	double tl_last_beat = -1.0;
	int log_tokens = 20;
	double token_t = 0.0;
	std::vector<md::ClockEvent> evs;
	std::vector<float> buf(4096);

	h->clock.reset(new md::MusicClock());
	h->clock->set_keep_history(false);
	{
		std::vector<md::ClockEvent> tmp;
		h->clock->drain_events(tmp); // enable streaming
	}

	while (!h->stop.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		const auto tnow = clock::now();
		const double now = std::chrono::duration<double>(tnow - t0).count();
		const double dt = std::min(0.1, std::chrono::duration<double>(tnow - last).count());
		last = tnow;

		/* 1. settings */
		if (Config *c = h->pending.exchange(nullptr)) {
			const bool audio_changed = c->settings.audio_source != h->settings.audio_source;
			h->settings = c->settings;
			md::ClockConfig cc;
			cc.phrase_beats = h->settings.phrase_beats;
			cc.fallback_bpm = h->settings.fallback_bpm;
			cc.input_gain_db = h->settings.input_gain_db;
			cc.kick_sensitivity = h->settings.kick_sensitivity;
			cc.enabled = h->settings.enabled;
			h->clock->apply_config(cc);
			if (audio_changed) {
				h->last_audio_try = -10.0;
				h->clock->reset_audio_reference(); // a new source starts with a fresh reference
			}
			delete c;
		}

		/* 2. audio source lifecycle */
		maintain_audio(h, now);

		/* 3. commands */
		HostCmd hc;
		while (h->cmds.pop(hc)) {
			md::Command dc = md::Command::PhraseNow;
			switch (hc.cmd) {
			case mdhost::Cmd::PhraseNow:
				dc = md::Command::PhraseNow;
				break;
			case mdhost::Cmd::DropNow:
				dc = md::Command::DropNow;
				break;
			case mdhost::Cmd::ForceBreakdown:
				dc = md::Command::ForceBreakdown;
				break;
			case mdhost::Cmd::TapTempo:
				dc = md::Command::TapTempo;
				break;
			case mdhost::Cmd::LockBpm:
				dc = md::Command::LockBpm;
				break;
			case mdhost::Cmd::NudgeBack:
				dc = md::Command::NudgeBack;
				break;
			case mdhost::Cmd::NudgeFwd:
				dc = md::Command::NudgeFwd;
				break;
			}
			h->clock->push_command(dc);
		}

		/* 4. audio -> clock (real feed, or real-time silence when there is none) */
		uint32_t sr = audio_output_get_sample_rate(obs_get_audio());
		if (sr == 0)
			sr = 48000;
		size_t got_total = 0;
		bool synthetic = false;
		if (h->settings.enabled) {
			for (;;) {
				const size_t got = h->ring.read(buf.data(), buf.size());
				if (!got)
					break;
				h->clock->push_audio(buf.data(), got, sr);
				got_total += got;
			}
			const uint64_t frames = h->audio_frames.load(std::memory_order_relaxed);
			if (frames != last_frames) {
				last_frames = frames;
				last_audio_seen = now;
			}
			if (!got_total && now - last_audio_seen > 0.25) {
				/* no audio source / feed stopped: the clock keeps running at the fallback BPM */
				size_t n = static_cast<size_t>(dt * sr);
				std::fill(buf.begin(), buf.end(), 0.0f);
				synthetic = true;
				while (n) {
					const size_t k = std::min(n, buf.size());
					h->clock->push_audio(buf.data(), k, sr);
					n -= k;
				}
			}
		}

		/* 5. snapshot */
		const md::ClockState st = h->clock->snapshot();
		const float bpm = st.bpm > 1.0f ? st.bpm : h->settings.fallback_bpm;
		const double beats_now = static_cast<double>(st.beat) + st.beat_frac;
		uint64_t ats_wall = 0;
		int ts_source = 0;
		{
			uint64_t ats = 0;
			const uint64_t wall = os_gettime_ns();
			if (got_total && audio_ts_at(h, h->ring.read_pos(), sr, &ats)) {
				ats_wall = h->clk_map.observe(ats, wall);
				ts_source = 1;
			} else if (got_total || synthetic) {
				h->clk_map.reset();
				ats_wall = wall;
				ts_source = 0;
			} else {
				ats_wall = h->snap.ts ? h->snap.ts : wall;
				ts_source = h->snap.ts_source;
			}
		}
		publish_snap(h, st, beats_now, bpm, ats_wall, ts_source, true);

		/* 6. section log (spec 2: one line per section change) */
		evs.clear();
		h->clock->drain_events(evs);
		token_t += dt;
		if (token_t >= 0.1) {
			log_tokens = std::min(20, log_tokens + static_cast<int>(token_t * 10));
			token_t = 0.0;
		}
		std::vector<std::string> lines;
		for (const auto &e : evs) {
			if (e.type != md::ClockEvent::Type::SectionChange)
				continue;
			char line[128];
			std::snprintf(line, sizeof(line), "section -> %s seq=%llu beat=%llu (%s)",
				      section_name(e.section), (unsigned long long)e.section_seq,
				      (unsigned long long)e.beat, e.forced ? "forced" : "natural");
			lines.push_back(line);
			if (log_tokens > 0) {
				log_tokens--;
				obs_log(LOG_INFO, "[music-director] %s", line);
			}
		}

		/* 7. dock view */
		{
			std::lock_guard<std::mutex> g(h->view_mx);
			h->view.st = st;
			h->view.beats = beats_now;
			h->view.bpm_now = bpm;
			h->view.audio_name = h->settings.audio_source;
			h->view.audio_attached = h->audio_weak != nullptr;
			h->view.has_clock = true;
			h->view.settings = h->settings;
			h->view.settings.migrated_from = g_migration_note;
			for (auto &l : lines) {
				h->view.log.push_back(l);
				while (h->view.log.size() > 200)
					h->view.log.erase(h->view.log.begin());
			}
			if (tl_last_beat < 0 || beats_now < tl_last_beat || beats_now - tl_last_beat >= 0.125) {
				tl_last_beat = beats_now;
				mdhost::TimelineSample s;
				s.beat = beats_now;
				s.bass = st.bass;
				s.section = static_cast<int>(st.section);
				h->view.timeline.push_back(s);
			}
			while (!h->view.timeline.empty() && (beats_now - h->view.timeline.front().beat > 64.0 ||
							     h->view.timeline.front().beat > beats_now))
				h->view.timeline.erase(h->view.timeline.begin());
		}

		if (now - last_status >= 4.0) {
			last_status = now;
			obs_log(LOG_INFO,
				"[music-director] status bpm %.1f conf %.2f %s beat %llu phrase %llu epoch %llu "
				"level %.1f dBFS kicks %u gain %+.1f dB sens %d%%%s%s",
				st.bpm, st.confidence, section_name(st.section), (unsigned long long)st.beat,
				(unsigned long long)st.phrase, (unsigned long long)st.epoch, st.input_db, st.kicks,
				h->settings.input_gain_db, (int)lroundf(h->settings.kick_sensitivity * 100.0f),
				st.silent ? " (silent: fallback bpm)" : "", h->audio_weak ? "" : " (no audio source)");
		}
	}
	detach_audio(h);
}

/* ------------------------------------------------------------------------- */
/* the one global clock proc (include/director-clock.h)                       */

void proc_clock(void *, calldata_t *cd)
{
	Host *h = g_host;
	calldata_set_bool(cd, "ok", false);
	calldata_set_int(cd, "api_version", DIRECTOR_CLOCK_API_VERSION);
	if (!h)
		return;
	Snap s;
	{
		std::lock_guard<std::mutex> g(h->snap_mx);
		if (!h->snap.valid)
			return;
		s = h->snap;
	}
	const uint64_t now = os_gettime_ns();
	const bool paused = s.st.paused;
	calldata_set_bool(cd, "ok", true);
	calldata_set_float(cd, "beats", static_cast<double>(s.beats));
	calldata_set_float(cd, "beats_now", md::clock_extrapolate(s.beats, s.bpm, s.ts, now, paused));
	calldata_set_float(cd, "bpm", s.bpm);
	calldata_set_float(cd, "confidence", s.st.confidence);
	calldata_set_int(cd, "beat", (long long)s.st.beat);
	calldata_set_int(cd, "phrase", (long long)s.st.phrase);
	calldata_set_int(cd, "phrase_beats", s.st.phrase_beats);
	calldata_set_int(cd, "section", static_cast<int>(s.st.section));
	calldata_set_int(cd, "section_seq", (long long)s.st.section_seq);
	calldata_set_int(cd, "section_beat", (long long)s.st.section_beat);
	calldata_set_bool(cd, "section_forced", s.st.section_forced);
	calldata_set_int(cd, "epoch", (long long)s.st.epoch);
	calldata_set_int(cd, "kicks", (long long)s.st.kicks);
	calldata_set_float(cd, "bass", s.st.bass);
	calldata_set_bool(cd, "silent", s.st.silent);
	calldata_set_bool(cd, "paused", paused);
	calldata_set_bool(cd, "enabled", s.st.enabled);
	calldata_set_int(cd, "sample_ts_ns", (long long)s.ts);
	calldata_set_int(cd, "ts_source", s.ts_source);
	calldata_set_int(cd, "now_ns", (long long)now);
}

void register_clock_proc()
{
	proc_handler_t *ph = obs_get_proc_handler();
	if (!ph) {
		obs_log(LOG_ERROR, "[music-director] no global proc handler; the clock was not registered");
		return;
	}
	/* Singleton: if the proc already answers, another Music Director is loaded. */
	calldata_t cd;
	calldata_init(&cd);
	const bool taken = proc_handler_call(ph, DIRECTOR_CLOCK_PROC_CLOCK, &cd);
	const bool answered = taken && calldata_bool(&cd, "ok");
	calldata_free(&cd);
	if (answered) {
		obs_log(LOG_ERROR, "[music-director] director_clock is already registered (another Music Director?); "
				   "this copy registers nothing");
		return;
	}
	proc_handler_add(
		ph,
		"void director_clock(out bool ok, out int api_version, out float beats, out float beats_now, "
		"out float bpm, out float confidence, out int beat, out int phrase, out int phrase_beats, "
		"out int section, out int section_seq, out int section_beat, out bool section_forced, out int epoch, "
		"out int kicks, "
		"out float bass, out bool silent, out bool paused, out bool enabled, out int sample_ts_ns, "
		"out int ts_source, out int now_ns)",
		proc_clock, nullptr);
	obs_log(LOG_INFO, "[music-director] clock proc registered (director_clock v%d)", DIRECTOR_CLOCK_API_VERSION);
}

#if defined(ENABLE_FRONTEND_API)
/* ------------------------------------------------------------------------- */
/* scene-collection settings + migration (spec 3.1)                           */

mdhost::Settings to_settings(const md::AudioSettings &a)
{
	mdhost::Settings s;
	s.audio_source = a.audio_source;
	s.input_gain_db = a.input_gain_db;
	s.kick_sensitivity = a.kick_sensitivity;
	s.fallback_bpm = a.fallback_bpm;
	s.phrase_beats = a.phrase_beats;
	s.enabled = true;
	return s;
}

// Apply settings to the live clock (worker) and the dock view, without touching the collection.
void push_settings(const mdhost::Settings &s)
{
	if (!g_host)
		return;
	{
		std::lock_guard<std::mutex> g(g_host->view_mx);
		g_host->view.settings = s;
	}
	Config *c = new Config();
	c->settings = s;
	Config *old = g_host->pending.exchange(c);
	delete old;
}

void write_settings(obs_data_t *save_data)
{
	if (!g_host)
		return;
	mdhost::Settings s;
	{
		std::lock_guard<std::mutex> g(g_host->view_mx);
		s = g_host->view.settings;
	}
	s.migrated_from = g_migration_note;
	obs_data_t *o = obs_data_create();
	obs_data_set_string(o, "audio_source", s.audio_source.c_str());
	obs_data_set_double(o, "input_gain_db", s.input_gain_db);
	obs_data_set_double(o, "kick_sensitivity", s.kick_sensitivity);
	obs_data_set_double(o, "fallback_bpm", s.fallback_bpm);
	obs_data_set_int(o, "phrase_beats", s.phrase_beats);
	obs_data_set_bool(o, "enabled", s.enabled);
	obs_data_set_string(o, "migrated_from", s.migrated_from.c_str());
	mdhotkey::save_bindings(o);
	obs_data_set_obj(save_data, "music_director", o);
	obs_data_release(o);
}

bool read_settings(obs_data_t *collection)
{
	obs_data_t *o = obs_data_get_obj(collection, "music_director");
	if (!o)
		return false;
	obs_data_set_default_double(o, "input_gain_db", 0.0);
	obs_data_set_default_double(o, "kick_sensitivity", 0.6);
	obs_data_set_default_double(o, "fallback_bpm", 128.0);
	obs_data_set_default_int(o, "phrase_beats", 32);
	obs_data_set_default_bool(o, "enabled", true);
	mdhost::Settings s;
	s.audio_source = obs_data_get_string(o, "audio_source");
	s.input_gain_db = static_cast<float>(obs_data_get_double(o, "input_gain_db"));
	s.kick_sensitivity = static_cast<float>(obs_data_get_double(o, "kick_sensitivity"));
	s.fallback_bpm = static_cast<float>(obs_data_get_double(o, "fallback_bpm"));
	s.phrase_beats = static_cast<int>(obs_data_get_int(o, "phrase_beats")) == 16 ? 16 : 32;
	s.enabled = obs_data_get_bool(o, "enabled");
	s.migrated_from = obs_data_get_string(o, "migrated_from");
	g_migration_note = s.migrated_from;
	mdhotkey::load_bindings(o);
	obs_data_release(o);
	push_settings(s);
	return true;
}

struct EnumCtx {
	std::vector<md::PaletteLock> records;
	std::vector<obs_source_t *> filters; // strong refs; released by the caller
};

void enum_filters(obs_source_t *parent, obs_source_t *filter, void *param)
{
	(void)parent;
	auto *ctx = static_cast<EnumCtx *>(param);
	if (!filter || !obs_source_get_id(filter) || strcmp(obs_source_get_id(filter), "palette_lock_filter") != 0)
		return;
	obs_data_t *fs = obs_source_get_settings(filter);
	md::PaletteLock l;
	l.filter = obs_source_get_name(filter) ? obs_source_get_name(filter) : "";
	l.enabled = obs_source_enabled(filter);
	l.has_audio_source = obs_data_has_user_value(fs, "audio_source");
	obs_data_set_default_double(fs, "input_gain_db", 0.0);
	obs_data_set_default_double(fs, "kick_sensitivity", 0.6);
	obs_data_set_default_double(fs, "fallback_bpm", 128.0);
	obs_data_set_default_int(fs, "phrase_beats", 32);
	l.values.audio_source = obs_data_get_string(fs, "audio_source");
	l.values.input_gain_db = static_cast<float>(obs_data_get_double(fs, "input_gain_db"));
	l.values.kick_sensitivity = static_cast<float>(obs_data_get_double(fs, "kick_sensitivity"));
	l.values.fallback_bpm = static_cast<float>(obs_data_get_double(fs, "fallback_bpm"));
	l.values.phrase_beats = static_cast<int>(obs_data_get_int(fs, "phrase_beats")) == 16 ? 16 : 32;
	obs_data_release(fs);
	ctx->records.push_back(l);
	ctx->filters.push_back(obs_source_get_ref(filter));
}

bool enum_sources(void *param, obs_source_t *source)
{
	auto *ctx = static_cast<EnumCtx *>(param);
	const std::size_t before = ctx->records.size();
	obs_source_enum_filters(source, enum_filters, ctx);
	const char *parent = obs_source_get_name(source) ? obs_source_get_name(source) : "";
	for (std::size_t i = before; i < ctx->records.size(); ++i)
		ctx->records[i].source = parent;
	return true;
}

void copy_hotkeys_from(obs_source_t *filter)
{
	obs_data_t *fs = obs_source_get_settings(filter);
	obs_data_t *bind = obs_data_get_obj(fs, "hotkey_bindings");
	if (bind) {
		static const char *kPdNames[] = {"pd_phrase_now", "pd_drop_now",   "pd_force_breakdown", "pd_tap_tempo",
						 "pd_lock_bpm",   "pd_nudge_back", "pd_nudge_fwd"};
		for (const char *name : kPdNames) {
			obs_data_array_t *arr = obs_data_get_array(bind, name);
			if (arr) {
				mdhotkey::load_pd_binding(name, arr);
				obs_data_array_release(arr);
			}
		}
		obs_data_release(bind);
	}
	obs_data_release(fs);
}

// Spec 3.1: on FINISHED_LOADING / SCENE_COLLECTION_CHANGED, when the collection has no
// music_director key, copy the audio settings from a Palette Lock filter. Never writes to the
// filter.
void migrate_if_needed(obs_data_t *collection)
{
	if (!collection || !g_host)
		return;
	if (obs_data_has_user_value(collection, "music_director"))
		return;

	EnumCtx ctx;
	obs_enum_sources(enum_sources, &ctx);
	const md::Migration m = md::pick_migration(ctx.records);

	if (!m.found) {
		obs_log(LOG_INFO, "[music-director] no Palette Lock filter; using Music Director's own defaults");
		g_migration_note.clear();
		push_settings(mdhost::Settings{});
		write_settings(collection); // mark the collection as migrated so this runs once
	} else {
		const mdhost::Settings s = to_settings(m.values);
		push_settings(s);
		copy_hotkeys_from(ctx.filters[m.index]);
		g_migration_note = m.filter + " on " + m.source;
		{
			std::lock_guard<std::mutex> g(g_host->view_mx);
			g_host->view.settings = s;
			g_host->view.settings.migrated_from = g_migration_note;
		}
		obs_log(LOG_INFO,
			"[music-director] migrated audio settings from '%s' on '%s': audio '%s', gain %+.1f dB, "
			"sens %d%%, fallback %.0f BPM, phrase %d",
			m.filter.c_str(), m.source.c_str(), m.values.audio_source.c_str(), m.values.input_gain_db,
			(int)lroundf(m.values.kick_sensitivity * 100.0f), m.values.fallback_bpm, m.values.phrase_beats);
		if (!m.differing.empty()) {
			std::string names;
			for (int idx : m.differing) {
				if (!names.empty())
					names += ", ";
				names += "'" + ctx.records[idx].filter + "' on '" + ctx.records[idx].source + "'";
			}
			obs_log(LOG_WARNING,
				"[music-director] other Palette Lock filters hold different audio settings (not "
				"migrated): %s",
				names.c_str());
		}
		write_settings(collection);
	}
	for (obs_source_t *f : ctx.filters)
		if (f)
			obs_source_release(f);
}

void frontend_save(obs_data_t *save_data, bool saving, void *)
{
	if (!save_data)
		return;
	if (saving) {
		write_settings(save_data);
		return;
	}
	if (g_collection != save_data) {
		obs_data_release(g_collection);
		g_collection = save_data;
		obs_data_addref(g_collection);
	}
	// Read this collection's settings; if it has no music_director key, migrate from a Palette Lock
	// filter (or write Music Director's defaults) exactly once.
	if (!read_settings(g_collection))
		migrate_if_needed(g_collection);
}

void frontend_event(enum obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING && event != OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
		return;
	if (!g_collection)
		return;
	read_settings(g_collection);
	migrate_if_needed(g_collection);
}
#endif // ENABLE_FRONTEND_API

} // namespace

namespace mdhost {

void load()
{
	if (g_host)
		return;
	g_host = new Host();
	{
		std::lock_guard<std::mutex> g(g_host->view_mx);
		g_host->view.settings = Settings{};
	}
	g_host->worker = std::thread(worker_main, g_host);
	register_clock_proc();
	music_director_hotkey_load();
#if defined(ENABLE_FRONTEND_API)
	obs_frontend_add_save_callback(frontend_save, nullptr);
	obs_frontend_add_event_callback(frontend_event, nullptr);
#endif
	obs_log(LOG_INFO, "[music-director] clock loaded (the suite's one music clock)");
}

void unload()
{
#if defined(ENABLE_FRONTEND_API)
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	obs_frontend_remove_save_callback(frontend_save, nullptr);
	obs_data_release(g_collection);
	g_collection = nullptr;
#endif
	music_director_hotkey_unload();
	if (g_host) {
		g_host->stop.store(true);
		if (g_host->worker.joinable())
			g_host->worker.join();
		delete g_host->pending.exchange(nullptr);
		delete g_host;
		g_host = nullptr;
	}
	obs_log(LOG_INFO, "[music-director] clock unloaded");
}

void command(Cmd c)
{
	if (!g_host)
		return;
	HostCmd hc;
	hc.cmd = c;
	g_host->cmds.push(hc);
}

void set_settings(const Settings &s, const char *why)
{
	push_settings(s);
	obs_log(LOG_INFO,
		"[music-director] settings: audio '%s', gain %+.1f dB, sens %d%%, fallback %.0f BPM, "
		"phrase %d (%s)",
		s.audio_source.c_str(), s.input_gain_db, (int)lroundf(s.kick_sensitivity * 100.0f), s.fallback_bpm,
		s.phrase_beats, why ? why : "");
#if defined(ENABLE_FRONTEND_API)
	if (g_collection)
		write_settings(g_collection);
#endif
}

View view()
{
	View out;
	if (!g_host)
		return out;
	Snap s;
	{
		std::lock_guard<std::mutex> g(g_host->snap_mx);
		s = g_host->snap;
	}
	{
		std::lock_guard<std::mutex> g(g_host->view_mx);
		out = g_host->view;
	}
	out.st = s.st;
	out.beats = s.beats;
	out.bpm_now = s.bpm;
	out.has_clock = s.valid;
	return out;
}

obs_source_t *audio_source_ref()
{
	if (!g_host)
		return nullptr;
	return obs_get_source_by_name(g_host->view.settings.audio_source.c_str());
}

} // namespace mdhost

extern "C" void music_director_clock_load(void)
{
	mdhost::load();
}

extern "C" void music_director_clock_unload(void)
{
	mdhost::unload();
}
