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
Music dock (v1.2, spec W1). Everything about *hearing* the music moved here out of the
Palette Director dock: beat lights, BPM/confidence, phrase.bar.beat, the section chip, the
level/bass meter, the 64-beat timeline and the beat-matching controls (audio source, input
gain, kick sensitivity, fallback BPM, phrase length). It also carries the v1.2 clock
commands (Phrase is now, Drop now, Force breakdown, Tap tempo, Lock BPM, Nudge).

Threading: everything here runs on the UI thread. Live view: a ~30 Hz QTimer polls
pdhost::read_view (a copy under the host's view mutex). Commands go pdhost::command ->
host command queue -> worker -> the Director's SPSC command queue. Edits use exactly the
same settings path the Palette Director dock uses (palette_lock_read_set / palette_lock_commit
/ palette_lock_set_audio), so both docks write the same settings keys.
*/

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>

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
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "dock-widgets.h"
#include "music-dock.h"
#include "palette-lock-filter.h"
#include "palette-lock-host.h"
#include "palette-lock-host.hpp"
#include "palette/palette.h"

using namespace pddock;

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
		setObjectName("PaletteDirectorMusicDock");
		pl_set_default(&model);
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
		picker->setFocusPolicy(Qt::StrongFocus);
		picker->installEventFilter(new WheelGuard(this));

		timer = new QTimer(this);
		timer->setInterval(33); // ~30 Hz
		QObject::connect(timer, &QTimer::timeout, this, [this] { poll(); });
	}

protected:
	void showEvent(QShowEvent *e) override
	{
		QWidget::showEvent(e);
		blog(LOG_INFO, "[palette-director] music dock shown: 30 Hz poll started");
		timer->start();
		poll();
	}

	void hideEvent(QHideEvent *e) override
	{
		QWidget::hideEvent(e);
		timer->stop();
		blog(LOG_INFO, "[palette-director] music dock hidden: poll stopped (directors keep running)");
	}

	/* ---------------------------------------------------------------- UI */
	void build_ui(QVBoxLayout *v)
	{
		auto *pickRow = new QHBoxLayout();
		pickRow->addWidget(new QLabel(T("Dock.Instance"), body));
		picker = new QComboBox(body);
		picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		pickRow->addWidget(picker, 1);
		v->addLayout(pickRow);
		QObject::connect(picker, &QComboBox::currentIndexChanged, this, [this](int i) {
			if (rebuilding || i < 0)
				return;
			attach(picker->itemData(i).toULongLong(), picker->itemText(i));
		});

		empty = new QLabel(T("Dock.NoInstance"), body);
		empty->setWordWrap(true);
		v->addWidget(empty);

		live_area = new QWidget(body);
		auto *lv = new QVBoxLayout(live_area);
		lv->setContentsMargins(0, 0, 0, 0);
		lv->setSpacing(7);
		v->addWidget(live_area);

		/* big BPM, phrase.bar.beat, section chip (+ beat lights) */
		auto *statRow = new QHBoxLayout();
		auto *bpmCol = new QVBoxLayout();
		bpmCol->setSpacing(0);
		bpmLbl = new QLabel(live_area);
		QFont bf = bpmLbl->font();
		bf.setPointSizeF(bf.pointSizeF() * 1.9);
		bf.setBold(true);
		bpmLbl->setFont(bf);
		confLbl = new QLabel(live_area);
		confLbl->setForegroundRole(QPalette::PlaceholderText);
		bpmCol->addWidget(bpmLbl);
		bpmCol->addWidget(confLbl);
		statRow->addLayout(bpmCol);
		statRow->addSpacing(14);
		auto *posCol = new QVBoxLayout();
		posCol->setSpacing(0);
		posLbl = new QLabel(live_area);
		QFont pf = posLbl->font();
		pf.setPointSizeF(pf.pointSizeF() * 1.9);
		pf.setBold(true);
		posLbl->setFont(pf);
		auto *posCap = new QLabel(T("Music.Position"), live_area);
		posCap->setForegroundRole(QPalette::PlaceholderText);
		posCol->addWidget(posLbl);
		posCol->addWidget(posCap);
		statRow->addLayout(posCol);
		statRow->addStretch(1);
		auto *secCol = new QVBoxLayout();
		secCol->setSpacing(3);
		secChip = new QLabel(live_area);
		secFor = new QLabel(live_area);
		secFor->setForegroundRole(QPalette::PlaceholderText);
		beats = new BeatLights(live_area);
		secCol->addWidget(secChip, 0, Qt::AlignRight);
		secCol->addWidget(secFor, 0, Qt::AlignRight);
		secCol->addWidget(beats, 0, Qt::AlignRight);
		statRow->addLayout(secCol);
		lv->addLayout(statRow);

		/* meter + timeline */
		meter = new AudioMeter(live_area);
		meter->setToolTip(T("Dock.MeterHelp"));
		lv->addWidget(meter);
		timeline = new TimelineWidget(live_area);
		lv->addWidget(timeline);
		legend = new QLabel(T("Dock.TimelineLegend"), live_area);
		legend->setForegroundRole(QPalette::PlaceholderText);
		legend->setWordWrap(true);
		lv->addWidget(legend);
		nowLbl = new QLabel(live_area);
		nowLbl->setWordWrap(true);
		QFont nf = nowLbl->font();
		nf.setBold(true);
		nowLbl->setFont(nf);
		nowLbl->setFrameShape(QFrame::StyledPanel);
		nowLbl->setContentsMargins(6, 4, 6, 4);
		lv->addWidget(nowLbl);
		audioStatus = new QLabel(live_area);
		audioStatus->setForegroundRole(QPalette::PlaceholderText);
		audioStatus->setWordWrap(true);
		lv->addWidget(audioStatus);

		/* beat matching */
		auto *hdr = new QLabel(T("Music.BeatMatching"), live_area);
		hdr->setForegroundRole(QPalette::PlaceholderText);
		lv->addWidget(hdr);

		auto *row = new QHBoxLayout();
		row->addWidget(new QLabel(T("AudioSource"), live_area));
		drAudio = new QComboBox(live_area);
		row->addWidget(drAudio, 1);
		lv->addLayout(row);
		QObject::connect(drAudio, &QComboBox::activated, this, [this](int i) {
			SrcRef r(selected);
			if (!r.s)
				return;
			audio_setting = drAudio->itemData(i).toString();
			palette_lock_set_audio(r.s, audio_setting.toUtf8().constData());
		});

		drGain = make_slider(lv, "InputGain", "InputGain.Help", -24, 48, false, 2.0, "input gain dB",
				     [](struct pl_set &s, double val) { s.input_gain_db = static_cast<float>(val); });
		drSens = make_slider(lv, "KickSensitivity", "KickSensitivity.Help", 0, 100, true, 100.0,
				     "kick sensitivity", [](struct pl_set &s, double val) {
					     s.kick_sensitivity = static_cast<float>(val);
				     });

		auto *row2 = new QHBoxLayout();
		row2->addWidget(new QLabel(T("FallbackBpm"), live_area));
		drBpm = new QSpinBox(live_area);
		drBpm->setRange(60, 200);
		row2->addWidget(drBpm, 1);
		row2->addWidget(new QLabel(T("PhraseLength"), live_area));
		drPhrase = new QComboBox(live_area);
		drPhrase->addItem("16 beats", 16);
		drPhrase->addItem("32 beats", 32);
		row2->addWidget(drPhrase, 1);
		lv->addLayout(row2);
		QObject::connect(drBpm, &QSpinBox::valueChanged, this, [this](int n) {
			if (filling)
				return;
			struct pl_set ns = model;
			ns.fallback_bpm = static_cast<float>(n);
			commit_set(ns, QString("fallback BPM -> %1").arg(n));
		});
		QObject::connect(drPhrase, &QComboBox::activated, this, [this](int i) {
			struct pl_set ns = model;
			ns.phrase_beats = drPhrase->itemData(i).toInt();
			commit_set(ns, "phrase length -> " + drPhrase->itemText(i));
		});

		/* clock commands */
		auto *cmds = new QGridLayout();
		cmds->setSpacing(6);
		bTap = new QPushButton(T("Music.TapTempo"), live_area);
		bPhrase = new QPushButton(T("Music.PhraseNow"), live_area);
		bLock = new QPushButton(T("Music.LockBpm"), live_area);
		bLock->setCheckable(true);
		bNudgeBack = new QPushButton(T("Music.NudgeBack"), live_area);
		bNudgeFwd = new QPushButton(T("Music.NudgeFwd"), live_area);
		bBreakdown = new QPushButton(T("Music.ForceBreakdown"), live_area);
		bDrop = new QPushButton(T("Music.DropNow"), live_area);
		for (QPushButton *b : {bTap, bPhrase, bLock, bNudgeBack, bNudgeFwd, bBreakdown})
			b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		cmds->addWidget(bTap, 0, 0);
		cmds->addWidget(bPhrase, 0, 1);
		cmds->addWidget(bLock, 0, 2);
		cmds->addWidget(bNudgeBack, 1, 0);
		cmds->addWidget(bNudgeFwd, 1, 1);
		cmds->addWidget(bBreakdown, 1, 2);
		lv->addLayout(cmds);
		bDrop->setStyleSheet("QPushButton{border:2px solid rgba(255,70,100,220);font-weight:700;padding:5px;}"
				     "QPushButton:hover{border-color:rgba(255,70,100,255);}");
		lv->addWidget(bDrop);
		lv->addWidget(help_label("Music.CommandsHelp", live_area));

		QObject::connect(bTap, &QPushButton::clicked, this, [this] { send(PD_CMD_TAP_TEMPO); });
		QObject::connect(bPhrase, &QPushButton::clicked, this, [this] { send(PD_CMD_PHRASE_NOW); });
		/* the checked state follows the director in poll(), so hotkey, MIDI and tap toggles show too */
		QObject::connect(bLock, &QPushButton::clicked, this, [this] { send(PD_CMD_LOCK_BPM); });
		QObject::connect(bNudgeBack, &QPushButton::clicked, this, [this] { send(PD_CMD_NUDGE_BACK); });
		QObject::connect(bNudgeFwd, &QPushButton::clicked, this, [this] { send(PD_CMD_NUDGE_FWD); });
		QObject::connect(bBreakdown, &QPushButton::clicked, this, [this] { send(PD_CMD_FORCE_BREAKDOWN); });
		QObject::connect(bDrop, &QPushButton::clicked, this, [this] { send(PD_CMD_DROP_NOW); });
	}

private:
	/* ---------------------------------------------------------------- small helpers */
	ValueSlider make_slider(QVBoxLayout *v, const char *label, const char *help, int lo, int hi, bool percent,
				double scale, const char *log_name, std::function<void(struct pl_set &, double)> set)
	{
		ValueSlider vs;
		vs.scale = scale;
		vs.percent = percent;
		auto *cap = new QLabel(T(label), live_area);
		auto *srow = new QHBoxLayout();
		vs.s = new QSlider(Qt::Horizontal, live_area);
		vs.s->setRange(lo, hi);
		vs.v = new QLabel(live_area);
		vs.v->setMinimumWidth(52);
		vs.v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		srow->addWidget(vs.s, 1);
		srow->addWidget(vs.v);
		auto *box = new QVBoxLayout();
		box->setSpacing(1);
		box->addWidget(cap);
		box->addLayout(srow);
		box->addWidget(help_label(help, live_area));
		v->addLayout(box);
		const ValueSlider copy = vs;
		QObject::connect(vs.s, &QSlider::valueChanged, this, [this, copy, set, log_name](int val) {
			copy.show_value();
			if (filling)
				return;
			struct pl_set ns = model;
			set(ns, val / copy.scale);
			commit_set(ns, copy.s->isSliderDown() ? QString()
							      : QString("%1 -> %2").arg(log_name, copy.v->text()));
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
	void commit_set(const struct pl_set &ns, const QString &why)
	{
		SrcRef r(selected);
		if (!r.s)
			return;
		const QByteArray w = why.toUtf8();
		palette_lock_commit(r.s, &ns, -1, w.constData());
		model = ns;
		have_model = true;
	}

	void reload_model(bool force)
	{
		SrcRef r(selected);
		if (!r.s)
			return;
		struct pl_set m;
		int edit = 0, live = 0;
		if (!palette_lock_read_set(r.s, &m, &edit, &live))
			return;
		const char *a = palette_lock_audio(r.s);
		const QString audio = QString::fromUtf8(a ? a : "");
		bfree((void *)a);
		if (!force && have_model && memcmp(&m, &model, sizeof(m)) == 0 && audio == audio_setting)
			return;
		model = m;
		have_model = true;
		audio_setting = audio;
		fill_editors();
	}

	void fill_editors()
	{
		filling = true;
		const struct pl_set &s = model;
		if (!drBpm->hasFocus() && drBpm->value() != static_cast<int>(std::lround(s.fallback_bpm)))
			drBpm->setValue(static_cast<int>(std::lround(s.fallback_bpm)));
		const int pi = drPhrase->findData(s.phrase_beats);
		if (pi >= 0 && drPhrase->currentIndex() != pi)
			drPhrase->setCurrentIndex(pi);
		set_slider(drGain, s.input_gain_db);
		set_slider(drSens, s.kick_sensitivity);
		refresh_audio_list();
		filling = false;
	}

	void refresh_audio_list()
	{
		QStringList names;
		obs_enum_sources(enum_audio_list, &names);
		if (!audio_setting.isEmpty() && !names.contains(audio_setting))
			names << audio_setting;
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
		const int i = drAudio->findData(audio_setting);
		if (i >= 0 && drAudio->currentIndex() != i)
			drAudio->setCurrentIndex(i);
		drAudio->blockSignals(false);
	}

	/* ---------------------------------------------------------------- live view */
	void send(int cmd, int idx = -1)
	{
		if (selected)
			pdhost::command(selected, cmd, idx);
	}

	void attach(uint64_t id, const QString &label)
	{
		if (id == selected)
			return;
		selected = id;
		sec_seen_ = -1;
		have_model = false;
		reload_model(true);
		blog(LOG_INFO, "[palette-director] music dock: attached to '%s'", label.toUtf8().constData());
	}

	void refresh_instances(bool force = false)
	{
		const auto list = pdhost::list_instances();
		std::string sig;
		for (const auto &ii : list)
			sig += std::to_string(ii.id) + "|" + ii.parent_name + "|" + ii.filter_name + ";";
		if (sig == inst_sig && !force)
			return;
		const bool changed = sig != inst_sig;
		inst_sig = sig;
		rebuilding = true;
		picker->clear();
		int sel = -1;
		const uint64_t want = want_select ? want_select : selected;
		for (const auto &ii : list) {
			const QString label =
				QString::fromUtf8(ii.parent_name.empty() ? "(no source)" : ii.parent_name.c_str()) +
				" / " + QString::fromUtf8(ii.filter_name.c_str());
			picker->addItem(label, QVariant::fromValue<qulonglong>(ii.id));
			if (ii.id == want)
				sel = picker->count() - 1;
		}
		want_select = 0;
		if (sel < 0 && picker->count() > 0)
			sel = 0;
		if (sel >= 0)
			picker->setCurrentIndex(sel);
		rebuilding = false;
		if (changed) {
			QStringList names;
			for (int i = 0; i < picker->count(); i++)
				names << picker->itemText(i);
			blog(LOG_INFO,
			     "[palette-director] music dock: instance list changed, %d Palette Lock filter(s): [%s]",
			     (int)list.size(), names.join("; ").toUtf8().constData());
		}
		if (sel >= 0)
			attach(picker->itemData(sel).toULongLong(), picker->itemText(sel));
		else
			selected = 0;
	}

	void poll()
	{
		refresh_instances();
		const bool have = selected && pdhost::read_view(selected, view, 0);
		empty->setVisible(!have);
		live_area->setVisible(have);
		if (!have)
			return;
		if (++slow_tick % 8 == 0)
			reload_model(false); // ~4 Hz: pick up edits from the properties window / other dock

		const pd::DirectorState &st = view.st;
		const struct pl_set &set = view.set;

		static const char *secName[3] = {"GROOVE", "BREAKDOWN", "DROP"};
		static const QColor secCol[3] = {QColor(80, 140, 255), QColor(255, 170, 40), QColor(255, 60, 100)};
		const int si = std::clamp(static_cast<int>(st.section), 0, 2);
		if (si != sec_seen_) {
			sec_seen_ = si;
			sec_start_ = view.beats;
		}
		set_chip(secChip, secName[si], secCol[si]);
		secFor->setText(QString(T("Music.ForBeats"))
					.arg(std::max(0, static_cast<int>(std::floor(view.beats - sec_start_)))));

		beats->on = st.beat_frac < 0.35f ? static_cast<int>(st.beat % 4) : -1;
		beats->update();

		bpmLbl->setText(QString::number(st.bpm, 'f', 1));
		confLbl->setText(st.silent ? T("Music.FixedBpm")
					   : QString(T("Music.Confidence")).arg(std::lround(st.confidence * 100.0f)));

		const int pb = set.phrase_beats == 16 ? 16 : 32;
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

		/* what is happening now (mock "now" sentence) */
		const int to_phrase = pb - (bi % pb);
		QString now;
		if (st.section == pd::Section::Breakdown)
			now = T("Music.NowBreakdown");
		else if (st.section == pd::Section::Drop)
			now = QString(T("Music.NowDrop")).arg(std::max(0, pb - (bi - static_cast<int>(sec_start_))));
		else
			now = QString(T("Music.NowGroove")).arg(to_phrase);
		nowLbl->setText(now);

		audioStatus->setText(view.audio_name.empty()
					     ? T("Dock.NoAudio")
					     : QString("%1: %2%3")
						       .arg(T("AudioSource"),
							    QString::fromUtf8(view.audio_name.c_str()),
							    view.audio_attached ? QString() : QString(" (not found)")));

		if (bLock->isChecked() != st.bpm_locked)
			bLock->setChecked(st.bpm_locked);
	}

	/* widgets */
	QWidget *body = nullptr, *live_area = nullptr;
	QComboBox *picker = nullptr;
	QLabel *empty = nullptr, *secChip = nullptr, *secFor = nullptr, *bpmLbl = nullptr, *confLbl = nullptr,
	       *posLbl = nullptr, *nowLbl = nullptr, *audioStatus = nullptr, *legend = nullptr;
	BeatLights *beats = nullptr;
	AudioMeter *meter = nullptr;
	TimelineWidget *timeline = nullptr;
	QComboBox *drAudio = nullptr, *drPhrase = nullptr;
	QSpinBox *drBpm = nullptr;
	ValueSlider drGain, drSens;
	QPushButton *bTap = nullptr, *bPhrase = nullptr, *bLock = nullptr, *bNudgeBack = nullptr, *bNudgeFwd = nullptr,
		    *bBreakdown = nullptr, *bDrop = nullptr;
	QTimer *timer = nullptr;

	/* state */
	pdhost::DockView view;
	struct pl_set model;
	bool have_model = false;
	bool filling = false, rebuilding = false;
	QString audio_setting;
	uint64_t selected = 0, want_select = 0;
	std::string inst_sig;
	unsigned slow_tick = 0;
	uint32_t kicks_seen = 0;
	int sec_seen_ = -1;
	double sec_start_ = 0.0;
};

MusicDock *g_music_dock = nullptr;

} // namespace

#endif // ENABLE_FRONTEND_API && ENABLE_QT

#include "music-dock.h"

bool palette_director_music_dock_available(void)
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	return g_music_dock != nullptr;
#else
	return false;
#endif
}

void palette_director_music_dock_load(void)
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	auto *dock = new MusicDock();
	if (!obs_frontend_add_dock_by_id("palette-director-music-dock", obs_module_text("MusicDock.Title"), dock)) {
		blog(LOG_ERROR, "[palette-director] music dock id already in use; dock not added");
		delete dock;
		return;
	}
	g_music_dock = dock;
	blog(LOG_INFO, "[palette-director] music dock registered (obs_frontend_add_dock_by_id)");
#endif
}
