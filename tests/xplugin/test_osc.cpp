// OSC 1.0 codec: golden byte tests for every route in spec section 5, round trips and
// malformed-packet rejection.
#include "doctest.h"

#include <cmath>
#include <string>
#include <vector>

#include "osc.h"

using namespace director_osc;

namespace {

/* every literal is a byte on the wire, zeros included, so the padding rule is pinned down */
std::vector<uint8_t> bytes(std::initializer_list<unsigned int> v)
{
	std::vector<uint8_t> o;
	for (unsigned int b : v)
		o.push_back(static_cast<uint8_t>(b));
	return o;
}

std::vector<uint8_t> encode(const OscMessage &m)
{
	std::vector<uint8_t> out;
	REQUIRE(osc_encode(m, out));
	return out;
}

} // namespace

TEST_CASE("osc: padding is to the next 4-byte boundary")
{
	CHECK(osc_padded(0) == 0);
	CHECK(osc_padded(1) == 4);
	CHECK(osc_padded(4) == 4);
	CHECK(osc_padded(5) == 8);
	CHECK(osc_padded(18) == 20);
}

TEST_CASE("osc: golden bytes for /scenes/{scene} [preset]")
{
	// "/scenes/gridtunnel" (18 + NUL -> 20), ",s\0\0" (4), "acid\0\0\0\0" (8) = 32
	const std::vector<uint8_t> want = bytes({
		'/', 's', 'c',  'e',  'n', 'e', 's',  '/',  'g', 'r', 'i', 'd', 't',  'u',  'n',  'n',
		'e', 'l', 0x00, 0x00, ',', 's', 0x00, 0x00, 'a', 'c', 'i', 'd', 0x00, 0x00, 0x00, 0x00,
	});
	const std::vector<uint8_t> got = encode(osc_scene("Grid Tunnel", "acid"));
	CHECK(got == want);
	CHECK(got.size() == 32u);

	// no preset: Synesthesia then uses its custom gold slot, so there is no second argument
	const std::vector<uint8_t> plain = encode(osc_scene("tunnel", ""));
	CHECK(plain == bytes({'/', 's', 'c', 'e', 'n',  'e',  's', '/',  't',  'u',
			      'n', 'n', 'e', 'l', 0x00, 0x00, ',', 0x00, 0x00, 0x00}));
	CHECK(plain.size() == 20u);
}

TEST_CASE("osc: golden bytes for every playlist, preset and favslot route")
{
	// /playlist/next (14 + NUL -> 16), ",", 0x00 * 3
	CHECK(encode(osc_playlist_next()) == bytes({'/', 'p', 'l', 'a', 'y',  'l',  'i', 's',  't',  '/',
						    'n', 'e', 'x', 't', 0x00, 0x00, ',', 0x00, 0x00, 0x00}));
	// /playlist/previous (18 + NUL -> 20)
	CHECK(encode(osc_playlist_previous()) ==
	      bytes({'/', 'p', 'l', 'a', 'y', 'l', 'i',  's',  't', '/',  'p',  'r',
		     'e', 'v', 'i', 'o', 'u', 's', 0x00, 0x00, ',', 0x00, 0x00, 0x00}));
	// /playlist/position 3 -> ",i" 0x00 0x00, 0x00 0x00 0x00 0x03
	CHECK(encode(osc_playlist_position(3)) ==
	      bytes({'/', 'p', 'l', 'a', 'y',  'l',  'i', 's', 't',  '/',  'p',  'o',  's',  'i',
		     't', 'i', 'o', 'n', 0x00, 0x00, ',', 'i', 0x00, 0x00, 0x00, 0x00, 0x00, 0x03}));
	// /playlist/select "set02" -> ",s" 0x00 0x00, "set02" 0x00 0x00 0x00
	CHECK(encode(osc_playlist_select("set02")) ==
	      bytes({'/',  'p',  'l',  'a',  'y', 'l', 'i',  's',  't', '/', 's', 'e', 'l', 'e',  'c',  't',
		     0x00, 0x00, 0x00, 0x00, ',', 's', 0x00, 0x00, 's', 'e', 't', '0', '2', 0x00, 0x00, 0x00}));
	// /presets "dreamy" (8 + NUL -> 12), ",s" 0x00 0x00, "dreamy" 0x00
	CHECK(encode(osc_preset("dreamy")) ==
	      bytes({'/', 'p', 'r',  'e',  's', 'e', 't', 's', 0x00, 0x00, 0x00, 0x00,
		     ',', 's', 0x00, 0x00, 'd', 'r', 'e', 'a', 'm',  'y',  0x00, 0x00}));
	// /favslots/2 (11 + NUL -> 12), an address-only message
	CHECK(encode(osc_favslot(2)) ==
	      bytes({'/', 'f', 'a', 'v', 's', 'l', 'o', 't', 's', '/', '2', 0x00, ',', 0x00, 0x00, 0x00}));
}

TEST_CASE("osc: golden bytes for every meta and global route")
{
	// /controls/global/bang/1 (23 + NUL -> 24), ",f", 1.0f
	CHECK(encode(osc_bang(1)) ==
	      bytes({'/', 'c', 'o', 'n', 't', 'r', 'o', 'l',  's', '/', 'g',  'l',  'o',  'b',  'a',  'l',
		     '/', 'b', 'a', 'n', 'g', '/', '1', 0x00, ',', 'f', 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00}));
	// /controls/meta/{name} f f f: /controls/meta/low_color (24 + NUL -> 28), ",fff\0" (8), 0.1f 0.2f 0.3f
	CHECK(encode(osc_meta_rgb("low_color", 0.1f, 0.2f, 0.3f)) ==
	      bytes({'/',  'c',  'o',  'n',  't',  'r',  'o',  'l',  's',  '/',  'm',  'e',  't',  'a',  '/',  'l',
		     'o',  'w',  '_',  'c',  'o',  'l',  'o',  'r',  0x00, 0x00, 0x00, 0x00, ',',  'f',  'f',  'f',
		     0x00, 0x00, 0x00, 0x00, 0x3D, 0xCC, 0xCC, 0xCD, 0x3E, 0x4C, 0xCC, 0xCD, 0x3E, 0x99, 0x99, 0x9A}));
	// /controls/meta/{name}/{r|g|b} f: /controls/meta/low_color/r (26 + NUL -> 28), ",f", 0.25f
	CHECK(encode(osc_meta_channel("low_color", 'r', 0.25f)) ==
	      bytes({'/', 'c', 'o',  'n',  't', 'r', 'o',  'l',  's',  '/',  'm',  'e',
		     't', 'a', '/',  'l',  'o', 'w', '_',  'c',  'o',  'l',  'o',  'r',
		     '/', 'r', 0x00, 0x00, ',', 'f', 0x00, 0x00, 0x3E, 0x80, 0x00, 0x00}));
	// /controls/meta/{name} f: /controls/meta/hue (18 + NUL -> 20), ",f", 0.5f
	CHECK(encode(osc_meta("hue", 0.5f)) ==
	      bytes({'/', 'c', 'o', 'n', 't',  'r',  'o', 'l', 's',  '/',  'm',  'e',  't',  'a',
		     '/', 'h', 'u', 'e', 0x00, 0x00, ',', 'f', 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00}));
	// /controls/global/{type}/{n} f: /controls/global/alpha/2 (24 + NUL -> 28), ",f", 0.5f
	CHECK(encode(osc_global("alpha", 2, 0.5f)) ==
	      bytes({'/',  'c',  'o',  'n',  't', 'r', 'o',  'l',  's',  '/',  'g',  'l',
		     'o',  'b',  'a',  'l',  '/', 'a', 'l',  'p',  'h',  'a',  '/',  '2',
		     0x00, 0x00, 0x00, 0x00, ',', 'f', 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00}));
	// /controls/{bank}/{name}/random: /controls/meta/hue/random (26 + NUL -> 28), address-only
	CHECK(encode(osc_control_random("meta", "hue")) ==
	      bytes({'/', 'c', 'o', 'n', 't', 'r', 'o', 'l', 's', '/',  'm',  'e',  't', 'a',  '/',  'h',
		     'u', 'e', '/', 'r', 'a', 'n', 'd', 'o', 'm', 0x00, 0x00, 0x00, ',', 0x00, 0x00, 0x00}));
}

TEST_CASE("osc: encode/decode round trip")
{
	const OscMessage in[] = {
		osc_playlist_next(),
		osc_playlist_previous(),
		osc_playlist_position(5),
		osc_playlist_select("setA"),
		osc_scene("Nebula Drift", "acid"),
		osc_preset("dreamy"),
		osc_favslot(3),
		osc_meta_rgb("low_color", 0.5f, 0.25f, 0.0f),
		osc_meta_channel("mid_color", 'g', 0.75f),
		osc_global("bang", 1, 1.0f),
		osc_control_random("meta", "hue"),
	};
	for (const OscMessage &m : in) {
		const std::vector<uint8_t> wire = encode(m);
		CHECK(wire.size() % 4u == 0u);
		OscMessage out;
		REQUIRE(osc_decode(wire, out));
		CHECK(out.address == m.address);
		REQUIRE(out.args.size() == m.args.size());
		for (size_t i = 0; i < m.args.size(); ++i) {
			CHECK(out.args[i].type == m.args[i].type);
			CHECK(out.args[i].s == m.args[i].s);
			CHECK(out.args[i].i == m.args[i].i);
			CHECK(out.args[i].f == doctest::Approx(m.args[i].f));
		}
	}
}

TEST_CASE("osc: mixed argument types keep their order and values")
{
	OscMessage m("/mix");
	m.add(7);
	m.add(-2.5f);
	m.add("text");
	const std::vector<uint8_t> wire = encode(m);
	CHECK(wire.size() % 4u == 0u);
	OscMessage out;
	REQUIRE(osc_decode(wire, out));
	CHECK(out.address == "/mix");
	REQUIRE(out.args.size() == 3u);
	CHECK(out.args[0].type == OscArg::Type::Int);
	CHECK(out.args[0].i == 7);
	CHECK(out.args[1].type == OscArg::Type::Float);
	CHECK(out.args[1].f == doctest::Approx(-2.5f));
	CHECK(out.args[2].type == OscArg::Type::String);
	CHECK(out.args[2].s == "text");
}

TEST_CASE("osc: what the encoder refuses")
{
	std::vector<uint8_t> sink;
	CHECK_FALSE(osc_encode(OscMessage(""), sink));                      // no address
	CHECK_FALSE(osc_encode(OscMessage("no-slash"), sink));              // OSC addresses start with '/'
	CHECK_FALSE(osc_encode(OscMessage(std::string("/a\0b", 4)), sink)); // embedded NUL
	OscMessage bad("/x");
	bad.add(std::string("a\0b", 3));
	CHECK_FALSE(osc_encode(bad, sink)); // embedded NUL in an argument
	CHECK(sink.empty());                // nothing is written on failure
}

TEST_CASE("osc: what the decoder rejects")
{
	OscMessage out;
	CHECK_FALSE(osc_decode(static_cast<const uint8_t *>(nullptr), 0, out));
	CHECK_FALSE(osc_decode(bytes({'/', 'x'}), out));                                    // shorter than a field
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00}), out));                              // short, no NUL terminator
	CHECK_FALSE(osc_decode(bytes({'x', 0x00, 0x00, 0x00}), out));                       // address without '/'
	CHECK_FALSE(osc_decode(bytes({0x00, 0x00, 0x00, 0x00}), out));                      // empty address
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00, 0x00, 'i', 0x00}), out));             // no type tag
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00, 0x00, 'i', 0x00, 0x00, 0x00}), out)); // tag without ','
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00, 0x00, ',', 'i', 0x00, 0x00, 0x01}), out));       // truncated arg
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00, 0x00, ',', 'q', 0x00, 0x00, 1, 2, 3, 4}), out)); // bad tag
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00, 0x00, ',', 's', 0x00, 0x00, 'a', 'b', 'c', 'd'}), out));
	CHECK_FALSE(osc_decode(bytes({'/', 'x', 0x00, 0x00, ',', 's', 0x00, 0x00}), out)); // string past the end

	// an address-only message: legal, no arguments
	REQUIRE(osc_decode(bytes({'/', 'x', 0x00, 0x00, ',', 0x00, 0x00, 0x00}), out));
	CHECK(out.address == "/x");
	CHECK(out.args.empty());

	// a sender that forgot the trailing padding still decodes
	REQUIRE(osc_decode(bytes({'/', 'x', 0x00, 0x00, ',', 's', 0x00, 0x00, 'h', 'i', 0x00}), out));
	REQUIRE(out.args.size() == 1u);
	CHECK(out.args[0].s == "hi");
}

TEST_CASE("osc: scene names are slugged for the wire, control names keep their underscores")
{
	CHECK(osc_slug("Hex Array") == "hexarray");
	CHECK(osc_slug("Grid-Tunnel_x") == "gridtunnelx");
	CHECK(osc_slug("Nebula  Drift") == "nebuladrift");
	CHECK(osc_slug("already") == "already");
	CHECK(osc_scene("Nebula Drift", "").address == "/scenes/nebuladrift");

	CHECK(osc_component("Low_Color") == "low_color");
	CHECK(osc_component("Mid Color") == "midcolor");
	CHECK(osc_component("a/b") == "ab");
	CHECK(osc_meta_rgb("Low_Color", 0.0f, 0.0f, 0.0f).address == "/controls/meta/low_color");
}
