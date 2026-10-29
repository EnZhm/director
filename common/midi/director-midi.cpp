// Director suite: one WinMM MIDI wrapper (MIDI in for Music Director, MIDI out for Scene Director). See director-midi.h.
#include "director-midi.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>

#include <mmsystem.h>
#endif

namespace director_midi {

namespace {

#ifdef _WIN32
#ifdef UNICODE
std::string to_utf8(const wchar_t *wide)
{
	if (!wide || !*wide)
		return std::string();
	const int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (need <= 1)
		return std::string();
	std::string out((size_t)need - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], need, nullptr, nullptr);
	return out;
}
#endif

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}
#endif

#ifdef _WIN32
uint64_t now_ns()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		       std::chrono::steady_clock::now().time_since_epoch())
		.count();
}
#endif

} // namespace

// ---------------------------------------------------------------------------------------------
// input

struct Input::Impl {
#ifdef _WIN32
	HMIDIIN handle = nullptr;

	// CALLBACK_FUNCTION entry point: append only, nothing that can block or touch OBS.
	static void CALLBACK on_data(HMIDIIN device, UINT message, DWORD_PTR instance, DWORD_PTR param1,
				     DWORD_PTR param2);
#endif

	std::mutex mutex;
	std::vector<uint8_t> queue;
};

#ifdef _WIN32

void CALLBACK Input::Impl::on_data(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
{
	if (message != MIM_DATA)
		return; /* with no MIDI_IO_STATUS at open there is no long (sysex) data to handle */
	auto *impl = reinterpret_cast<Input::Impl *>(instance);
	if (!impl)
		return;

	const uint8_t status = (uint8_t)(param1 & 0xFFu);
	const uint8_t first = (uint8_t)((param1 >> 8) & 0xFFu);
	const uint8_t second = (uint8_t)((param1 >> 16) & 0xFFu);

	size_t length = 0;
	switch (status & 0xF0u) {
	case 0x80: /* note off */
	case 0x90: /* note on */
	case 0xA0: /* poly aftertouch */
	case 0xB0: /* control change */
	case 0xE0: /* pitch bend */
		length = 3;
		break;
	case 0xC0: /* program change */
	case 0xD0: /* channel aftertouch */
		length = 2;
		break;
	default:
		return; /* system messages are not MIDI Director material */
	}

	std::lock_guard<std::mutex> lock(impl->mutex);
	impl->queue.push_back(status);
	impl->queue.push_back(first);
	if (length > 2)
		impl->queue.push_back(second);
}

#endif

Input::Input() : impl_(new Impl()) {}

Input::~Input()
{
	close();
}

bool available()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

namespace {

#ifdef _WIN32
template<typename Caps> std::string caps_name(const Caps &caps)
{
#ifdef UNICODE
	return to_utf8(caps.szPname);
#else
	return caps.szPname;
#endif
}
#endif

} // namespace

std::vector<Device> input_devices()
{
	std::vector<Device> out;
#ifdef _WIN32
	const UINT count = midiInGetNumDevs();
	for (UINT i = 0; i < count; ++i) {
		MIDIINCAPS caps;
		ZeroMemory(&caps, sizeof(caps));
		if (midiInGetDevCaps(i, &caps, (UINT)sizeof(caps)) != MMSYSERR_NOERROR)
			continue;
		Device device;
		device.index = (int)i;
		device.name = caps_name(caps);
		if (device.name.empty())
			device.name = "MIDI input " + std::to_string(device.index);
		out.push_back(device);
	}
#endif
	return out;
}

std::vector<Device> output_devices()
{
	std::vector<Device> out;
#ifdef _WIN32
	const UINT count = midiOutGetNumDevs();
	for (UINT i = 0; i < count; ++i) {
		MIDIOUTCAPS caps;
		ZeroMemory(&caps, sizeof(caps));
		if (midiOutGetDevCaps(i, &caps, (UINT)sizeof(caps)) != MMSYSERR_NOERROR)
			continue;
		Device device;
		device.index = (int)i;
		device.name = caps_name(caps);
		if (device.name.empty())
			device.name = "MIDI output " + std::to_string(device.index);
		out.push_back(device);
	}
#endif
	return out;
}

bool Input::open(int device_index, std::string *error)
{
	close();
#ifdef _WIN32
	const UINT count = midiInGetNumDevs();
	if (device_index < 0 || (UINT)device_index >= count) {
		if (error)
			*error = "MIDI input " + std::to_string(device_index) + " is not available";
		return false;
	}
	HMIDIIN handle = nullptr;
	const MMRESULT opened = midiInOpen(&handle, (UINT)device_index,
					   reinterpret_cast<DWORD_PTR>(&Input::Impl::on_data),
					   reinterpret_cast<DWORD_PTR>(impl_.get()), CALLBACK_FUNCTION);
	if (opened != MMSYSERR_NOERROR || !handle) {
		if (error)
			*error = "midiInOpen failed (code " + std::to_string((int)opened) + ")";
		return false;
	}
	if (midiInStart(handle) != MMSYSERR_NOERROR) {
		midiInClose(handle);
		if (error)
			*error = "midiInStart failed";
		return false;
	}
	impl_->handle = handle;
	open_ = true;
	index_ = device_index;
	return true;
#else
	if (error)
		*error = "MIDI input is only available in the Windows build";
	(void)device_index;
	return false;
#endif
}

void Input::close()
{
#ifdef _WIN32
	if (impl_->handle) {
		/* stop, then reset: midiInReset waits for a running callback, so it cannot touch a
		 * closed handle afterwards */
		midiInStop(impl_->handle);
		midiInReset(impl_->handle);
		midiInClose(impl_->handle);
		impl_->handle = nullptr;
	}
#endif
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->queue.clear();
	open_ = false;
	index_ = -1;
}

void Input::drain(std::vector<uint8_t> &out)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->queue.empty())
		return;
	out.insert(out.end(), impl_->queue.begin(), impl_->queue.end());
	impl_->queue.clear();
}

// ---------------------------------------------------------------------------------------------
// output

struct Output::Impl {
	std::mutex mx; // handle, counters
	std::mutex off_mx;
	std::condition_variable off_cv;
	std::vector<std::pair<uint64_t, uint32_t>> offs; // due time (ns), packed note off
	std::string name;                                // the device currently open, "" when closed
	std::string wanted; // the device the settings asked for, kept across failed lookups
	bool open = false;
	bool stop = false;
	uint64_t sent = 0;
	uint64_t not_found = 0;
	uint32_t note_off_ms = 50;

#ifdef _WIN32
	HMIDIOUT handle = nullptr;
	std::thread worker;

	void worker_main();
	void send_raw(uint32_t message);
#endif
	void start_worker();
	void schedule_off(uint32_t message);
};

void Output::Impl::start_worker()
{
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> lk(off_mx);
		stop = false;
	}
	worker = std::thread([this] { worker_main(); });
#endif
}

void Output::Impl::schedule_off(uint32_t message)
{
#ifdef _WIN32
	std::lock_guard<std::mutex> lk(off_mx);
	if (stop)
		return;
	offs.emplace_back(now_ns() + (uint64_t)note_off_ms * 1000000ull, message);
	off_cv.notify_one();
#else
	(void)message;
#endif
}

#ifdef _WIN32

void Output::Impl::send_raw(uint32_t message)
{
	std::lock_guard<std::mutex> lk(mx);
	if (!handle)
		return;
	if (midiOutShortMsg(handle, message) == MMSYSERR_NOERROR)
		sent++;
}

void Output::Impl::worker_main()
{
	for (;;) {
		uint32_t message = 0;
		{
			std::unique_lock<std::mutex> lk(off_mx);
			if (stop)
				return;
			if (offs.empty()) {
				off_cv.wait(lk, [this] { return stop || !offs.empty(); });
				if (stop)
					return;
			}
			const auto earliest = std::min_element(offs.begin(), offs.end(),
							       [](const std::pair<uint64_t, uint32_t> &a,
								  const std::pair<uint64_t, uint32_t> &b) {
								       return a.first < b.first;
							       });
			const uint64_t now = now_ns();
			if (earliest->first > now) {
				/* wait for it, then re-check (a new, earlier note off may have arrived) */
				off_cv.wait_for(lk, std::chrono::nanoseconds(earliest->first - now));
				continue;
			}
			message = earliest->second;
			offs.erase(earliest);
		}
		send_raw(message);
	}
}

#endif

Output::Output() : impl_(new Impl())
{
	impl_->start_worker();
}

Output::~Output()
{
	close();
}

bool Output::open(const std::string &name, std::string *error)
{
	close();
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> lk(impl_->mx);
		impl_->wanted = name; /* "" = none selected; retries keep aiming at this name */
	}
	/* close() joined the note-off worker: start a fresh one */
	impl_->start_worker();
	if (name.empty()) {
		if (error)
			*error = "no MIDI output device selected";
		return false;
	}
	const std::vector<Device> found = output_devices();
	int index = -1;
	std::string actual;
	for (const Device &d : found) {
		if (d.name == name) {
			index = d.index;
			actual = d.name;
			break;
		}
	}
	if (index < 0) {
		const std::string want = lower(name);
		for (const Device &d : found) {
			if (lower(d.name) == want) {
				index = d.index;
				actual = d.name;
				break;
			}
		}
	}
	if (index < 0) {
		std::lock_guard<std::mutex> lk(impl_->mx);
		impl_->not_found++;
		if (error)
			*error = "MIDI output device '" + name + "' is not connected";
		return false;
	}
	HMIDIOUT handle = nullptr;
	const MMRESULT opened = midiOutOpen(&handle, (UINT)index, 0, 0, CALLBACK_NULL);
	if (opened != MMSYSERR_NOERROR || !handle) {
		std::lock_guard<std::mutex> lk(impl_->mx);
		impl_->not_found++;
		if (error)
			*error = "midiOutOpen failed (code " + std::to_string((int)opened) + ")";
		return false;
	}
	{
		std::lock_guard<std::mutex> lk(impl_->mx);
		impl_->handle = handle;
		impl_->name = actual;
		impl_->open = true;
		impl_->not_found = 0;
	}
	return true;
#else
	if (error)
		*error = "MIDI output is only available in the Windows build";
	(void)name;
	return false;
#endif
}

void Output::close()
{
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> lk(impl_->off_mx);
		impl_->stop = true;
		impl_->offs.clear();
	}
	impl_->off_cv.notify_all();
	if (impl_->worker.joinable())
		impl_->worker.join();
	{
		std::lock_guard<std::mutex> lk(impl_->mx);
		if (impl_->handle) {
			midiOutReset(impl_->handle);
			midiOutClose(impl_->handle);
			impl_->handle = nullptr;
		}
		impl_->open = false;
		impl_->name.clear();
	}
#else
	impl_->open = false;
	impl_->name.clear();
#endif
}

bool Output::is_open() const
{
	std::lock_guard<std::mutex> lk(impl_->mx);
#ifdef _WIN32
	return impl_->open && impl_->handle != nullptr;
#else
	return false;
#endif
}

std::string Output::device_name() const
{
	std::lock_guard<std::mutex> lk(impl_->mx);
	return impl_->name;
}

bool Output::ensure_open()
{
	std::string want;
	{
		std::lock_guard<std::mutex> lk(impl_->mx);
		want = impl_->wanted;
	}
	if (want.empty())
		return false;
	if (is_open()) {
		for (const Device &d : output_devices()) {
			if (d.name == want)
				return true;
		}
	}
	/* closed, or the device went away (hot-plug, a virtual port that restarted): try again */
	return open(want, nullptr);
}

uint64_t Output::failed_lookups() const
{
	std::lock_guard<std::mutex> lk(impl_->mx);
	return impl_->not_found;
}

uint32_t Output::note_off_ms() const
{
	std::lock_guard<std::mutex> lk(impl_->mx);
	return impl_->note_off_ms;
}

void Output::set_note_off_ms(uint32_t ms)
{
	std::lock_guard<std::mutex> lk(impl_->mx);
	impl_->note_off_ms = ms;
}

bool Output::send(uint32_t message)
{
#ifdef _WIN32
	{
		std::lock_guard<std::mutex> lk(impl_->mx);
		if (!impl_->handle)
			return false;
	}
	impl_->send_raw(message);
	return true;
#else
	(void)message;
	return false;
#endif
}

bool Output::send_with_note_off(uint32_t note_on, uint32_t note_off)
{
	if (!send(note_on))
		return false;
	impl_->schedule_off(note_off);
	return true;
}

uint64_t Output::sent() const
{
	std::lock_guard<std::mutex> lk(impl_->mx);
	return impl_->sent;
}

} // namespace director_midi
