#include "doctest.h"

#include <cstdint>
#include <string>

#include "midi_msg.h"

using namespace cd::midi;

namespace {

std::string hex(uint32_t v)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "%06X", v);
	return buf;
}

std::string bytes(uint32_t packed)
{
	char buf[24];
	snprintf(buf, sizeof(buf), "%02X %02X %02X", (unsigned)(packed & 0xFFu), (unsigned)((packed >> 8) & 0xFFu),
		 (unsigned)((packed >> 16) & 0xFFu));
	return buf;
}

} // namespace

TEST_CASE("clamping: channel, number and value")
{
	CHECK((int)clamp_channel(1) == 1);
	CHECK((int)clamp_channel(8) == 8);
	CHECK((int)clamp_channel(16) == 16);
	CHECK((int)clamp_channel(0) == 1);
	CHECK((int)clamp_channel(-7) == 1);
	CHECK((int)clamp_channel(17) == 16);
	CHECK((int)clamp_channel(9999) == 16);

	CHECK((int)clamp_number(0) == 0);
	CHECK((int)clamp_number(60) == 60);
	CHECK((int)clamp_number(127) == 127);
	CHECK((int)clamp_number(-5) == 0);
	CHECK((int)clamp_number(200) == 127);

	CHECK((int)clamp_value(0) == 0);
	CHECK((int)clamp_value(64) == 64);
	CHECK((int)clamp_value(127) == 127);
	CHECK((int)clamp_value(-1) == 0);
	CHECK((int)clamp_value(128) == 127);
}

TEST_CASE("DWORD packing: status, data 1, data 2")
{
	/* WinMM: byte 0 status, byte 1 data 1, byte 2 data 2 */
	const uint32_t on = pack(0x90, 60, 127);
	CHECK(bytes(on) == "90 3C 7F");
	CHECK(hex(on) == "7F3C90");

	CHECK(bytes(pack(0x80, 59, 0)) == "80 3B 00");
	CHECK(bytes(pack(0xB0, 7, 127)) == "B0 07 7F");
}

TEST_CASE("note on / note off / control change")
{
	/* channel 16 -> status nibble 0x?F, note 60 */
	CHECK(hex(encode_note_on(16, 60, 127)) == "7F3C9F");
	CHECK(bytes(encode_note_on(16, 60, 127)) == "9F 3C 7F");
	CHECK(bytes(encode_note_off(16, 60, 0)) == "8F 3C 00");
	CHECK(hex(encode_note_off(16, 60, 0)) == "003C8F");
	CHECK(bytes(encode_control(16, 7, 127)) == "BF 07 7F");

	/* channel 1, defaults */
	CHECK(bytes(encode_note_on(1, 0, 1)) == "90 00 01");
	CHECK(bytes(encode_control(1, 0, 0)) == "B0 00 00");
}

TEST_CASE("encode() follows the configured type and clamps everything")
{
	CHECK(encode(Type::Note, 16, 60, 127) == encode_note_on(16, 60, 127));
	CHECK(encode(Type::Control, 16, 7, 127) == encode_control(16, 7, 127));

	/* velocity 0 on a Note is the note-off convention */
	CHECK(encode(Type::Note, 16, 60, 0) == encode_note_off(16, 60, 0));

	/* out of range input is clamped, never sent as-is */
	CHECK(encode(Type::Note, 99, 999, 999) == encode_note_on(16, 127, 127));
	CHECK(encode(Type::Control, 0, -3, -9) == encode_control(1, 0, 0));
}

TEST_CASE("Next and Prev defaults")
{
	const MsgSpec next = next_msg_default();
	CHECK(next.type == Type::Note);
	CHECK(next.channel == 16);
	CHECK(next.number == 60);

	const MsgSpec prev = prev_msg_default();
	CHECK(prev.type == Type::Note);
	CHECK(prev.channel == 16);
	CHECK(prev.number == 59);

	CHECK(hex(next.wire()) == "7F3C9F"); /* Note on, ch 16, #60, velocity 127 */
	CHECK(hex(prev.wire()) == "7F3B9F"); /* Note on, ch 16, #59, velocity 127 */
}

TEST_CASE("a CC message always sends value 127 from wire()")
{
	MsgSpec m;
	m.type = Type::Control;
	m.channel = 3;
	m.number = 42;
	CHECK(bytes(m.wire()) == "B2 2A 7F");
	CHECK(m.wire() == encode_control(3, 42, 127));
}
