#pragma once
// Scene Director core: MIDI short-message encoding. Pure C++17, no WinMM.
//
// WinMM's midiOutShortMsg takes a DWORD: byte 0 = status, byte 1 = data 1, byte 2 = data 2.
// Everything is clamped, so a bad settings value can never send out of range bytes.
#include <cstdint>
#include <string>

#include "midi_map.h" // cd::midi::Type ("note" / "cc")

namespace cd {
namespace midi {

constexpr uint8_t kNoteOff = 0x80;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kControlChange = 0xB0;

// MIDI channel is 1..16 as musicians count it; note/controller numbers and values are 0..127.
uint8_t clamp_channel(int channel);
uint8_t clamp_number(int number);
uint8_t clamp_value(int value);

// Packs status, data 1 and data 2 into the WinMM DWORD layout.
uint32_t pack(uint8_t status, uint8_t data1, uint8_t data2);

uint32_t encode_note_on(int channel, int number, int velocity = 127);
uint32_t encode_note_off(int channel, int number, int velocity = 0);
uint32_t encode_control(int channel, int number, int value = 127);

// Encodes one configured message: Note -> note on with `value`; Control -> CC with `value`.
uint32_t encode(Type type, int channel, int number, int value = 127);

// One "Next" / "Prev" message as it is stored in the settings.
struct MsgSpec {
	Type type = Type::Note;
	int channel = 16; // 1..16
	int number = 60;  // 0..127

	uint32_t wire(int value = 127) const { return encode(type, channel, number, value); }
	bool operator==(const MsgSpec &o) const { return type == o.type && channel == o.channel && number == o.number; }
};

MsgSpec next_msg_default(); // Note, channel 16, #60
MsgSpec prev_msg_default(); // Note, channel 16, #59

} // namespace midi
} // namespace cd
