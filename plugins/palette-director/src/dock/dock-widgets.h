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
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/*
Shared dock widgets for the Palette Director docks (v1.2).

The Music dock took the audio/visualiser widgets out of the Palette Director dock, so the
beat lights, the level/bass meter, the 64-beat timeline and their helpers live here once and
are used by both docks. Everything runs on the UI thread.
*/

#pragma once

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <obs-module.h>

#include <QColor>
#include <QLabel>
#include <QObject>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

#include "palette-lock-host.hpp"

namespace pddock {

QString T(const char *key);
extern const QString kDot;

/* coloured status chip: tinted background over the theme, theme text colour */
void set_chip(QLabel *l, const QString &text, const QColor &tint);

QLabel *help_label(const char *key, QWidget *parent);

/* ---- 4 beat lights ---- */
class BeatLights : public QWidget {
public:
	explicit BeatLights(QWidget *parent);
	int on = -1;

protected:
	void paintEvent(QPaintEvent *) override;
};

/* ---- audio meter: input level (dBFS after gain, CLIP > -1 dBFS), bass bar with the kick
 * threshold marked, kick light. Fed from the DockView snapshot. */
class AudioMeter : public QWidget {
public:
	explicit AudioMeter(QWidget *parent);
	float db = -120.0f, bass = 0.0f, thr = 0.4f;
	bool clip = false, silent = true;
	int kick_flash = 0; // frames left to light the kick lamp

protected:
	void paintEvent(QPaintEvent *) override;
};

/* ---- 64-beat timeline ---- */
class TimelineWidget : public QWidget {
public:
	explicit TimelineWidget(QWidget *parent);

	std::vector<pdhost::TimelineSample> tl;
	double now = 0.0;
	int phrase = 32;

protected:
	void paintEvent(QPaintEvent *) override;
};

/* In a scrolling dock, a wheel over a spin box / combo / slider must scroll the dock, not
 * silently change a setting. Such widgets only take the wheel once they have focus. */
class WheelGuard : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *o, QEvent *e) override;
};

void guard_wheel(QWidget *root, QObject *guard);

/* RAII strong reference to the selected filter source */
struct SrcRef {
	obs_source_t *s = nullptr;
	explicit SrcRef(uint64_t id);
	~SrcRef();
	SrcRef(const SrcRef &) = delete;
	SrcRef &operator=(const SrcRef &) = delete;
};

} // namespace pddock

#endif
