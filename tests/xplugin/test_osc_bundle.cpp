// osc_decode_packet: bare messages, OSC 1.0 bundles (recursive, depth cap), and the real
// Synesthesia 1.25.4.189 feedback captures (fixtures/*.hex), plus a fixed-seed fuzz.
#include "doctest.h"

#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "osc.h"

using namespace director_osc;

namespace {

std::vector<uint8_t> fixture(const std::string &name)
{
	std::ifstream f(std::string(XPLUGIN_FIXTURE_DIR) + "/" + name);
	REQUIRE(f.good());
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string hex = ss.str();
	std::vector<uint8_t> out;
	for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
		if (hex[i] == '\n' || hex[i] == '\r')
			break;
		out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
	}
	return out;
}

std::vector<uint8_t> msg(const std::string &addr)
{
	std::vector<uint8_t> out;
	REQUIRE(osc_encode(OscMessage(addr), out));
	return out;
}

void put_size(std::vector<uint8_t> &b, int32_t n)
{
	const uint32_t u = static_cast<uint32_t>(n);
	b.push_back(static_cast<uint8_t>(u >> 24));
	b.push_back(static_cast<uint8_t>(u >> 16));
	b.push_back(static_cast<uint8_t>(u >> 8));
	b.push_back(static_cast<uint8_t>(u));
}

std::vector<uint8_t> bundle(const std::vector<std::vector<uint8_t>> &elements)
{
	std::vector<uint8_t> b = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
	for (const std::vector<uint8_t> &e : elements) {
		put_size(b, static_cast<int32_t>(e.size()));
		b.insert(b.end(), e.begin(), e.end());
	}
	return b;
}

std::vector<uint8_t> nested(int depth, const std::vector<uint8_t> &inner)
{
	std::vector<uint8_t> b = inner;
	for (int i = 0; i < depth; ++i)
		b = bundle({b});
	return b;
}

} // namespace

TEST_CASE("Synesthesia 1.25.4.189 feedback captures decode to their /scenes address")
{
	std::vector<OscMessage> out;
	const std::vector<uint8_t> next = fixture("synesthesia-1.25.4.189-feedback-next.hex");
	REQUIRE(next.size() == 40u);
	REQUIRE(osc_decode_packet(next, out));
	REQUIRE(out.size() == 1u);
	CHECK(out[0].address == "/scenes/beamers");
	CHECK(out[0].args.empty());

	const std::vector<uint8_t> prev = fixture("synesthesia-1.25.4.189-feedback-previous.hex");
	REQUIRE(prev.size() == 44u);
	REQUIRE(osc_decode_packet(prev, out));
	REQUIRE(out.size() == 1u);
	CHECK(out[0].address == "/scenes/aliencavern");

	/* the bare decoder still rejects a bundle (unchanged behaviour) */
	OscMessage m;
	CHECK_FALSE(osc_decode(next, m));
}

TEST_CASE("a bare message decodes to one message")
{
	std::vector<OscMessage> out;
	REQUIRE(osc_decode_packet(msg("/scenes/fake"), out));
	REQUIRE(out.size() == 1u);
	CHECK(out[0].address == "/scenes/fake");
}

TEST_CASE("a bundle with two messages keeps packet order")
{
	std::vector<OscMessage> out;
	REQUIRE(osc_decode_packet(bundle({msg("/scenes/one"), msg("/scenes/two")}), out));
	REQUIRE(out.size() == 2u);
	CHECK(out[0].address == "/scenes/one");
	CHECK(out[1].address == "/scenes/two");
}

TEST_CASE("nested bundles unwrap recursively, in order")
{
	std::vector<OscMessage> out;
	const std::vector<uint8_t> two_deep = bundle({bundle({msg("/a"), msg("/b")}), msg("/c")});
	REQUIRE(osc_decode_packet(two_deep, out));
	REQUIRE(out.size() == 3u);
	CHECK(out[0].address == "/a");
	CHECK(out[1].address == "/b");
	CHECK(out[2].address == "/c");

	REQUIRE(osc_decode_packet(nested(8, msg("/deep")), out)); /* depth 8: the cap, accepted */
	REQUIRE(out.size() == 1u);
	CHECK(out[0].address == "/deep");
	CHECK_FALSE(osc_decode_packet(nested(9, msg("/deep")), out)); /* depth 9: rejected */
	CHECK(out.empty());
}

TEST_CASE("an empty bundle is valid and yields no messages")
{
	std::vector<OscMessage> out = {OscMessage("/stale")};
	REQUIRE(osc_decode_packet(bundle({}), out));
	CHECK(out.empty());
}

TEST_CASE("malformed bundles reject the whole packet and clear out")
{
	std::vector<OscMessage> out;
	const std::vector<uint8_t> good = bundle({msg("/scenes/one"), msg("/scenes/two")});

	SUBCASE("truncated header")
	{
		std::vector<uint8_t> b(good.begin(), good.begin() + 12);
		out = {OscMessage("/stale")};
		CHECK_FALSE(osc_decode_packet(b, out));
		CHECK(out.empty());
	}
	SUBCASE("size not a multiple of 4")
	{
		std::vector<uint8_t> b = bundle({});
		put_size(b, 6);
		b.insert(b.end(), {'/', 'x', 0, 0, ',', 0});
		CHECK_FALSE(osc_decode_packet(b, out));
	}
	SUBCASE("size past the end")
	{
		std::vector<uint8_t> b = bundle({});
		put_size(b, 64);
		const std::vector<uint8_t> m = msg("/x");
		b.insert(b.end(), m.begin(), m.end());
		CHECK_FALSE(osc_decode_packet(b, out));
	}
	SUBCASE("negative size")
	{
		std::vector<uint8_t> b = bundle({});
		put_size(b, -4);
		b.insert(b.end(), {'/', 'x', 0, 0});
		CHECK_FALSE(osc_decode_packet(b, out));
	}
	SUBCASE("zero size")
	{
		std::vector<uint8_t> b = bundle({});
		put_size(b, 0);
		CHECK_FALSE(osc_decode_packet(b, out));
	}
	SUBCASE("a bad second element rejects the first too")
	{
		std::vector<uint8_t> b = bundle({msg("/scenes/one"), {'x', 0, 0, 0}});
		out = {OscMessage("/stale")};
		CHECK_FALSE(osc_decode_packet(b, out));
		CHECK(out.empty());
	}
	SUBCASE("trailing bytes shorter than a size field")
	{
		std::vector<uint8_t> b = good;
		b.push_back(0);
		b.push_back(0);
		CHECK_FALSE(osc_decode_packet(b, out));
	}
	SUBCASE("null and empty input")
	{
		CHECK_FALSE(osc_decode_packet(nullptr, 0, out));
		CHECK_FALSE(osc_decode_packet(std::vector<uint8_t>(), out));
	}
}

TEST_CASE("fuzz: truncations and byte flips of the feedback captures never crash or overread")
{
	const std::vector<std::vector<uint8_t>> seeds = {
		fixture("synesthesia-1.25.4.189-feedback-next.hex"),
		fixture("synesthesia-1.25.4.189-feedback-previous.hex"),
		bundle({bundle({msg("/a"), msg("/b")}), msg("/c")}),
	};
	std::mt19937 rng(20261030u);
	std::vector<OscMessage> out;
	int cases = 0, accepted = 0;
	for (const std::vector<uint8_t> &seed : seeds) {
		for (std::size_t n = 0; n <= seed.size(); ++n) { /* every truncation, exact-size heap copy */
			std::vector<uint8_t> b(seed.begin(), seed.begin() + static_cast<std::ptrdiff_t>(n));
			if (osc_decode_packet(b, out))
				++accepted;
			++cases;
		}
	}
	for (int i = 0; i < 12000; ++i) {
		std::vector<uint8_t> b = seeds[static_cast<std::size_t>(i) % seeds.size()];
		const int flips = 1 + static_cast<int>(rng() % 4);
		for (int f = 0; f < flips; ++f)
			b[rng() % b.size()] = static_cast<uint8_t>(rng());
		if (rng() % 3 == 0)
			b.resize(rng() % (b.size() + 1));
		if (osc_decode_packet(b, out)) {
			++accepted;
			for (const OscMessage &m : out)
				CHECK(!m.address.empty());
		} else {
			CHECK(out.empty());
		}
		++cases;
	}
	CHECK(cases >= 10000);
	CHECK(accepted > 0);
}
