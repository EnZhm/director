// MIDI Director removal (spec 6): the md_* hotkey table keeps md_drop_now and the other six, and
// the scene collection's midi_director value is parsed tolerantly, hinted per target and kept
// verbatim. The binding save/load itself (mdhotkey::save_bindings / load_bindings) is libobs-only
// (obs_hotkey_save / obs_hotkey_load), so the pure layer tested here is the name table it iterates.
#include "doctest.h"

#include <cstring>
#include <set>
#include <string>

#include "hotkey_names.h"
#include "midi_legacy.h"

namespace {

const char *kTwoRows =
	"{\"version\":1,\"mappings\":[\n"
	"  {\"type\":\"note\",\"channel\":1,\"number\":36,\"mode\":\"momentary\",\"name\":\"md_drop_now\","
	"\"registerer\":\"frontend\",\"registerer_name\":\"\",\"description\":\"Music Director: Drop now\"},\n"
	"  {\"type\":\"cc\",\"channel\":2,\"number\":7,\"mode\":\"momentary\",\"name\":\"scene_director_next\","
	"\"registerer\":\"frontend\",\"registerer_name\":\"\",\"description\":\"Scene Director: Next\"}\n"
	"]}";

} // namespace

TEST_CASE("md_* hotkey table: md_drop_now and the other six, names unchanged from 2.0")
{
	const std::set<std::string> want = {"md_phrase_now", "md_drop_now",   "md_force_breakdown", "md_tap_tempo",
					    "md_lock_bpm",   "md_nudge_back", "md_nudge_fwd"};
	std::set<std::string> got;
	for (const mdhotkey::Name &n : mdhotkey::kNames) {
		got.insert(n.name);
		CHECK(std::strncmp(n.text_key, "Hotkey", 6) == 0);
		CHECK(std::strncmp(n.pd_name, "pd_", 3) == 0);
		CHECK(std::string(n.name).substr(3) == std::string(n.pd_name).substr(3)); // md_x <- pd_x
	}
	CHECK(mdhotkey::kCount == 7);
	CHECK(got == want);
	const mdhotkey::Name *drop = mdhotkey::find("md_drop_now");
	REQUIRE(drop != nullptr);
	CHECK(std::string(drop->text_key) == "HotkeyDropNow");
	CHECK(std::string(drop->label) == "Drop now");
	CHECK(mdhotkey::find("md_drop") == nullptr);
	CHECK(mdhotkey::find(nullptr) == nullptr);
}

TEST_CASE("legacy midi_director: v1 rows parse to (control label, target)")
{
	const md::LegacyMidi m = md::parse_legacy_midi(kTwoRows);
	CHECK(m.parsed);
	REQUIRE(m.mappings.size() == 2);
	CHECK(m.mappings[0].control == "Note 36 ch 1");
	CHECK(m.mappings[0].target == "md_drop_now");
	CHECK(m.mappings[1].control == "CC 7 ch 2");
	CHECK(m.mappings[1].target == "scene_director_next");
}

TEST_CASE("legacy midi_director: tolerant parse never throws and skips what it cannot use")
{
	CHECK_FALSE(md::parse_legacy_midi("").parsed);
	CHECK_FALSE(md::parse_legacy_midi("not json").parsed);
	CHECK_FALSE(md::parse_legacy_midi("{\"mappings\":[{\"name\":\"md_tap_tempo\"").parsed); // truncated
	CHECK_FALSE(md::parse_legacy_midi("{} trailing").parsed);
	CHECK(md::parse_legacy_midi("{}").parsed);
	CHECK(md::parse_legacy_midi("{\"version\":1,\"mappings\":[]}").mappings.empty());

	// unknown keys, odd value types and a nameless row: kept parseable, nameless row dropped
	const md::LegacyMidi m = md::parse_legacy_midi(
		"{\"version\":2,\"extra\":{\"a\":[1,2,{\"b\":null}]},\"mappings\":["
		"{\"type\":\"note\",\"channel\":true,\"number\":\"x\",\"name\":\"md_tap_tempo\",\"future\":[1.5e3]},"
		"{\"type\":\"cc\",\"channel\":1,\"number\":1}]}");
	CHECK(m.parsed);
	REQUIRE(m.mappings.size() == 1);
	CHECK(m.mappings[0].target == "md_tap_tempo");
	CHECK(m.mappings[0].control == "Note ? ch ?");

	// deep nesting is bounded, not a stack overflow
	std::string deep = "{\"x\":";
	for (int i = 0; i < 10000; ++i)
		deep += "[";
	CHECK_FALSE(md::parse_legacy_midi(deep).parsed);

	// every byte value, every prefix of a real document: no crash, no throw
	const std::string doc = kTwoRows;
	for (size_t n = 0; n <= doc.size(); ++n)
		(void)md::parse_legacy_midi(doc.substr(0, n));
	std::string bytes;
	for (int c = 0; c < 256; ++c)
		bytes += static_cast<char>(c);
	(void)md::parse_legacy_midi(bytes);
}

TEST_CASE("legacy midi_director: one rebind hint per target (spec 6.2 / 6.3)")
{
	md::LegacyMapping drop{"Note 36 ch 1", "md_drop_now", "Music Director: Drop now"};
	md::LegacyMapping next{"CC 7 ch 2", "scene_director_next", "Scene Director: Next"};
	md::LegacyMapping prev{"CC 8 ch 2", "scene_director_prev", ""};
	md::LegacyMapping brk{"Note 40 ch 1", "md_force_breakdown", "Music Director: Force breakdown"};
	md::LegacyMapping other{"Note 41 ch 1", "clip_director_next", "Clip Director: Next clip"};
	md::LegacyMapping bare{"Note 42 ch 1", "some_hotkey", ""};

	CHECK(md::rebind_hint(drop) == "bind Drop now in Settings > Hotkeys or on a Stream Deck");
	CHECK(md::rebind_hint(next) == "map the control in Synesthesia's MIDI panel");
	CHECK(md::rebind_hint(prev) == "map the control in Synesthesia's MIDI panel");
	CHECK(md::rebind_hint(brk) == "bind Force breakdown in Settings > Hotkeys");
	CHECK(md::rebind_hint(other) == "bind Clip Director: Next clip in Settings > Hotkeys");
	CHECK(md::rebind_hint(bare) == "bind some_hotkey in Settings > Hotkeys");

	CHECK(md::rebind_line(drop) == "MIDI Director mapping 'Note 36 ch 1' -> md_drop_now (Drop now) is inactive: "
				       "bind Drop now in Settings > Hotkeys or on a Stream Deck");
	CHECK(md::removal_line(2, "/cfg/midi-director-Live.json") ==
	      "MIDI Director was removed in 2.1: 2 mappings kept in the scene collection (inactive), copy at "
	      "/cfg/midi-director-Live.json");
}

TEST_CASE("legacy midi_director: copy file name is one safe path segment")
{
	CHECK(md::legacy_copy_name("Live Set") == "midi-director-Live Set.json");
	CHECK(md::legacy_copy_name("a/b\\c:d") == "midi-director-a_b_c_d.json");
	CHECK(md::legacy_copy_name("") == "midi-director-_.json");
	CHECK(md::legacy_copy_name("..") == "midi-director-_.json");
}

TEST_CASE("legacy midi_director: the value is passed through verbatim and reported once per collection")
{
	md::LegacyMidiStore store;
	CHECK_FALSE(store.present());

	// byte for byte, including whitespace and content the parser rejects
	const std::string odd = std::string(kTwoRows) + "  \n";
	store.load(odd.c_str());
	CHECK(store.present());
	CHECK(store.raw() == odd);
	store.load("{broken");
	CHECK(store.raw() == "{broken");

	// switching to a collection without the key drops the old value (it must not leak across)
	store.load(nullptr);
	CHECK_FALSE(store.present());
	store.load("");
	CHECK_FALSE(store.present());

	CHECK(store.first_report("Live"));
	CHECK_FALSE(store.first_report("Live"));
	CHECK(store.first_report("Rehearsal"));
}
