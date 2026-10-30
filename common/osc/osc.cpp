#include "osc.h"

#include <cctype>
#include <cstring>

namespace director_osc {

namespace {

void put_u32(std::vector<uint8_t> &out, uint32_t v)
{
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
	out.push_back(static_cast<uint8_t>(v & 0xFFu));
}

/* string + NUL, padded to a 4-byte boundary from the start of the string */
void put_str(std::vector<uint8_t> &out, const std::string &s)
{
	out.insert(out.end(), s.begin(), s.end());
	out.push_back(0);
	const std::size_t n = s.size() + 1;
	for (std::size_t i = n % 4; i != 0 && i < 4; ++i)
		out.push_back(0);
}

uint32_t get_u32(const uint8_t *d, std::size_t off)
{
	return (static_cast<uint32_t>(d[off]) << 24) | (static_cast<uint32_t>(d[off + 1]) << 16) |
	       (static_cast<uint32_t>(d[off + 2]) << 8) | static_cast<uint32_t>(d[off + 3]);
}

float bits_to_float(uint32_t bits)
{
	float f = 0.0f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

uint32_t float_to_bits(float f)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &f, sizeof(bits));
	return bits;
}

bool read_str(const uint8_t *d, std::size_t len, std::size_t &off, std::string &out)
{
	const std::size_t start = off;
	while (off < len && d[off] != 0)
		++off;
	if (off >= len)
		return false; /* not NUL-terminated */
	out.assign(reinterpret_cast<const char *>(d + start), off - start);
	off = (off + 1 + 3u) & ~static_cast<std::size_t>(3u);
	if (off > len) /* tolerate a sender that dropped the trailing padding */
		off = len;
	return true;
}

} // namespace

std::size_t osc_padded(std::size_t n)
{
	return (n + 3u) & ~static_cast<std::size_t>(3u);
}

OscArg OscArg::of_int(int32_t v)
{
	OscArg a;
	a.type = Type::Int;
	a.i = v;
	return a;
}

OscArg OscArg::of_float(float v)
{
	OscArg a;
	a.type = Type::Float;
	a.f = v;
	return a;
}

OscArg OscArg::of_string(std::string v)
{
	OscArg a;
	a.type = Type::String;
	a.s = std::move(v);
	return a;
}

OscMessage &OscMessage::add(int32_t v)
{
	args.push_back(OscArg::of_int(v));
	return *this;
}

OscMessage &OscMessage::add(float v)
{
	args.push_back(OscArg::of_float(v));
	return *this;
}

OscMessage &OscMessage::add(std::string v)
{
	args.push_back(OscArg::of_string(std::move(v)));
	return *this;
}

bool osc_encode(const OscMessage &m, std::vector<uint8_t> &out)
{
	if (m.address.empty() || m.address[0] != '/')
		return false;
	if (m.address.find('\0') != std::string::npos)
		return false;
	for (const OscArg &a : m.args)
		if (a.type == OscArg::Type::String && a.s.find('\0') != std::string::npos)
			return false;

	out.clear();
	put_str(out, m.address);
	std::string tags = ",";
	for (const OscArg &a : m.args)
		tags.push_back(a.type == OscArg::Type::Int ? 'i' : (a.type == OscArg::Type::Float ? 'f' : 's'));
	put_str(out, tags);
	for (const OscArg &a : m.args) {
		switch (a.type) {
		case OscArg::Type::Int:
			put_u32(out, static_cast<uint32_t>(a.i));
			break;
		case OscArg::Type::Float:
			put_u32(out, float_to_bits(a.f));
			break;
		case OscArg::Type::String:
			put_str(out, a.s);
			break;
		}
	}
	return true;
}

bool osc_decode(const uint8_t *data, std::size_t len, OscMessage &out)
{
	out = OscMessage();
	std::string tags;
	std::size_t off = 0;
	if (!data || len < 4)
		return false;
	if (!read_str(data, len, off, out.address))
		return false;
	if (out.address.empty() || out.address[0] != '/')
		return false;
	if (off >= len)
		return true; /* an address-only message is legal */
	if (!read_str(data, len, off, tags))
		return false;
	if (tags.empty() || tags[0] != ',')
		return false;

	for (std::size_t i = 1; i < tags.size(); ++i) {
		const char t = tags[i];
		if (off >= len)
			return false; /* fewer arguments than the tag string promises */
		if (t == 'i') {
			if (len - off < 4)
				return false;
			out.args.push_back(OscArg::of_int(static_cast<int32_t>(get_u32(data, off))));
			off += 4;
		} else if (t == 'f') {
			if (len - off < 4)
				return false;
			out.args.push_back(OscArg::of_float(bits_to_float(get_u32(data, off))));
			off += 4;
		} else if (t == 's') {
			std::string s;
			if (!read_str(data, len, off, s))
				return false;
			out.args.push_back(OscArg::of_string(std::move(s)));
		} else {
			return false; /* unsupported type */
		}
	}
	return true;
}

bool osc_decode(const std::vector<uint8_t> &data, OscMessage &out)
{
	return osc_decode(data.empty() ? nullptr : data.data(), data.size(), out);
}

namespace {

const uint8_t kBundleTag[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};

bool is_bundle(const uint8_t *data, std::size_t len)
{
	return len >= 8 && std::memcmp(data, kBundleTag, 8) == 0;
}

/* One packet or bundle element. depth 1 = the outermost bundle. */
bool decode_element(const uint8_t *data, std::size_t len, int depth, std::vector<OscMessage> &out)
{
	if (!data || len == 0)
		return false;
	if (!is_bundle(data, len)) {
		OscMessage m;
		if (!osc_decode(data, len, m))
			return false;
		out.push_back(std::move(m));
		return true;
	}
	if (depth > kOscMaxBundleDepth || len < 16) /* "#bundle\0" + 8-byte time tag */
		return false;
	std::size_t off = 16; /* the time tag is ignored: feedback is handled on arrival */
	while (off < len) {
		if (len - off < 4)
			return false;
		const int32_t size = static_cast<int32_t>(get_u32(data, off));
		off += 4;
		if (size <= 0 || (size % 4) != 0 || static_cast<std::size_t>(size) > len - off)
			return false;
		if (!decode_element(data + off, static_cast<std::size_t>(size), depth + 1, out))
			return false;
		off += static_cast<std::size_t>(size);
	}
	return true;
}

} // namespace

bool osc_decode_packet(const uint8_t *data, std::size_t len, std::vector<OscMessage> &out)
{
	out.clear();
	if (!decode_element(data, len, 1, out)) {
		out.clear();
		return false;
	}
	return true;
}

bool osc_decode_packet(const std::vector<uint8_t> &data, std::vector<OscMessage> &out)
{
	return osc_decode_packet(data.empty() ? nullptr : data.data(), data.size(), out);
}

std::string osc_slug(const std::string &name)
{
	std::string s;
	s.reserve(name.size());
	for (const char c : name) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (c == ' ' || c == '_' || c == '-' || c == '\t')
			continue;
		s.push_back(static_cast<char>(std::tolower(u)));
	}
	return s;
}

std::string osc_component(const std::string &name)
{
	std::string s;
	s.reserve(name.size());
	for (const char c : name) {
		const unsigned char u = static_cast<unsigned char>(c);
		if (std::isalnum(u) || c == '_')
			s.push_back(static_cast<char>(std::tolower(u)));
	}
	return s;
}

OscMessage osc_playlist_next()
{
	return OscMessage("/playlist/next");
}

OscMessage osc_playlist_previous()
{
	return OscMessage("/playlist/previous");
}

OscMessage osc_playlist_position(int index)
{
	OscMessage m("/playlist/position");
	m.add(index);
	return m;
}

OscMessage osc_playlist_select(const std::string &name)
{
	OscMessage m("/playlist/select");
	m.add(name);
	return m;
}

OscMessage osc_scene(const std::string &scene, const std::string &preset)
{
	OscMessage m("/scenes/" + osc_slug(scene));
	if (!preset.empty())
		m.add(preset);
	return m;
}

OscMessage osc_preset(const std::string &preset)
{
	OscMessage m("/presets");
	if (!preset.empty())
		m.add(preset);
	return m;
}

OscMessage osc_favslot(int slot)
{
	return OscMessage("/favslots/" + std::to_string(slot < 1 ? 1 : slot));
}

OscMessage osc_meta_rgb(const std::string &name, float r, float g, float b)
{
	OscMessage m("/controls/meta/" + osc_component(name));
	m.add(r);
	m.add(g);
	m.add(b);
	return m;
}

OscMessage osc_meta_channel(const std::string &name, char channel, float value)
{
	const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(channel)));
	OscMessage m("/controls/meta/" + osc_component(name) + "/" + std::string(1, c));
	m.add(value);
	return m;
}

OscMessage osc_global(const std::string &type, int slot, float value)
{
	OscMessage m("/controls/global/" + osc_component(type) + "/" + std::to_string(slot));
	m.add(value);
	return m;
}

OscMessage osc_control_random(const std::string &bank, const std::string &name)
{
	return OscMessage("/controls/" + osc_component(bank) + "/" + osc_component(name) + "/random");
}

OscMessage osc_bang(int position)
{
	return osc_global("bang", position < 1 ? 1 : position, 1.0f);
}

OscMessage osc_meta(const std::string &name, float value)
{
	OscMessage m("/controls/meta/" + osc_component(name));
	m.add(value);
	return m;
}

} // namespace director_osc
