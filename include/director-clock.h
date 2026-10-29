/*
Director clock contract (api_version 1)  --  public, read-only, for other OBS plugins and scripts.
Lives in EnZhm/director include/director-clock.h; Palette, Clip and Scene Director include it from here.

Music Director is the ONE music clock of the Director suite (a singleton). It registers its procedure on the
GLOBAL proc handler (obs_get_proc_handler()) when its module loads. They are plain calldata calls,
so plugins (C/C++) and OBS scripts (Lua/Python: obs.proc_handler_call) can use them. Fields are
versioned by name: later versions may add fields; existing ones keep their meaning.
Check `api_version` >= 1.

  void director_clock(out bool ok, out int api_version, ...fields below)
      There is exactly one clock (no selector). ok=false only while Music Director is shutting down.

      out float beats           beat position at sample_ts_ns (absolute, fractional)
      out float beats_now       beats extrapolated to the call time (capped at +/-0.5 s;
                                frozen while paused)
      out float bpm             tempo the beat grid runs at (fallback BPM while silent)
      out float confidence      0..1
      out int   beat            integer beat
      out int   phrase          phrase counter
      out int   phrase_beats    16 | 32
      out int   section         DIRECTOR_CLOCK_SECTION_*
      out int   section_seq     +1 on EVERY section change, natural or forced. A follower acts on a
                                section change exactly once: when section_seq differs from the last
                                value it handled.
      out int   section_beat    the beat the current section started on (the downbeat every follower
                                aims at; equal across followers by construction)
      out bool  section_forced  the current section came from a command (Drop now / Force breakdown),
                                not from detection. Followers may treat forced edges differently
                                (Scene Director: forced edges bypass its automatic-switch cooldown).
      out int   epoch           +1 whenever the beat grid is re-anchored (Phrase now, tap, nudge,
                                audio source change, reset). Followers re-sync beat/phrase tracking
                                on a change and do NOT fire catch-up beat/phrase edges for it.
      out int   kicks           monotonic kick counter (change = a kick)
      out float bass            0..1 bass envelope (for breathing effects)
      out bool  silent          feed silent (fallback BPM in use)
      out bool  paused          clock held (Music Director disabled)
      out bool  enabled         Music Director enabled
      out int   sample_ts_ns    os_gettime_ns() time `beats` refers to (OBS audio timestamp mapped
                                onto the wall clock with a sliding-minimum offset), or wall time when
                                there is no feed (see ts_source)
      out int   ts_source       1 = from audio timestamps, 0 = wall clock (no feed / silence)
      out int   now_ns          os_gettime_ns() at the call (beats_now refers to this)

The snapshot is refreshed by Music Director's worker thread about every 5 ms. The call takes one
short mutex and never blocks on audio or rendering; it is safe from any thread. Followers call it
from video_tick or their own worker, never per pixel.

When Music Director is not loaded, director_clock_read() returns false (proc not found). Followers then
idle: no crash, one clear log line, and their dock/properties say "Waiting for Music Director".
*/

#pragma once

#include <string.h>
#include <obs.h>
#include <callback/calldata.h>
#include <callback/proc.h>

#define DIRECTOR_CLOCK_API_VERSION 1
#define DIRECTOR_CLOCK_PROC_CLOCK "director_clock"

enum director_clock_section {
	DIRECTOR_CLOCK_SECTION_GROOVE = 0,
	DIRECTOR_CLOCK_SECTION_BREAKDOWN = 1,
	DIRECTOR_CLOCK_SECTION_DROP = 2,
};

/* Consumer-side convenience. This struct is NOT shared with Music Director (it is filled from
 * named calldata fields), so it can change freely without breaking anyone. */
struct director_clock {
	bool ok;
	int api_version;
	double beats, beats_now, bpm, confidence, bass;
	long long beat, phrase, section_seq, section_beat, epoch, kicks, sample_ts_ns, now_ns;
	int phrase_beats, section, ts_source;
	bool section_forced, silent, paused, enabled;
};

/* false if Music Director is not loaded or too old. */
static inline bool director_clock_read(struct director_clock *out)
{
	proc_handler_t *ph = obs_get_proc_handler();
	calldata_t cd;
	bool called;
	memset(out, 0, sizeof(*out));
	if (!ph)
		return false;
	calldata_init(&cd);
	called = proc_handler_call(ph, DIRECTOR_CLOCK_PROC_CLOCK, &cd);
	if (called) {
		out->ok = calldata_bool(&cd, "ok");
		out->api_version = (int)calldata_int(&cd, "api_version");
		out->beats = calldata_float(&cd, "beats");
		out->beats_now = calldata_float(&cd, "beats_now");
		out->bpm = calldata_float(&cd, "bpm");
		out->confidence = calldata_float(&cd, "confidence");
		out->bass = calldata_float(&cd, "bass");
		out->beat = calldata_int(&cd, "beat");
		out->phrase = calldata_int(&cd, "phrase");
		out->phrase_beats = (int)calldata_int(&cd, "phrase_beats");
		out->section = (int)calldata_int(&cd, "section");
		out->section_seq = calldata_int(&cd, "section_seq");
		out->section_beat = calldata_int(&cd, "section_beat");
		out->section_forced = calldata_bool(&cd, "section_forced");
		out->epoch = calldata_int(&cd, "epoch");
		out->kicks = calldata_int(&cd, "kicks");
		out->silent = calldata_bool(&cd, "silent");
		out->paused = calldata_bool(&cd, "paused");
		out->enabled = calldata_bool(&cd, "enabled");
		out->sample_ts_ns = calldata_int(&cd, "sample_ts_ns");
		out->ts_source = (int)calldata_int(&cd, "ts_source");
		out->now_ns = calldata_int(&cd, "now_ns");
	}
	calldata_free(&cd);
	return called && out->ok && out->api_version >= DIRECTOR_CLOCK_API_VERSION;
}
