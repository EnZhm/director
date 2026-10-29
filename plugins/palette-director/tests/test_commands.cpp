#include "doctest.h"

#include <string>
#include <vector>

#include "palette-director.h"
#include "test_helpers.h"

using namespace pdtest;

// Palette commands (force on/off, pause, panic) driven through the follower core. Commands are
// queued with push_command and drained on the next clock sample, exactly as the worker does it.

namespace {
std::vector<pd::ClockSample> drain_samples()
{
	return pdtest::script(4, 32);
}
void drain(pd::PaletteDirector &d)
{
	pdtest::run(d, drain_samples());
}
} // namespace

TEST_CASE("force off (hold) and release")
{
	pd::SetFile s = pdtest::make_set(2);
	s.director.random_off = false;
	pd::PaletteDirector d(s);

	pdtest::run(d, drain_samples()); // establish following

	d.push_command(pd::Command::ForceOff);
	drain(d);
	CHECK(d.snapshot().mix_target == 0.0f);
	CHECK(d.snapshot().lock_on == false);

	d.push_command(pd::Command::ReleaseForce);
	drain(d);
	CHECK(d.snapshot().mix_target == 1.0f);
	CHECK(d.snapshot().lock_on == true);
}

TEST_CASE("force on holds the lock on")
{
	pd::SetFile s = pdtest::make_set(2);
	s.director.random_off = true;
	s.director.random_chance = 1.0f;
	pd::PaletteDirector d(s);

	d.push_command(pd::Command::ForceOn);
	drain(d);
	CHECK(d.snapshot().mix_target == 1.0f);
	// even with random off at chance 1.0, forced on keeps the lock on
	pdtest::run(d, pdtest::script(64, 32));
	CHECK(d.snapshot().mix_target == 1.0f);
	CHECK(d.snapshot().lock_on == true);
}

TEST_CASE("panic: lock on, palette held, rotation reset")
{
	pd::SetFile s = pdtest::make_set(3);
	s.director.rotation = true;
	s.director.random_off = false;
	pd::PaletteDirector d(s);

	// advance one phrase so rotation increments
	pdtest::run(d, pdtest::script(40, 32));
	CHECK(d.snapshot().rotation_offset == 1);

	const int before = d.snapshot().live_palette;
	d.push_command(pd::Command::Panic);
	drain(d);
	CHECK(d.snapshot().rotation_offset == 0);
	CHECK(d.snapshot().lock_on == true);
	CHECK(d.snapshot().mix_target == 1.0f);
	CHECK(d.snapshot().live_palette == before);

	// director paused -> no further automatic changes
	const std::size_t n_ev = d.events().size();
	pdtest::run(d, pdtest::script(64, 32));
	CHECK(d.events().size() == n_ev); // no new decisions
}

TEST_CASE("pause holds state; resume continues")
{
	pd::SetFile s = pdtest::make_set(3);
	s.palette_changes.trigger = pd::ChangeTrigger::Phrases;
	s.palette_changes.every_phrases = 1;
	s.palette_changes.order = pd::ChangeOrder::Sequence;
	s.director.phrase_beats = 32;
	s.director.random_off = false;
	pd::PaletteDirector d(s);

	pdtest::run(d, pdtest::script(64, 32)); // changes at beat 32 and 64
	const std::size_t n1 = pdtest::palette_changes(d.events()).size();
	CHECK(n1 >= 1);

	d.push_command(pd::Command::Pause);
	drain(d);
	pdtest::run(d, pdtest::script(32, 32));
	CHECK(pdtest::palette_changes(d.events()).size() == n1); // no change while paused

	d.push_command(pd::Command::Resume);
	drain(d);
	pdtest::run(d, pdtest::script(128, 32)); // fresh phrase boundaries -> changes resume
	CHECK(pdtest::palette_changes(d.events()).size() > n1);
}

// ---- v1.0.1: PANIC is a toggle (user report: "the panic lock is stuck on") ----

TEST_CASE("v1.0.1 panic -> panic releases")
{
	pd::SetFile s = pdtest::make_set(3);
	s.director.random_off = false;
	pd::PaletteDirector d(s);
	d.push_command(pd::Command::Panic);
	drain(d);
	CHECK(d.snapshot().panic);
	CHECK(d.snapshot().paused);
	CHECK(d.snapshot().mix_target == 1.0f);
	d.push_command(pd::Command::Panic);
	drain(d);
	CHECK_FALSE(d.snapshot().panic);
	CHECK_FALSE(d.snapshot().paused);
	CHECK_FALSE(d.snapshot().forced_on);
	CHECK_FALSE(d.snapshot().force_off);
	bool logged = false;
	for (const auto &e : d.events())
		logged = logged || e.detail == "PANIC released: director resumed";
	CHECK(logged);
}

TEST_CASE("v1.0.1 panic -> resume releases")
{
	pd::SetFile s = pdtest::make_set(3);
	pd::PaletteDirector d(s);
	d.push_command(pd::Command::Panic);
	drain(d);
	d.push_command(pd::Command::Resume);
	drain(d);
	CHECK_FALSE(d.snapshot().panic);
	CHECK_FALSE(d.snapshot().paused);
}

TEST_CASE("v1.0.1 panic -> force on -> resume leaves no forced lock")
{
	pd::SetFile s = pdtest::make_set(3);
	pd::PaletteDirector d(s);
	d.push_command(pd::Command::Panic);
	d.push_command(pd::Command::ForceOn);
	drain(d);
	CHECK(d.snapshot().forced_on);
	CHECK(d.snapshot().mix_target == 1.0f);
	d.push_command(pd::Command::Resume);
	drain(d);
	CHECK_FALSE(d.snapshot().panic);
	CHECK_FALSE(d.snapshot().paused);
	CHECK_FALSE(d.snapshot().forced_on);
	// force on held before PANIC is cleared too
	d.push_command(pd::Command::ForceOn);
	d.push_command(pd::Command::Panic);
	d.push_command(pd::Command::Panic);
	drain(d);
	CHECK_FALSE(d.snapshot().forced_on);
}

TEST_CASE("v1.0.1 panic holds the lock on even while force off is held")
{
	pd::SetFile s = pdtest::make_set(3);
	pd::PaletteDirector d(s);
	d.push_command(pd::Command::Panic);
	d.push_command(pd::Command::ForceOff);
	drain(d);
	CHECK(d.snapshot().mix_target == 1.0f);
	d.push_command(pd::Command::Panic); // release clears the hold too
	drain(d);
	CHECK(d.snapshot().mix_target == 1.0f);
	CHECK_FALSE(d.snapshot().force_off);
}

TEST_CASE("v1.0.1 random off resumes after PANIC is released")
{
	pd::SetFile s = pdtest::make_set(3);
	s.director.random_off = true;
	s.director.random_chance = 1.0f;
	s.director.cooldown_phrases = 0;
	s.director.phrase_beats = 16;
	pd::PaletteDirector d(s);
	d.push_command(pd::Command::Panic);
	drain(d);
	pdtest::run(d, pdtest::script(64, 16));
	CHECK(pdtest::lock_offs(d.events()).empty()); // held on while panicked
	CHECK(d.snapshot().mix_target == 1.0f);
	d.push_command(pd::Command::Panic);
	drain(d);
	pdtest::run(d, pdtest::script(128, 16));
	CHECK_FALSE(pdtest::lock_offs(d.events()).empty()); // director lets go again
}
