/*
Music Director
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

#include "dock/dock-widgets.h"

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace mddock {

QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

void set_chip(QLabel *l, const QString &text, const QColor &tint)
{
	if (l->text() != text)
		l->setText(text);
	const QString ss = QString("QLabel{padding:3px 10px;border-radius:10px;font-weight:700;"
				   "background:rgba(%1,%2,%3,90);border:1px solid rgba(%1,%2,%3,200);}")
				   .arg(tint.red())
				   .arg(tint.green())
				   .arg(tint.blue());
	if (l->styleSheet() != ss)
		l->setStyleSheet(ss);
}

QLabel *help_label(const char *key, QWidget *parent)
{
	auto *l = new QLabel(T(key), parent);
	l->setWordWrap(true);
	QFont f = l->font();
	f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.88));
	l->setFont(f);
	l->setForegroundRole(QPalette::PlaceholderText);
	return l;
}

/* ---- 4 beat lights ---- */
BeatLights::BeatLights(QWidget *parent) : QWidget(parent)
{
	setFixedSize(4 * 20, 16);
}

void BeatLights::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	for (int i = 0; i < 4; i++) {
		p.setPen(Qt::NoPen);
		p.setBrush(i == on ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid));
		p.drawEllipse(QRectF(i * 20 + 2, 1, 14, 14));
	}
}

/* ---- audio meter ---- */
AudioMeter::AudioMeter(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(34);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AudioMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QPalette &pl = palette();
	const double W = width(), lamp = 26.0, lbl = 46.0;
	const double bw = std::max(40.0, W - lamp - lbl - 10.0);
	QFont f = font();
	f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.8));
	f.setBold(true);
	p.setFont(f);
	p.setPen(pl.color(QPalette::PlaceholderText));
	p.drawText(QRectF(0, 0, lbl, 15), Qt::AlignLeft | Qt::AlignVCenter, "LEVEL");
	p.drawText(QRectF(0, 18, lbl, 15), Qt::AlignLeft | Qt::AlignVCenter, "BASS");
	/* level: -60..0 dBFS */
	const QRectF lr(lbl, 2, bw, 11);
	p.fillRect(lr, pl.color(QPalette::Base));
	const double lv = std::clamp((static_cast<double>(db) + 60.0) / 60.0, 0.0, 1.0);
	const QColor lc = clip ? QColor(255, 60, 90) : (db > -6.0f ? QColor(255, 190, 40) : QColor(60, 200, 110));
	p.fillRect(QRectF(lr.left(), lr.top(), lr.width() * lv, lr.height()), lc);
	for (int t = -48; t < 0; t += 12) {
		const double x = lr.left() + lr.width() * (t + 60.0) / 60.0;
		p.fillRect(QRectF(x, lr.top(), 1, lr.height()), pl.color(QPalette::Mid));
	}
	p.setPen(pl.color(QPalette::Text));
	p.drawText(lr.adjusted(4, -1, -4, 1), Qt::AlignRight | Qt::AlignVCenter,
		   clip ? QString("CLIP %1 dBFS").arg(db, 0, 'f', 1)
			: (db <= -119.0f ? QString("no signal") : QString("%1 dBFS").arg(db, 0, 'f', 1)));
	/* bass bar with the kick threshold */
	const QRectF br(lbl, 19, bw, 11);
	p.fillRect(br, pl.color(QPalette::Base));
	p.fillRect(QRectF(br.left(), br.top(), br.width() * std::clamp(bass, 0.0f, 1.0f), br.height()),
		   pl.color(QPalette::Highlight));
	const double tx = br.left() + br.width() * std::clamp(thr, 0.0f, 1.0f);
	p.fillRect(QRectF(tx - 1, br.top() - 2, 2, br.height() + 4), QColor(255, 60, 90));
	/* kick light */
	const QRectF kr(W - lamp + 2, 4, lamp - 6, lamp - 6);
	p.setPen(QPen(pl.color(QPalette::Mid), 1));
	p.setBrush(kick_flash > 0 ? QColor(255, 60, 90) : pl.color(QPalette::Base));
	p.drawEllipse(kr);
	p.setPen(pl.color(QPalette::PlaceholderText));
	p.drawText(QRectF(W - lamp - 4, 22, lamp + 4, 12), Qt::AlignCenter, "KICK");
}

/* ---- 64-beat timeline ---- */
TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(96);
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const double W = width(), H = height();
	const QPalette &pl = palette();
	p.fillRect(rect(), pl.color(QPalette::Base));
	auto X = [&](double b) {
		return W - (now - b) / 64.0 * W;
	};
	const QColor sec[3] = {QColor(80, 140, 255, 46), QColor(255, 182, 72, 70), QColor(255, 92, 122, 80)};
	p.setRenderHint(QPainter::Antialiasing, false); // abutting translucent bands: no seams
	for (size_t i = 0; i < tl.size(); i++) {
		const int x0 = static_cast<int>(std::floor(X(tl[i].beat)));
		const int x1 = i + 1 < tl.size() ? static_cast<int>(std::floor(X(tl[i + 1].beat)))
						 : static_cast<int>(W);
		if (x1 <= x0)
			continue;
		p.fillRect(QRect(x0, 6, x1 - x0, static_cast<int>(H) - 6), sec[std::clamp(tl[i].section, 0, 2)]);
	}
	p.setRenderHint(QPainter::Antialiasing, true);
	QPainterPath bass;
	for (size_t i = 0; i < tl.size(); i++) {
		const double x = X(tl[i].beat);
		const double yb = H - 4 - std::min(1.0f, tl[i].bass) * H * 0.3;
		if (i == 0)
			bass.moveTo(x, yb);
		else
			bass.lineTo(x, yb);
	}
	QColor bc = pl.color(QPalette::Text);
	bc.setAlpha(90);
	p.setPen(QPen(bc, 1.0));
	p.drawPath(bass);
	QColor tc = pl.color(QPalette::Text);
	tc.setAlpha(140);
	const int ph = std::max(1, phrase);
	for (double b = std::ceil((now - 64.0) / ph) * ph; b <= now; b += ph)
		p.fillRect(QRectF(X(b), 6, 1, H), tc);
	p.setPen(QPen(pl.color(QPalette::Highlight), 2.0));
	p.drawLine(QPointF(W - 1, 0), QPointF(W - 1, H));
}

bool WheelGuard::eventFilter(QObject *o, QEvent *e)
{
	if (e->type() == QEvent::Wheel) {
		auto *w = qobject_cast<QWidget *>(o);
		if (w && !w->hasFocus()) {
			e->ignore();
			return true;
		}
	}
	return QObject::eventFilter(o, e);
}

void guard_wheel(QWidget *root, QObject *guard)
{
	for (QWidget *w : root->findChildren<QWidget *>()) {
		if (qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QComboBox *>(w) ||
		    qobject_cast<QAbstractSlider *>(w)) {
			w->setFocusPolicy(Qt::StrongFocus);
			w->installEventFilter(guard);
		}
	}
}

} // namespace mddock

#endif
