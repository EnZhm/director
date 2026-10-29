#pragma once
// Scene Director: one WinMM MIDI wrapper for the whole plugin (input and output).
//
// Windows: WinMM. MIDI input is midiInGetNumDevs / midiInGetDevCaps / midiInOpen(CALLBACK_FUNCTION)
// / midiInStart / midiInStop / midiInClose; the device callback runs on WinMM's own thread and
// does one thing - append the bytes it received to a mutex-protected queue - so no hotkey is ever
// triggered off the MIDI thread. MIDI output is midiOutGetNumDevs / midiOutGetDevCaps /
// midiOutOpen(CALLBACK_NULL) / midiOutShortMsg / midiOutClose, opened by device name and re-resolved
// when the device goes away (a virtual port that starts after OBS, or a hot-plug). A Note message
// gets its note off a fixed delay later, from a small worker thread.
//
// Other platforms: a stub that reports no devices, so the plugin and the test suite build
// everywhere.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace director_midi {

struct Device {
	std::string name;
	int index = 0; // device index in this backend
};

// True when this build has a real MIDI backend (Windows only for now).
bool available();

// Devices the backend can open; empty when the platform has none.
std::vector<Device> input_devices();
std::vector<Device> output_devices();

// -------------------------------------------------------------------------------------------
// input

class Input {
public:
	Input();
	~Input();
	Input(const Input &) = delete;
	Input &operator=(const Input &) = delete;

	// Opens (and starts) one input device. Any previously open device is closed first.
	bool open(int device_index, std::string *error = nullptr);
	void close();
	bool is_open() const { return open_; }
	int device_index() const { return index_; }

	// Appends the bytes received since the last call to `out` (in arrival order).
	void drain(std::vector<uint8_t> &out);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
	bool open_ = false;
	int index_ = -1;
};

// -------------------------------------------------------------------------------------------
// output

class Output {
public:
	Output();
	~Output();
	Output(const Output &) = delete;
	Output &operator=(const Output &) = delete;

	// Opens the device with this name (exact match first, then case-insensitive). Any previously
	// open device is closed first.
	bool open(const std::string &name, std::string *error = nullptr);
	void close();
	bool is_open() const;
	// The device currently open (empty when closed).
	std::string device_name() const;

	// Re-resolves the configured device and opens it again when it is not open (or went away).
	// Quiet: no logging, and the failure count is what the dock shows.
	bool ensure_open();
	// How many times a name lookup failed since the last successful open.
	uint64_t failed_lookups() const;

	// Raw `midiOutShortMsg` payload, already packed (status | data1 << 8 | data2 << 16).
	bool send(uint32_t message);
	// Note on, then the matching note off after `note_off_ms()`; false when the device is closed.
	bool send_with_note_off(uint32_t note_on, uint32_t note_off);

	uint32_t note_off_ms() const;
	void set_note_off_ms(uint32_t ms);

	uint64_t sent() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace director_midi
