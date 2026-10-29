#include "midi_msg.h"

#include <algorithm>

namespace cd {
namespace midi {

uint8_t clamp_channel(int channel)
{
	return (uint8_t)std::clamp(channel, 1, 16);
}

uint8_t clamp_number(int number)
{
	return (uint8_t)std::clamp(number, 0, 127);
}

uint8_t clamp_value(int value)
{
	return (uint8_t)std::clamp(value, 0, 127);
}

uint32_t pack(uint8_t status, uint8_t data1, uint8_t data2)
{
	return (uint32_t)status | ((uint32_t)data1 << 8) | ((uint32_t)data2 << 16);
}

uint32_t encode_note_on(int channel, int number, int velocity)
{
	return pack((uint8_t)(kNoteOn | (clamp_channel(channel) - 1u)), clamp_number(number), clamp_value(velocity));
}

uint32_t encode_note_off(int channel, int number, int velocity)
{
	return pack((uint8_t)(kNoteOff | (clamp_channel(channel) - 1u)), clamp_number(number), clamp_value(velocity));
}

uint32_t encode_control(int channel, int number, int value)
{
	return pack((uint8_t)(kControlChange | (clamp_channel(channel) - 1u)), clamp_number(number),
		    clamp_value(value));
}

uint32_t encode(Type type, int channel, int number, int value)
{
	if (type == Type::Control)
		return encode_control(channel, number, value);
	if (clamp_value(value) == 0)
		return encode_note_off(channel, number, 0); /* note on with velocity 0 is a note off */
	return encode_note_on(channel, number, value);
}

MsgSpec next_msg_default()
{
	MsgSpec m;
	m.type = Type::Note;
	m.channel = 16;
	m.number = 60;
	return m;
}

MsgSpec prev_msg_default()
{
	MsgSpec m;
	m.type = Type::Note;
	m.channel = 16;
	m.number = 59;
	return m;
}

} // namespace midi
} // namespace cd
