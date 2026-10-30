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

#include <string>
#include <vector>

#include "host/music-clock-host.h"

namespace {

#if defined(ENABLE_FRONTEND_API)

// The md_* clock hotkeys, registered in table order (director/hotkey_names.h).
const mdhost::Cmd kCmds[] = {
	mdhost::Cmd::PhraseNow, mdhost::Cmd::DropNow,   mdhost::Cmd::ForceBreakdown, mdhost::Cmd::TapTempo,
	mdhost::Cmd::LockBpm,   mdhost::Cmd::NudgeBack, mdhost::Cmd::NudgeFwd,
};
static_assert(sizeof(kCmds) / sizeof(kCmds[0]) == static_cast<size_t>(mdhotkey::kCount), "one command per md_* hotkey");
constexpr int kN = mdhotkey::kCount;
obs_hotkey_id g_ids[kN];
bool g_registered = false;

void on_hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const auto *cmd = static_cast<const mdhost::Cmd *>(data);
	mdhost::command(*cmd);
}

#endif // ENABLE_FRONTEND_API

} // namespace

namespace mdhotkey {

bool load_pd_binding(const char *pd_name, obs_data_array_t *arr)
{
#if defined(ENABLE_FRONTEND_API)
	if (!pd_name || !arr || !g_registered)
		return false;
	for (int i = 0; i < kN; ++i) {
		if (std::string(kNames[i].pd_name) != pd_name)
			continue;
		if (g_ids[i] == OBS_INVALID_HOTKEY_ID)
			return false;
		obs_hotkey_load(g_ids[i], arr);
		obs_log(LOG_INFO, "migrated hotkey binding %s -> %s", kNames[i].pd_name, kNames[i].name);
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
			obs_data_set_array(saved, kNames[i].name, arr);
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
		obs_data_array_t *arr = obs_data_get_array(saved, kNames[i].name);
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
		g_ids[i] = obs_hotkey_register_frontend(mdhotkey::kNames[i].name,
							obs_module_text(mdhotkey::kNames[i].text_key), on_hotkey,
							const_cast<mdhost::Cmd *>(&kCmds[i]));
		if (g_ids[i] == OBS_INVALID_HOTKEY_ID)
			obs_log(LOG_ERROR, "hotkey '%s' could not be registered", mdhotkey::kNames[i].name);
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
