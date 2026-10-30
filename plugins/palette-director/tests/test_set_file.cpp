#include "doctest.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "sets/set-file.h"

/*
 * Set files, schema 2 (spec 5.1). The bundled preset must load, and saving it again must
 * reproduce the source byte for byte (design acceptance 11: "Save round-trips to an
 * identical file"; card P6: load -> save -> JSON-equal).
 */

static std::string slurp(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static const std::string kBundled = std::string(PD_SOURCE_ROOT) + "/data/sets/neon-default.json";

TEST_CASE("bundled Neon Default loads with every palette, mode and setting")
{
	const std::string src = slurp(kBundled);
	REQUIRE(!src.empty());
	struct pl_set s;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(src.c_str(), &s, err, sizeof(err)), err);
	CHECK(std::string(s.name) == "Neon Default");
	CHECK(std::string(s.synesthesia_playlist) == "Neon Default");
	/* Data-driven on purpose: the palettes array is owned by the set designer and may be
	 * swapped without touching code. Check the structural rules every palette must satisfy, not
	 * the names or colours. The settings below are pinned. */
	REQUIRE(s.n_palettes >= 1);
	REQUIRE(s.n_palettes <= PL_MAX_PALETTES);
	for (int i = 0; i < s.n_palettes; i++) {
		INFO("palette " << i << " '" << s.palettes[i].name << "'");
		CHECK(s.palettes[i].name[0] != '\0');
		CHECK(pl_name_unique(&s, s.palettes[i].name, i));
		CHECK((s.palettes[i].mode == PL_MODE_DUOTONE || s.palettes[i].mode == PL_MODE_GRADIENT));
		CHECK(s.palettes[i].n_colours >= 2);
		CHECK(s.palettes[i].n_colours <= PL_MAX_COLOURS);
	}
	CHECK(s.strength == doctest::Approx(1.0f));
	CHECK(s.contrast == doctest::Approx(1.2f));
	CHECK(s.preserve == doctest::Approx(0.0f));
	CHECK(s.input_black == doctest::Approx(0.0f));
	CHECK(s.input_white == doctest::Approx(1.0f));
	CHECK(s.brightness_source == PL_BSRC_PERCEIVED);
	CHECK(s.trigger == PL_TRIGGER_PHRASES);
	CHECK(s.every_phrases == 8);
	CHECK(s.order == PL_ORDER_SHUFFLE);
	CHECK(s.crossfade_beats == doctest::Approx(1.0f));
	CHECK(std::string(s.breakdown) == "Spectrum Fallout");
	CHECK(pl_breakdown_index(&s) >= 0); // resolved BY NAME, wherever it sits in the list
	CHECK(s.fallback_bpm == doctest::Approx(132.0f));
	CHECK(s.random_off == true);
	CHECK(s.random_chance == doctest::Approx(0.12f));
	CHECK(s.bars_min == 1);
	CHECK(s.bars_max == 2);
	CHECK(s.cooldown_phrases == 3);
	CHECK(s.off_on_breakdown == false);
	CHECK(s.off_on_drop == true);
	CHECK(s.drop_bars == 1);
	CHECK(s.breathing == doctest::Approx(0.0f));
	CHECK(s.rotation == false);
	CHECK(s.seed == 2077u);
	/* v1.0.1 audio keys */
	CHECK(s.input_gain_db == doctest::Approx(0.0f));
	CHECK(s.kick_sensitivity == doctest::Approx(0.6f));
	/* v1.0.1 drop colour cycle: on, 50%, 1 bar, 1/8 */
	CHECK(s.drop_cycle == true);
	CHECK(s.drop_cycle_chance == doctest::Approx(0.5f));
	CHECK(s.drop_cycle_bars == 1);
	CHECK(s.drop_cycle_steps_per_beat == 2);
}

TEST_CASE("load -> save round-trips the bundled preset byte for byte")
{
	const std::string src = slurp(kBundled);
	struct pl_set s;
	char err[256] = {0};
	REQUIRE(pd_set_parse(src.c_str(), &s, err, sizeof(err)));
	char *out = pd_set_write(&s);
	REQUIRE(out != nullptr);
	CHECK(std::string(out) == src);
	// and the saved text loads back to the same set
	struct pl_set s2;
	REQUIRE(pd_set_parse(out, &s2, err, sizeof(err)));
	char *out2 = pd_set_write(&s2);
	CHECK(std::string(out2) == std::string(out));
	pd_set_free(out);
	pd_set_free(out2);
}

TEST_CASE("the spec 5.1 example loads; breakdown is a name: follows reorder + rename, cleared on delete")
{
	const char *spec = R"({
  "schema": 2,
  "name": "Warehouse 2026-11-07",
  "synesthesia_playlist": "Techno Nov",
  "palettes": [
    { "name": "Night Neon",    "mode": "gradient_map", "colours": ["#0B0B12", "#3A0CA3", "#F72585", "#4CC9F0"] },
    { "name": "Infrared",      "mode": "gradient_map", "colours": ["#07070A", "#5A0000", "#FF2A00", "#FFD166"] },
    { "name": "Ice",           "mode": "gradient_map", "colours": ["#05060A", "#0D3B66", "#7FDBFF", "#F0FBFF"] },
    { "name": "Breakdown Duo", "mode": "duotone",      "colours": ["#0A0A12", "#C9C9FF"] }
  ],
  "colour": { "strength": 1.0, "contrast": 1.1, "keep_brightness": 0.0 },
  "palette_changes": {
    "trigger": "drop", "every_phrases": 4, "chance": 0.25, "order": "sequence",
    "crossfade_beats": 2, "breakdown_palette": "Breakdown Duo"
  },
  "director": {
    "enabled": true, "phrase_beats": 32, "lock_fade_beats": 1, "fallback_bpm": 128,
    "random_off": { "enabled": true, "chance": 0.15, "bars_min": 1, "bars_max": 4, "cooldown_phrases": 2 },
    "off_on_breakdown": false,
    "off_on_drop": { "enabled": false, "bars": 2 },
    "breathing": 0.0, "rotation": false, "seed": 7
  }
})";
	struct pl_set s;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(spec, &s, err, sizeof(err)), err);
	CHECK(s.n_palettes == 4);
	CHECK(s.palettes[3].mode == PL_MODE_DUOTONE);
	CHECK(pl_breakdown_index(&s) == 3);
	int live = 0, edit = 3;
	pl_move_palette(&s, 3, -1, &live, &edit); // move the breakdown palette up
	CHECK(pl_breakdown_index(&s) == 2);
	CHECK(pl_rename_palette(&s, 2, "BD"));
	CHECK(std::string(s.breakdown) == "BD"); // follows the rename
	CHECK(pl_breakdown_index(&s) == 2);
	CHECK(pl_delete_palette(&s, 2, &live, &edit));
	CHECK(std::string(s.breakdown).empty()); // deleting it sets breakdown to None
	char *out = pd_set_write(&s);
	struct pl_set s2;
	REQUIRE(pd_set_parse(out, &s2, err, sizeof(err)));
	CHECK(s2.n_palettes == 3);
	CHECK(std::string(s2.breakdown).empty());
	pd_set_free(out);
}

TEST_CASE("invalid set files are rejected with a reason")
{
	struct pl_set s;
	char err[256] = {0};
	CHECK_FALSE(pd_set_parse("{\"schema\": 1, \"name\": \"x\"}", &s, err, sizeof(err)));
	CHECK(std::strstr(err, "schema") != nullptr);
	CHECK_FALSE(pd_set_parse("{\"schema\": 2, \"name\": \"x\", \"palettes\": []}", &s, err, sizeof(err)));
	CHECK_FALSE(pd_set_parse("{\"schema\": 2, \"name\": \"x\", \"palettes\": [{\"name\": \"a\", \"colours\": "
				 "[\"#000000\"]}]}",
				 &s, err, sizeof(err)));
	CHECK_FALSE(pd_set_parse("{\"schema\": 2, \"name\": \"x\", \"palettes\": [{\"name\": \"a\", \"colours\": "
				 "[\"#000000\", \"#FFF\"]}]}",
				 &s, err, sizeof(err)));
	CHECK_FALSE(pd_set_parse("{\"schema\": 2, \"name\": \"x\", \"palettes\": [{\"name\": \"a\", \"colours\": "
				 "[\"#000000\", \"#FFFFFF\"]}], \"palette_changes\": {\"breakdown_palette\": \"zz\"}}",
				 &s, err, sizeof(err)));
	CHECK_FALSE(pd_set_parse("not json", &s, err, sizeof(err)));
}

TEST_CASE("a 17th palette is rejected on load")
{
	std::string j = "{\"schema\": 2, \"name\": \"Big\", \"palettes\": [";
	for (int i = 0; i < 17; i++)
		j += std::string(i ? "," : "") + "{\"name\": \"p" + std::to_string(i) +
		     "\", \"colours\": [\"#000000\", \"#FFFFFF\"]}";
	j += "]}";
	struct pl_set s;
	char err[256] = {0};
	CHECK_FALSE(pd_set_parse(j.c_str(), &s, err, sizeof(err)));
	CHECK(std::strstr(err, "1-16") != nullptr);
	/* 16 is fine and survives a round trip */
	const std::string j16 = j.substr(0, j.rfind(",{")) + "]}";
	REQUIRE_MESSAGE(pd_set_parse(j16.c_str(), &s, err, sizeof(err)), err);
	CHECK(s.n_palettes == 16);
	char *out = pd_set_write(&s);
	struct pl_set s2;
	REQUIRE(pd_set_parse(out, &s2, err, sizeof(err)));
	CHECK(s2.n_palettes == 16);
	CHECK(std::string(s2.palettes[15].name) == "p15");
	pd_set_free(out);
}

TEST_CASE("v1 look keys are optional on load and round-trip when present")
{
	/* an older schema-2 file without the v1 keys loads with the defaults */
	const char *old_file = "{\"schema\": 2, \"name\": \"Old\", \"palettes\": [{\"name\": \"a\", \"colours\": "
			       "[\"#000000\", \"#FF0000\"]}], \"colour\": {\"strength\": 0.5}}";
	struct pl_set s;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(old_file, &s, err, sizeof(err)), err);
	CHECK(s.strength == doctest::Approx(0.5f));
	CHECK(s.input_black == doctest::Approx(0.0f));
	CHECK(s.input_white == doctest::Approx(1.0f));
	CHECK(s.brightness_source == PL_BSRC_PERCEIVED);

	s.input_black = 0.125f;
	s.input_white = 0.75f;
	s.brightness_source = PL_BSRC_MAX;
	char *out = pd_set_write(&s);
	REQUIRE(out != nullptr);
	CHECK(std::strstr(out, "\"input_black\": 0.125") != nullptr);
	CHECK(std::strstr(out, "\"input_white\": 0.75") != nullptr);
	CHECK(std::strstr(out, "\"brightness_source\": \"max\"") != nullptr);
	struct pl_set s2;
	REQUIRE(pd_set_parse(out, &s2, err, sizeof(err)));
	CHECK(s2.input_black == doctest::Approx(0.125f));
	CHECK(s2.input_white == doctest::Approx(0.75f));
	CHECK(s2.brightness_source == PL_BSRC_MAX);
	pd_set_free(out);

	/* out-of-range / unknown values are rejected */
	CHECK_FALSE(pd_set_parse("{\"schema\": 2, \"name\": \"x\", \"palettes\": [{\"name\": \"a\", \"colours\": "
				 "[\"#000000\", \"#FFFFFF\"]}], \"colour\": {\"input_white\": 0.2}}",
				 &s, err, sizeof(err)));
	CHECK_FALSE(pd_set_parse("{\"schema\": 2, \"name\": \"x\", \"palettes\": [{\"name\": \"a\", \"colours\": "
				 "[\"#000000\", \"#FFFFFF\"]}], \"colour\": {\"brightness_source\": \"hue\"}}",
				 &s, err, sizeof(err)));
	CHECK(std::strstr(err, "brightness_source") != nullptr);
}

TEST_CASE("set file names follow the mock's slug rule")
{
	char out[128];
	pd_set_slug("Neon Default", out, sizeof(out));
	CHECK(std::string(out) == "Neon-Default");
	pd_set_slug("Warehouse 2026-11-07", out, sizeof(out));
	CHECK(std::string(out) == "Warehouse-2026-11-07");
	pd_set_slug("A/B: test  set!", out, sizeof(out));
	CHECK(std::string(out) == "AB-test-set");
	pd_set_slug("///", out, sizeof(out));
	CHECK(std::string(out) == "set");
}

TEST_CASE("v1.0.1 audio keys are optional on load (v1.0.0 files keep today's behaviour) and round-trip")
{
	std::string src = slurp(kBundled);
	const auto a = src.find(",\n    \"input_gain_db\""); // strip every v1.0.1 director key
	REQUIRE(a != std::string::npos);
	const auto b = src.find("\n  }\n}", a);
	std::string old = src.substr(0, a) + src.substr(b); // a v1.0.0-style file
	struct pl_set s;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(old.c_str(), &s, err, sizeof(err)), err);
	CHECK(s.input_gain_db == doctest::Approx(0.0f));
	CHECK(s.kick_sensitivity == doctest::Approx(0.6f));
	s.input_gain_db = 7.5f;
	s.kick_sensitivity = 0.85f;
	char *out = pd_set_write(&s);
	struct pl_set s2;
	REQUIRE(pd_set_parse(out, &s2, err, sizeof(err)));
	CHECK(s2.input_gain_db == doctest::Approx(7.5f));
	CHECK(s2.kick_sensitivity == doctest::Approx(0.85f));
	pd_set_free(out);
	// out-of-range values are rejected with a reason, like every other key
	std::string hot = src;
	hot.replace(hot.find("\"input_gain_db\": 0"), 18, "\"input_gain_db\": 99");
	CHECK_FALSE(pd_set_parse(hot.c_str(), &s2, err, sizeof(err)));
	CHECK(std::string(err).find("input_gain_db") != std::string::npos);
}

TEST_CASE("v1.0.1 drop_cycle keys: missing = disabled, every rate round-trips, bad rate rejected")
{
	const std::string src = slurp(kBundled);
	const auto a = src.find(",\n    \"drop_cycle\"");
	REQUIRE(a != std::string::npos);
	const auto b = src.find("\n  }\n}", a);
	const std::string old = src.substr(0, a) + src.substr(b);
	struct pl_set s;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(old.c_str(), &s, err, sizeof(err)), err);
	CHECK(s.drop_cycle == false); // older files: exactly as before
	for (int spb : {1, 2, 4}) {
		s.drop_cycle = true;
		s.drop_cycle_chance = 0.25f;
		s.drop_cycle_bars = 3;
		s.drop_cycle_steps_per_beat = spb;
		char *out = pd_set_write(&s);
		struct pl_set s2;
		REQUIRE(pd_set_parse(out, &s2, err, sizeof(err)));
		CHECK(s2.drop_cycle);
		CHECK(s2.drop_cycle_chance == doctest::Approx(0.25f));
		CHECK(s2.drop_cycle_bars == 3);
		CHECK(s2.drop_cycle_steps_per_beat == spb);
		char *out2 = pd_set_write(&s2);
		CHECK(std::string(out) == std::string(out2));
		pd_set_free(out);
		pd_set_free(out2);
	}
	std::string bad = src;
	bad.replace(bad.find("\"1/8\""), 5, "\"1/3\"");
	CHECK_FALSE(pd_set_parse(bad.c_str(), &s, err, sizeof(err)));
	CHECK(std::string(err).find("drop_cycle rate") != std::string::npos);
}

TEST_CASE("v2.1 palette pool: pool:false round-trips byte-identical; absent = in the pool")
{
	struct pl_set s;
	pl_set_init(&s);
	snprintf(s.name, sizeof(s.name), "Pooled");
	s.n_palettes = 4;
	for (int i = 0; i < 4; i++) {
		pl_palette_default(&s.palettes[i], "");
		snprintf(s.palettes[i].name, sizeof(s.palettes[i].name), "Pal %d", i);
	}
	s.palettes[1].in_pool = false;
	s.palettes[3].in_pool = false;
	char *out = pd_set_write(&s);
	REQUIRE(out != nullptr);
	/* the key is written ONLY where false */
	int n_pool = 0;
	for (const char *p = out; (p = strstr(p, "\"pool\": false")) != nullptr; p++)
		n_pool++;
	CHECK(n_pool == 2);

	struct pl_set s2;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(out, &s2, err, sizeof(err)), err);
	CHECK(s2.palettes[0].in_pool == true);
	CHECK(s2.palettes[1].in_pool == false);
	CHECK(s2.palettes[2].in_pool == true);
	CHECK(s2.palettes[3].in_pool == false);
	char *out2 = pd_set_write(&s2);
	CHECK(std::string(out) == std::string(out2)); // byte-identical
	pd_set_free(out);
	pd_set_free(out2);

	/* a 2.0-style set (no pool key) loads with every palette in the pool and stays absent */
	const char *old = "{\"schema\": 2, \"name\": \"Old\", \"palettes\": ["
			  "{\"name\": \"a\", \"mode\": \"gradient_map\", \"colours\": [\"#000000\", \"#FFFFFF\"]},"
			  "{\"name\": \"b\", \"mode\": \"gradient_map\", \"colours\": [\"#000000\", \"#FFFFFF\"]}]}";
	struct pl_set s3;
	REQUIRE_MESSAGE(pd_set_parse(old, &s3, err, sizeof(err)), err);
	CHECK(s3.palettes[0].in_pool == true);
	CHECK(s3.palettes[1].in_pool == true);
	char *out3 = pd_set_write(&s3);
	CHECK(std::strstr(out3, "\"pool\"") == nullptr); // never written when every palette is in
	struct pl_set s4;
	REQUIRE_MESSAGE(pd_set_parse(out3, &s4, err, sizeof(err)), err);
	char *out4 = pd_set_write(&s4);
	CHECK(std::string(out3) == std::string(out4));
	pd_set_free(out3);
	pd_set_free(out4);
}

TEST_CASE("v2.1 palette pool: new and duplicated palettes are in the pool")
{
	struct pl_set s;
	pl_set_init(&s);
	snprintf(s.name, sizeof(s.name), "Lifecycle");
	s.n_palettes = 1;
	pl_palette_default(&s.palettes[0], "Pal 0");
	s.palettes[0].in_pool = false; // the source is out of the pool

	int idx = -1;
	REQUIRE(pl_add_palette(&s, "Pal 1", &idx));
	CHECK(s.palettes[idx].in_pool == true); // new -> in the pool

	int live = 0, edit = 0, dup = -1;
	REQUIRE(pl_duplicate_palette(&s, 0, &live, &edit, &dup));
	CHECK(s.palettes[dup].in_pool == true); // duplicated -> in the pool (not copied from the source)
}

TEST_CASE("v2.1 palette pool: the bundled preset has no pool key and loads all-in-pool")
{
	const std::string src = slurp(kBundled);
	struct pl_set s;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(src.c_str(), &s, err, sizeof(err)), err);
	for (int i = 0; i < s.n_palettes; i++)
		CHECK(s.palettes[i].in_pool == true);
	char *out = pd_set_write(&s);
	CHECK(std::string(out) == src); // the bundled file round-trips byte-identical
	pd_set_free(out);
}

TEST_CASE("a spectrum-style set (16 palettes, 5-8 colours) round-trips byte-identical")
{
	/* Spectrum palettes (spec 4.1): 16 gradient_map palettes with 5-8 stops. Build it, write
	 * it, parse it back and write it again: the writer is canonical, so the two texts must be
	 * byte-identical, and loading the text must reproduce every palette and colour. */
	struct pl_set s;
	pl_set_init(&s);
	snprintf(s.name, sizeof(s.name), "Spectrum");
	snprintf(s.synesthesia_playlist, sizeof(s.synesthesia_playlist), "Spectrum");
	s.n_palettes = 16;
	for (int i = 0; i < 16; i++) {
		pl_palette_default(&s.palettes[i], "");
		snprintf(s.palettes[i].name, sizeof(s.palettes[i].name), "Band %02d", i);
		s.palettes[i].mode = PL_MODE_GRADIENT;
		const int n = 5 + (i % 4); // 5..8
		s.palettes[i].n_colours = n;
		for (int j = 0; j < n; j++) {
			s.palettes[i].colours[j].r = static_cast<float>(j) / static_cast<float>(n - 1);
			s.palettes[i].colours[j].g = static_cast<float>(i) / 15.0f;
			s.palettes[i].colours[j].b = 1.0f - static_cast<float>(j) / static_cast<float>(n - 1);
		}
	}
	char *out = pd_set_write(&s);
	REQUIRE(out != nullptr);
	struct pl_set s2;
	char err[256] = {0};
	REQUIRE_MESSAGE(pd_set_parse(out, &s2, err, sizeof(err)), err);
	CHECK(s2.n_palettes == 16);
	for (int i = 0; i < 16; i++) {
		CHECK(s2.palettes[i].n_colours == 5 + (i % 4));
		CHECK(s2.palettes[i].mode == PL_MODE_GRADIENT);
		char h[PL_HEX_LEN], h2[PL_HEX_LEN];
		pl_colour_to_hex(s.palettes[i].colours[0], h);
		pl_colour_to_hex(s2.palettes[i].colours[0], h2);
		CHECK(std::strcmp(h, h2) == 0);
	}
	char *out2 = pd_set_write(&s2);
	REQUIRE(out2 != nullptr);
	CHECK(std::string(out2) == std::string(out));
	pd_set_free(out);
	pd_set_free(out2);
}
