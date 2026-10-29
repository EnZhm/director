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

#include "obs/md-hotkeys.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <string>
#include <vector>

#include "host/music-clock-host.h"

namespace {

std::string text(const char *s)
{
	return s ? std::string(s) : std::string();
}

std::string registerer_of(const obs_hotkey_t *key)
{
	switch (obs_hotkey_get_registerer_type(key)) {
	case OBS_HOTKEY_REGISTERER_SOURCE:
		return "source";
	case OBS_HOTKEY_REGISTERER_OUTPUT:
		return "output";
	case OBS_HOTKEY_REGISTERER_ENCODER:
		return "encoder";
	case OBS_HOTKEY_REGISTERER_SERVICE:
		return "service";
	case OBS_HOTKEY_REGISTERER_FRONTEND:
		break;
	}
	return "frontend";
}

// Source-level hotkeys are identified by their source's name; nothing else carries one.
std::string registerer_name_of(const obs_hotkey_t *key)
{
	if (obs_hotkey_get_registerer_type(key) != OBS_HOTKEY_REGISTERER_SOURCE)
		return std::string();
	/* libobs registers a source hotkey with a WEAK reference as its registerer (obs-hotkey.c,
	 * obs_hotkey_register_source), so take a strong ref for the name, as OBS's own hotkey
	 * settings do. Reading it as an obs_source_t crashed OBS when the MIDI dock listed hotkeys. */
	auto *weak = static_cast<obs_weak_source_t *>(obs_hotkey_get_registerer(key));
	obs_source_t *source = weak ? obs_weak_source_get_source(weak) : nullptr;
	if (!source)
		return std::string();
	std::string name = text(obs_source_get_name(source));
	obs_source_release(source);
	return name;
}

bool collect(void *data, obs_hotkey_id, obs_hotkey_t *key)
{
	auto *out = static_cast<std::vector<mdhotkey::Entry> *>(data);
	mdhotkey::Entry entry;
	entry.name = text(obs_hotkey_get_name(key));
	if (entry.name.empty())
		return true; /* an unnamed hotkey cannot be stored in a mapping */
	entry.description = text(obs_hotkey_get_description(key));
	if (entry.description.empty())
		entry.description = entry.name;
	entry.registerer = registerer_of(key);
	entry.registerer_name = registerer_name_of(key);
	out->push_back(entry);
	return true;
}

struct Match {
	const mdhotkey::Entry *want = nullptr;
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
};

bool matches(void *data, obs_hotkey_id id, obs_hotkey_t *key)
{
	auto *match = static_cast<Match *>(data);
	if (match->id != OBS_INVALID_HOTKEY_ID)
		return false;
	if (text(obs_hotkey_get_name(key)) != match->want->name)
		return true;
	if (registerer_of(key) != match->want->registerer)
		return true;
	if (registerer_name_of(key) != match->want->registerer_name)
		return true;
	match->id = id;
	return false;
}

#if defined(ENABLE_FRONTEND_API)

// The md_* clock hotkeys. Their names are stable (MIDI mappings and migration use them).
struct MdHotkey {
	const char *name;
	const char *text_key;
	mdhost::Cmd cmd;
};

const MdHotkey kHotkeys[] = {
	{mdhotkey::kPhraseNow, "HotkeyPhraseNow", mdhost::Cmd::PhraseNow},
	{mdhotkey::kDropNow, "HotkeyDropNow", mdhost::Cmd::DropNow},
	{mdhotkey::kForceBreakdown, "HotkeyForceBreakdown", mdhost::Cmd::ForceBreakdown},
	{mdhotkey::kTapTempo, "HotkeyTapTempo", mdhost::Cmd::TapTempo},
	{mdhotkey::kLockBpm, "HotkeyLockBpm", mdhost::Cmd::LockBpm},
	{mdhotkey::kNudgeBack, "HotkeyNudgeBack", mdhost::Cmd::NudgeBack},
	{mdhotkey::kNudgeFwd, "HotkeyNudgeFwd", mdhost::Cmd::NudgeFwd},
};
constexpr int kN = static_cast<int>(sizeof(kHotkeys) / sizeof(kHotkeys[0]));
obs_hotkey_id g_ids[kN];
bool g_registered = false;

void on_hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const MdHotkey *hk = static_cast<const MdHotkey *>(data);
	mdhost::command(hk->cmd);
}

#endif // ENABLE_FRONTEND_API

} // namespace

namespace mdhotkey {

std::vector<Entry> list()
{
	std::vector<Entry> out;
	obs_enum_hotkeys(collect, &out);
	std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
		if (a.description != b.description)
			return a.description < b.description;
		if (a.registerer != b.registerer)
			return a.registerer < b.registerer;
		if (a.registerer_name != b.registerer_name)
			return a.registerer_name < b.registerer_name;
		return a.name < b.name;
	});
	return out;
}

bool trigger(const Entry &entry, bool pressed)
{
	if (entry.name.empty())
		return false;
	Match match;
	match.want = &entry;
	obs_enum_hotkeys(matches, &match);
	if (match.id == OBS_INVALID_HOTKEY_ID) {
		obs_log(LOG_DEBUG, "midi: hotkey '%s' is not registered any more; mapping ignored", entry.name.c_str());
		return false;
	}
	/* Public libobs API (obs-hotkey.h): routes the callback through the frontend's router - we call
	 * it from the dock's UI thread - or invokes the hotkey callback directly when callback rerouting
	 * is off. Either way the hotkey fires exactly once. */
	obs_hotkey_trigger_routed_callback(match.id, pressed);
	return true;
}

bool load_pd_binding(const char *pd_name, obs_data_array_t *arr)
{
#if defined(ENABLE_FRONTEND_API)
	if (!pd_name || !arr || !g_registered)
		return false;
	// Palette Director 1.2.0's clock hotkeys, in the same order as the md_* ones above.
	static const char *kPdNames[kN] = {"pd_phrase_now", "pd_drop_now",   "pd_force_breakdown", "pd_tap_tempo",
					   "pd_lock_bpm",   "pd_nudge_back", "pd_nudge_fwd"};
	for (int i = 0; i < kN; ++i) {
		if (std::string(kPdNames[i]) != pd_name)
			continue;
		if (g_ids[i] == OBS_INVALID_HOTKEY_ID)
			return false;
		obs_hotkey_load(g_ids[i], arr);
		obs_log(LOG_INFO, "migrated hotkey binding %s -> %s", kPdNames[i], kHotkeys[i].name);
		return true;
	}
#else
	(void)pd_name;
	(void)arr;
#endif
	return false;
}

void save_bindings(obs_data_t *md_obj)
{
#if defined(ENABLE_FRONTEND_API)
	if (!md_obj || !g_registered)
		return;
	obs_data_t *saved = obs_data_create();
	for (int i = 0; i < kN; ++i) {
		if (g_ids[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *arr = obs_hotkey_save(g_ids[i]);
		if (arr) {
			obs_data_set_array(saved, kHotkeys[i].name, arr);
			obs_data_array_release(arr);
		}
	}
	obs_data_set_obj(md_obj, "hotkeys", saved);
	obs_data_release(saved);
#else
	(void)md_obj;
#endif
}

void load_bindings(obs_data_t *md_obj)
{
#if defined(ENABLE_FRONTEND_API)
	if (!md_obj || !g_registered)
		return;
	obs_data_t *saved = obs_data_get_obj(md_obj, "hotkeys");
	if (!saved)
		return;
	for (int i = 0; i < kN; ++i) {
		if (g_ids[i] == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *arr = obs_data_get_array(saved, kHotkeys[i].name);
		if (arr) {
			obs_hotkey_load(g_ids[i], arr);
			obs_data_array_release(arr);
		}
	}
	obs_data_release(saved);
#else
	(void)md_obj;
#endif
}

} // namespace mdhotkey

extern "C" void music_director_hotkey_load(void)
{
#if defined(ENABLE_FRONTEND_API)
	if (g_registered)
		return;
	for (int i = 0; i < kN; ++i) {
		g_ids[i] = obs_hotkey_register_frontend(kHotkeys[i].name, obs_module_text(kHotkeys[i].text_key),
							on_hotkey, const_cast<MdHotkey *>(&kHotkeys[i]));
		if (g_ids[i] == OBS_INVALID_HOTKEY_ID)
			obs_log(LOG_ERROR, "hotkey '%s' could not be registered", kHotkeys[i].name);
	}
	g_registered = true;
	obs_log(LOG_INFO, "%d clock hotkeys registered (md_*)", kN);
#endif
}

extern "C" void music_director_hotkey_unload(void)
{
#if defined(ENABLE_FRONTEND_API)
	if (!g_registered)
		return;
	for (obs_hotkey_id id : g_ids) {
		if (id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(id);
	}
	g_registered = false;
#endif
}
