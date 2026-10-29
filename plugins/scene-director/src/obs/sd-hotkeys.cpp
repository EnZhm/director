// Scene Director hotkeys: Next / Prev as frontend hotkeys.
#include "sd-hotkeys.h"

#include <obs-module.h>
#include <obs.h>
#include <plugin-support.h>

#include "obs/sd-api.h"

namespace {

#if defined(ENABLE_FRONTEND_API)

using Action = void (*)(obs_source_t *source, bool pressed);

// A hotkey acts on the first Scene Director source of the scene collection: OBS hotkeys are global
// while the dock picks its own source, and one director drives the set.
void run_on_director(Action action, bool pressed)
{
	obs_source_t *director = nullptr;
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if (!sdapi::is_scene_director(source))
				return true;
			*(obs_source_t **)data = obs_source_get_ref(source);
			return false;
		},
		&director);
	if (!director)
		return;
	action(director, pressed);
	obs_source_release(director);
}

/* scene_director_next: switch the playlist on to the next scene (the dock's Next button). */
void action_next(obs_source_t *source, bool pressed)
{
	if (pressed)
		sdapi::manual_next(source);
}

/* scene_director_prev: back to the previous scene (the dock's Prev button). */
void action_prev(obs_source_t *source, bool pressed)
{
	if (pressed)
		sdapi::manual_prev(source);
}

Action g_actions[] = {action_next, action_prev};
obs_hotkey_id g_ids[] = {OBS_INVALID_HOTKEY_ID, OBS_INVALID_HOTKEY_ID};
bool g_registered = false;

void hotkey_callback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	run_on_director(*static_cast<Action *>(data), pressed);
}

#endif

} // namespace

extern "C" void scene_director_hotkey_load(void)
{
#if defined(ENABLE_FRONTEND_API)
	if (g_registered)
		return;
	g_ids[0] = obs_hotkey_register_frontend(sdhotkey::kNext, obs_module_text("Hotkey.Next"), hotkey_callback,
						&g_actions[0]);
	g_ids[1] = obs_hotkey_register_frontend(sdhotkey::kPrev, obs_module_text("Hotkey.Prev"), hotkey_callback,
						&g_actions[1]);
	g_registered = true;
	obs_log(LOG_INFO, "hotkeys registered: %s, %s", sdhotkey::kNext, sdhotkey::kPrev);
#endif
}

extern "C" void scene_director_hotkey_unload(void)
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
