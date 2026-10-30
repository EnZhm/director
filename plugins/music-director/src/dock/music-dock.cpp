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

/*
Music dock (dock id "music-director-dock"). Everything about *hearing* the music lives here: the beat
lights, BPM/confidence, phrase.bar.beat, the section chip, the level/bass meter, the 64-beat timeline
and the beat-matching controls (audio source, input gain, kick sensitivity, fallback BPM, phrase
length). It also carries the clock commands (Tap tempo, Phrase is now, Drop now, Force breakdown,
Lock BPM, Nudge).

There is exactly ONE clock (Music Director is a singleton), so there is no filter/instance picker:
the dock reads mdhost::view() and writes settings back with mdhost::set_settings, which stores them in
the scene collection under "music_director". It also shows the migrated Palette Lock values.
*/

#include "dock/music-dock.h"

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QComboBox>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include <plugin-support.h>

#include "dock/dock-widgets.h"
#include "host/music-clock-host.h"

using namespace mddock;

namespace {

/* slider with a live value label; model value = slider / scale */
struct ValueSlider {
	QSlider *s = nullptr;
	QLabel *v = nullptr;
	double scale = 100.0;
	bool percent = true;
	void show_value() const
	{
		if (percent)
			v->setText(QString("%1%").arg(s->value()));
		else
			v->setText(QString::number(s->value() / scale, 'f', scale <= 2.0 ? 1 : 2));
	}
};

bool enum_audio_list(void *param, obs_source_t *src)
{
	auto *out = static_cast<QStringList *>(param);
	if ((obs_source_get_output_flags(src) & OBS_SOURCE_AUDIO) == 0)
		return true;
	const char *n = obs_source_get_name(src);
	if (n && n[0])
		*out << QString::fromUtf8(n);
	return true;
}

class MusicDock : public QWidget {
public:
	explicit MusicDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		setObjectName("MusicDirectorDock");
		auto *outer = new QVBoxLayout(this);
		outer->setContentsMargins(0, 0, 0, 0);
		auto *scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scroll->setFrameShape(QFrame::NoFrame);
		outer->addWidget(scroll);
		body = new QWidget(scroll);
		scroll->setWidget(body);
		auto *v = new QVBoxLayout(body);
		v->setContentsMargins(8, 8, 8, 8);
		v->setSpacing(8);

		build_ui(v);
		v->addStretch(1);
		guard_wheel(body, new WheelGuard(this));

		timer = new QTimer(this);
		timer->setInterval(33); // ~30 Hz
		QObject::connect(timer, &QTimer::timeout, this, [this] { poll(); });
	}

protected:
	void showEvent(QShowEvent *e) override
	{
		QWidget::showEvent(e);
		obs_log(LOG_INFO, "music dock shown: 30 Hz poll started");
		timer->start();
		poll();
	}

	void hideEvent(QHideEvent *e) override
	{
		QWidget::hideEvent(e);
		timer->stop();
		obs_log(LOG_INFO, "music dock hidden: poll stopped (the clock keeps running)");
	}

	/* ---------------------------------------------------------------- UI */
	void build_ui(QVBoxLayout *v)
	{
		/* big BPM, phrase.bar.beat, section chip (+ beat lights) */
		auto *statRow = new QHBoxLayout();
		auto *bpmCol = new QVBoxLayout();
		bpmCol->setSpacing(0);
		bpmLbl = new QLabel(body);
		QFont bf = bpmLbl->font();
		bf.setPointSizeF(bf.pointSizeF() * 1.9);
		bf.setBold(true);
		bpmLbl->setFont(bf);
		confLbl = new QLabel(body);
		confLbl->setForegroundRole(QPalette::PlaceholderText);
		bpmCol->addWidget(bpmLbl);
		bpmCol->addWidget(confLbl);
		statRow->addLayout(bpmCol);
		statRow->addSpacing(14);
		auto *posCol = new QVBoxLayout();
		posCol->setSpacing(0);
		posLbl = new QLabel(body);
		QFont pf = posLbl->font();
		pf.setPointSizeF(pf.pointSizeF() * 1.9);
		pf.setBold(true);
		posLbl->setFont(pf);
		auto *posCap = new QLabel(T("Music.Position"), body);
		posCap->setForegroundRole(QPalette::PlaceholderText);
		posCol->addWidget(posLbl);
		posCol->addWidget(posCap);
		statRow->addLayout(posCol);
		statRow->addStretch(1);
		auto *secCol = new QVBoxLayout();
		secCol->setSpacing(3);
		secChip = new QLabel(body);
		secFor = new QLabel(body);
		secFor->setForegroundRole(QPalette::PlaceholderText);
		beats = new BeatLights(body);
		secCol->addWidget(secChip, 0, Qt::AlignRight);
		secCol->addWidget(secFor, 0, Qt::AlignRight);
		secCol->addWidget(beats, 0, Qt::AlignRight);
		statRow->addLayout(secCol);
		v->addLayout(statRow);

		/* meter + timeline */
		meter = new AudioMeter(body);
		meter->setToolTip(T("Dock.MeterHelp"));
		v->addWidget(meter);
		timeline = new TimelineWidget(body);
		v->addWidget(timeline);
		legend = new QLabel(T("Dock.TimelineLegend"), body);
		legend->setForegroundRole(QPalette::PlaceholderText);
		legend->setWordWrap(true);
		v->addWidget(legend);
		nowLbl = new QLabel(body);
		nowLbl->setWordWrap(true);
		QFont nf = nowLbl->font();
		nf.setBold(true);
		nowLbl->setFont(nf);
		nowLbl->setFrameShape(QFrame::StyledPanel);
		nowLbl->setContentsMargins(6, 4, 6, 4);
		v->addWidget(nowLbl);
		audioStatus = new QLabel(body);
		audioStatus->setForegroundRole(QPalette::PlaceholderText);
		audioStatus->setWordWrap(true);
		v->addWidget(audioStatus);
		migrated = new QLabel(body);
		migrated->setWordWrap(true);
		v->addWidget(migrated);

		/* beat matching */
		auto *hdr = new QLabel(T("Music.BeatMatching"), body);
		hdr->setForegroundRole(QPalette::PlaceholderText);
		v->addWidget(hdr);

		auto *row = new QHBoxLayout();
		row->addWidget(new QLabel(T("AudioSource"), body));
		drAudio = new QComboBox(body);
		drAudio->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		row->addWidget(drAudio, 1);
		v->addLayout(row);
		QObject::connect(drAudio, &QComboBox::activated, this, [this](int i) {
			mdhost::Settings s = model;
			s.audio_source = drAudio->itemData(i).toString().toStdString();
			commit(s, "audio source");
		});

		drGain =
			make_slider(v, "InputGain", "InputGain.Help", -24, 48, false, 2.0, "input gain dB",
				    [](mdhost::Settings &s, double val) { s.input_gain_db = static_cast<float>(val); });
		drSens = make_slider(v, "KickSensitivity", "KickSensitivity.Help", 0, 100, true, 100.0,
				     "kick sensitivity", [](mdhost::Settings &s, double val) {
					     s.kick_sensitivity = static_cast<float>(val);
				     });

		auto *row2 = new QHBoxLayout();
		row2->addWidget(new QLabel(T("FallbackBpm"), body));
		drBpm = new QSpinBox(body);
		drBpm->setRange(60, 200);
		row2->addWidget(drBpm, 1);
		row2->addWidget(new QLabel(T("PhraseLength"), body));
		drPhrase = new QComboBox(body);
		drPhrase->addItem("16 beats", 16);
		drPhrase->addItem("32 beats", 32);
		row2->addWidget(drPhrase, 1);
		v->addLayout(row2);
		drEnabled = new QPushButton(T("Dock.Enabled"), body);
		drEnabled->setCheckable(true);
		drEnabled->setToolTip(T("Dock.EnabledHelp"));
		v->addWidget(drEnabled);
		QObject::connect(drBpm, &QSpinBox::valueChanged, this, [this](int n) {
			if (filling)
				return;
			mdhost::Settings s = model;
			s.fallback_bpm = static_cast<float>(n);
			commit(s, QString("fallback BPM -> %1").arg(n));
		});
		QObject::connect(drPhrase, &QComboBox::activated, this, [this](int i) {
			mdhost::Settings s = model;
			s.phrase_beats = drPhrase->itemData(i).toInt();
			commit(s, "phrase length -> " + drPhrase->itemText(i));
		});
		QObject::connect(drEnabled, &QPushButton::toggled, this, [this](bool on) {
			if (filling)
				return;
			mdhost::Settings s = model;
			s.enabled = on;
			commit(s, on ? "enabled" : "disabled (clock held)");
		});

		/* clock commands */
		auto *cmds = new QGridLayout();
		cmds->setSpacing(6);
		bTap = new QPushButton(T("Music.TapTempo"), body);
		bPhrase = new QPushButton(T("Music.PhraseNow"), body);
		bLock = new QPushButton(T("Music.LockBpm"), body);
		bLock->setCheckable(true);
		bNudgeBack = new QPushButton(T("Music.NudgeBack"), body);
		bNudgeFwd = new QPushButton(T("Music.NudgeFwd"), body);
		bBreakdown = new QPushButton(T("Music.ForceBreakdown"), body);
		bDrop = new QPushButton(T("Music.DropNow"), body);
		for (QPushButton *b : {bTap, bPhrase, bLock, bNudgeBack, bNudgeFwd, bBreakdown})
			b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		cmds->addWidget(bTap, 0, 0);
		cmds->addWidget(bPhrase, 0, 1);
		cmds->addWidget(bLock, 0, 2);
		cmds->addWidget(bNudgeBack, 1, 0);
		cmds->addWidget(bNudgeFwd, 1, 1);
		cmds->addWidget(bBreakdown, 1, 2);
		v->addLayout(cmds);
		bDrop->setStyleSheet("QPushButton{border:2px solid rgba(255,70,100,220);font-weight:700;padding:5px;}"
				     "QPushButton:hover{border-color:rgba(255,70,100,255);}");
		v->addWidget(bDrop);
		v->addWidget(help_label("Music.CommandsHelp", body));

		QObject::connect(bTap, &QPushButton::clicked, this, [this] { send(mdhost::Cmd::TapTempo); });
		QObject::connect(bPhrase, &QPushButton::clicked, this, [this] { send(mdhost::Cmd::PhraseNow); });
		/* the checked state follows the clock in poll(), so hotkey and tap toggles show too */
		QObject::connect(bLock, &QPushButton::clicked, this, [this] { send(mdhost::Cmd::LockBpm); });
		QObject::connect(bNudgeBack, &QPushButton::clicked, this, [this] { send(mdhost::Cmd::NudgeBack); });
		QObject::connect(bNudgeFwd, &QPushButton::clicked, this, [this] { send(mdhost::Cmd::NudgeFwd); });
		QObject::connect(bBreakdown, &QPushButton::clicked, this,
				 [this] { send(mdhost::Cmd::ForceBreakdown); });
		QObject::connect(bDrop, &QPushButton::clicked, this, [this] { send(mdhost::Cmd::DropNow); });
	}

private:
	/* ---------------------------------------------------------------- small helpers */
	ValueSlider make_slider(QVBoxLayout *v, const char *label, const char *help, int lo, int hi, bool percent,
				double scale, const char *log_name, std::function<void(mdhost::Settings &, double)> set)
	{
		ValueSlider vs;
		vs.scale = scale;
		vs.percent = percent;
		auto *cap = new QLabel(T(label), body);
		auto *srow = new QHBoxLayout();
		vs.s = new QSlider(Qt::Horizontal, body);
		vs.s->setRange(lo, hi);
		vs.v = new QLabel(body);
		vs.v->setMinimumWidth(52);
		vs.v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		srow->addWidget(vs.s, 1);
		srow->addWidget(vs.v);
		auto *box = new QVBoxLayout();
		box->setSpacing(1);
		box->addWidget(cap);
		box->addLayout(srow);
		box->addWidget(help_label(help, body));
		v->addLayout(box);
		const ValueSlider copy = vs;
		QObject::connect(vs.s, &QSlider::valueChanged, this, [this, copy, set, log_name](int val) {
			copy.show_value();
			if (filling)
				return;
			mdhost::Settings s = model;
			set(s, val / copy.scale);
			commit(s,
			       copy.s->isSliderDown() ? QString() : QString("%1 -> %2").arg(log_name, copy.v->text()));
		});
		return vs;
	}

	static void set_slider(const ValueSlider &vs, double val)
	{
		if (vs.s->isSliderDown())
			return;
		const int iv = static_cast<int>(std::lround(val * vs.scale));
		if (vs.s->value() != iv)
			vs.s->setValue(iv);
		vs.show_value();
	}

	/* ---------------------------------------------------------------- model + edits */
	void commit(const mdhost::Settings &s, const QString &why)
	{
		model = s;
		mdhost::set_settings(s, why.toUtf8().constData());
	}

	void fill_editors()
	{
		filling = true;
		const mdhost::Settings &s = model;
		if (!drBpm->hasFocus() && drBpm->value() != static_cast<int>(std::lround(s.fallback_bpm)))
			drBpm->setValue(static_cast<int>(std::lround(s.fallback_bpm)));
		const int pi = drPhrase->findData(s.phrase_beats);
		if (pi >= 0 && drPhrase->currentIndex() != pi)
			drPhrase->setCurrentIndex(pi);
		if (drEnabled->isChecked() != s.enabled)
			drEnabled->setChecked(s.enabled);
		set_slider(drGain, s.input_gain_db);
		set_slider(drSens, s.kick_sensitivity);
		refresh_audio_list();
		filling = false;
	}

	void refresh_audio_list()
	{
		QStringList names;
		obs_enum_sources(enum_audio_list, &names);
		if (!model.audio_source.empty() && !names.contains(QString::fromStdString(model.audio_source)))
			names << QString::fromStdString(model.audio_source);
		QStringList cur;
		for (int i = 1; i < drAudio->count(); i++)
			cur << drAudio->itemData(i).toString();
		drAudio->blockSignals(true);
		if (cur != names || drAudio->count() == 0) {
			drAudio->clear();
			drAudio->addItem(T("AudioSource.None"), QString());
			for (const QString &n : names)
				drAudio->addItem(n, n);
		}
		const int i = drAudio->findData(QString::fromStdString(model.audio_source));
		if (i >= 0 && drAudio->currentIndex() != i)
			drAudio->setCurrentIndex(i);
		drAudio->blockSignals(false);
	}

	/* ---------------------------------------------------------------- live view */
	void send(mdhost::Cmd c) { mdhost::command(c); }

	void poll()
	{
		const mdhost::View view = mdhost::view();
		if (view.settings != model) {
			model = view.settings;
			fill_editors();
		} else if (!model_ready) {
			model_ready = true;
			fill_editors();
		}

		const md::ClockState &st = view.st;
		static const char *secName[3] = {"GROOVE", "BREAKDOWN", "DROP"};
		static const QColor secCol[3] = {QColor(80, 140, 255), QColor(255, 170, 40), QColor(255, 60, 100)};
		const int si = std::clamp(static_cast<int>(st.section), 0, 2);
		if (si != sec_seen_) {
			sec_seen_ = si;
			sec_start_ = view.beats;
		}
		set_chip(secChip, st.paused ? T("Dock.Paused") : secName[si],
			 st.paused ? QColor(140, 140, 140) : secCol[si]);
		secFor->setText(QString(T("Music.ForBeats"))
					.arg(std::max(0, static_cast<int>(std::floor(view.beats - sec_start_)))));

		beats->on = st.beat_frac < 0.35f ? static_cast<int>(st.beat % 4) : -1;
		beats->update();

		bpmLbl->setText(QString::number(st.bpm, 'f', 1));
		confLbl->setText(st.silent ? T("Music.FixedBpm")
					   : QString(T("Music.Confidence")).arg(std::lround(st.confidence * 100.0f)));

		const int pb = st.phrase_beats == 16 ? 16 : 32;
		const int bi = static_cast<int>(std::floor(view.beats));
		posLbl->setText(QString("%1.%2.%3").arg(bi / pb + 1).arg((bi % pb) / 4 + 1).arg(bi % 4 + 1));

		meter->db = st.input_db;
		meter->clip = st.clip;
		meter->bass = st.bass_level;
		meter->thr = st.kick_threshold;
		meter->silent = st.silent;
		if (st.kicks != kicks_seen) {
			kicks_seen = st.kicks;
			meter->kick_flash = 4; // ~130 ms at 30 Hz
		} else if (meter->kick_flash > 0) {
			meter->kick_flash--;
		}
		meter->update();

		timeline->tl = view.timeline;
		timeline->now = view.beats;
		timeline->phrase = pb;
		timeline->update();

		/* what is happening now */
		const int to_phrase = pb - (bi % pb);
		QString now;
		if (st.paused)
			now = T("Dock.NowPaused");
		else if (st.section == md::Section::Breakdown)
			now = T("Music.NowBreakdown");
		else if (st.section == md::Section::Drop)
			now = QString(T("Music.NowDrop")).arg(std::max(0, pb - (bi - static_cast<int>(sec_start_))));
		else
			now = QString(T("Music.NowGroove")).arg(to_phrase);
		nowLbl->setText(now);

		audioStatus->setText(view.audio_name.empty()
					     ? T("Dock.NoAudio")
					     : QString("%1: %2%3")
						       .arg(T("AudioSource"), QString::fromStdString(view.audio_name),
							    view.audio_attached ? QString() : QString(" (not found)")));
		migrated->setVisible(!model.migrated_from.empty());
		if (!model.migrated_from.empty())
			migrated->setText(
				QString(T("Music.Migrated")).arg(QString::fromStdString(model.migrated_from)));

		if (bLock->isChecked() != st.bpm_locked)
			bLock->setChecked(st.bpm_locked);
	}

	/* widgets */
	QWidget *body = nullptr;
	QLabel *secChip = nullptr, *secFor = nullptr, *bpmLbl = nullptr, *confLbl = nullptr, *posLbl = nullptr,
	       *nowLbl = nullptr, *audioStatus = nullptr, *legend = nullptr, *migrated = nullptr;
	BeatLights *beats = nullptr;
	AudioMeter *meter = nullptr;
	TimelineWidget *timeline = nullptr;
	QComboBox *drAudio = nullptr, *drPhrase = nullptr;
	QSpinBox *drBpm = nullptr;
	QPushButton *bTap = nullptr, *bPhrase = nullptr, *bLock = nullptr, *bNudgeBack = nullptr, *bNudgeFwd = nullptr,
		    *bBreakdown = nullptr, *bDrop = nullptr, *drEnabled = nullptr;
	ValueSlider drGain, drSens;
	QTimer *timer = nullptr;

	/* state */
	mdhost::Settings model;
	bool model_ready = false;
	bool filling = false;
	uint32_t kicks_seen = 0;
	int sec_seen_ = -1;
	double sec_start_ = 0.0;
};

MusicDock *g_music_dock = nullptr;

} // namespace

#endif // ENABLE_FRONTEND_API && ENABLE_QT

bool music_director_music_dock_available(void)
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	return g_music_dock != nullptr;
#else
	return false;
#endif
}

void music_director_music_dock_load(void)
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	auto *dock = new MusicDock();
	if (!obs_frontend_add_dock_by_id("music-director-dock", obs_module_text("MusicDock.Title"), dock)) {
		obs_log(LOG_ERROR, "music dock id already in use; dock not added");
		delete dock;
		return;
	}
	g_music_dock = dock;
	obs_log(LOG_INFO, "music dock registered (music-director-dock)");
#endif
}
