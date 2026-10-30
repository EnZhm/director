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

#include "palette-snapshot.h"

#include <cstdio>

namespace pd {

namespace {

float clamp01(float v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

} // namespace

std::string palette_colours(const struct pl_palette &p)
{
	int n = p.n_colours;
	if (n < 1)
		n = 1;
	if (n > PL_MAX_COLOURS)
		n = PL_MAX_COLOURS;

	std::string out;
	out.reserve(static_cast<std::size_t>(n) * 21); // "-0.0000,-0.0000,-0.0000;" upper bound
	char buf[48];
	for (int i = 0; i < n; ++i) {
		if (i)
			out += ';';
		std::snprintf(buf, sizeof(buf), "%.4f,%.4f,%.4f", clamp01(p.colours[i].r), clamp01(p.colours[i].g),
			      clamp01(p.colours[i].b));
		out += buf;
	}
	return out;
}

std::string palette_snapshot(struct pl_set set, int live)
{
	int l = live;
	int prev = -1;
	pl_sanitize_for_render(&set, &l, &prev);
	return palette_colours(set.palettes[l]);
}

} // namespace pd
