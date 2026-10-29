#pragma once

namespace md {

// GROOVE / BREAKDOWN / DROP, shared by the section detector and the clock snapshot.
// Numeric values match DIRECTOR_CLOCK_SECTION_* in include/director-clock.h.
enum class Section { Groove = 0, Breakdown = 1, Drop = 2 };

} // namespace md
