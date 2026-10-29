// Scene Director hotkeys: Next / Prev as frontend hotkeys, plus the listing and triggering helpers
// the MIDI Director dock uses to drive any registered hotkey by identity.
#include "sd-hotkeys.h"

#include <obs-module.h>
#include <obs.h>
#include <plugin-support.h>

#include <algorithm>
#include <string>
#include <vector>

#include "obs/sd-api.h"

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
	auto *out = static_cast<std::vector<sdhotkey::Entry> *>(data);
	sdhotkey::Entry entry;
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
	const sdhotkey::Entry *want = nullptr;
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

namespace sdhotkey {

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
	 * it from the dock's UI thread - or invokes the hotkey callback directly when callback
	 * rerouting is off. Either way the hotkey fires exactly once. */
	obs_hotkey_trigger_routed_callback(match.id, pressed);
	return true;
}

} // namespace sdhotkey

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
