/*
Palette Director
Copyright (C) 2026 EnZhm

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>.
*/

/*
The pure half of the palette feed (spec 3.2): formatting a palette into the `colours` string of
the `palette_director_palette` proc, "r,g,b;r,g,b;...". No libobs, no Qt, no render state -- the
host glue in palette-lock-host.cpp takes a copy of the filter's set under its existing lock and
formats it here, so the proc never touches the render path.
*/

#pragma once

#include <string>

#include "palette/palette.h"

namespace pd {

/* api_version of the palette feed proc (fields are versioned by name: later versions may add
 * fields; existing ones keep their meaning). */
constexpr int kPaletteFeedApiVersion = 1;

/* One palette as "r,g,b;r,g,b;..." -- floats 0..1 with 4 decimals, in the palette's own order
 * (dark -> bright, as stored). Components outside 0..1 are clamped and the colour count is
 * clamped to [1, PL_MAX_COLOURS], so the string always satisfies the feed contract even for a
 * palette that never went through pl_sanitize_for_render(). */
std::string palette_colours(const struct pl_palette &p);

/* The live palette of `set`: a private copy goes through pl_sanitize_for_render() first (an empty
 * set becomes the built-in palette, the live index and every colour count are clamped, an unknown
 * mode is fixed), so the string describes what the render path would draw. `live` is the caller's
 * live index; an out-of-range index is clamped by the same sanitisation. */
std::string palette_snapshot(struct pl_set set, int live);

} // namespace pd
