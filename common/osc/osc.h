#pragma once
// Director suite: OSC 1.0 codec. Pure C++17, no libobs/Qt, no dependencies.
//
// Promoted from Clip Director 1.1.0 (plugins/clip-director/src/core/osc.{h,cpp} at 68cf066^),
// namespace director_osc.
//
// Open Sound Control 1.0 messages are big-endian and everything is padded to a 4-byte
// boundary: an address pattern (NUL-terminated string), a type tag string that starts with
// ',' and names the argument types, then the packed arguments. Only the three types the
// suite needs are supported (int32, float32, string); Synesthesia's routes use nothing else.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace director_osc {

struct OscArg {
	enum class Type { Int = 0, Float = 1, String = 2 };

	Type type = Type::Int;
	int32_t i = 0;
	float f = 0.0f;
	std::string s;

	static OscArg of_int(int32_t v);
	static OscArg of_float(float v);
	static OscArg of_string(std::string v);
};

struct OscMessage {
	std::string address;
	std::vector<OscArg> args;

	OscMessage() = default;
	explicit OscMessage(std::string addr) : address(std::move(addr)) {}
	OscMessage &add(int32_t v);
	OscMessage &add(float v);
	OscMessage &add(std::string v);
};

// A field is padded to the next multiple of 4 bytes (a string's NUL is part of its length).
std::size_t osc_padded(std::size_t n);

// false when there is nothing to send (empty address or an address with an embedded NUL).
bool osc_encode(const OscMessage &m, std::vector<uint8_t> &out);
// false on a malformed packet: short, address not starting with '/', unterminated string,
// truncated argument or a tag that does not name the arguments present.
bool osc_decode(const uint8_t *data, std::size_t len, OscMessage &out);
bool osc_decode(const std::vector<uint8_t> &data, OscMessage &out);

// A whole datagram: a bare message (-> 1 message) or an OSC 1.0 bundle ("#bundle\0", an 8-byte
// NTP time tag, then elements of a big-endian int32 size followed by a message or a nested
// bundle). Bundles are unwrapped recursively (depth cap kOscMaxBundleDepth); time tags are
// ignored; messages come back in packet order. Any malformed element rejects the whole packet
// (out cleared, false). An empty bundle is valid (true, no messages). Never reads past len.
constexpr int kOscMaxBundleDepth = 8;
bool osc_decode_packet(const uint8_t *data, std::size_t len, std::vector<OscMessage> &out);
bool osc_decode_packet(const std::vector<uint8_t> &data, std::vector<OscMessage> &out);

// Synesthesia scene names are lowercase with no spaces, underscores or hyphens:
// /scenes/{sceneName}, so "Grid Tunnel" goes out as /scenes/gridtunnel.
std::string osc_slug(const std::string &name);

// A single route component (a control name, a bank, a type): lowercase, keeps letters, digits
// and '_' so names like "low_color" survive, drops everything else (spaces, '-' and '/').
std::string osc_component(const std::string &name);

// Routes the suite sends (Synesthesia OSC docs). One helper per route in spec section 5.
OscMessage osc_playlist_next();                          // /playlist/next
OscMessage osc_playlist_previous();                      // /playlist/previous
OscMessage osc_playlist_position(int index);             // /playlist/position {int}
OscMessage osc_playlist_select(const std::string &name); // /playlist/select {string}
OscMessage osc_scene(const std::string &scene, const std::string &preset = std::string());
OscMessage osc_preset(const std::string &preset = std::string());                // /presets [{string}]
OscMessage osc_favslot(int slot);                                                // /favslots/{n}
OscMessage osc_meta_rgb(const std::string &name, float r, float g, float b);     // .../<name> f f f
OscMessage osc_meta_channel(const std::string &name, char channel, float value); // .../<name>/{rgb} f
OscMessage osc_global(const std::string &type, int slot, float value);           // /controls/global/{type}/{n} f
OscMessage osc_control_random(const std::string &bank, const std::string &name); // /controls/{bank}/{name}/random

// Kept from 1.1.0: /controls/global/bang/{n} with 1.0, and /controls/meta/{name} with one float.
OscMessage osc_bang(int position);
OscMessage osc_meta(const std::string &name, float value);

} // namespace director_osc
