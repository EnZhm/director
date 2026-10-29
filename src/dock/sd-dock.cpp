/*
Scene Director dock: the control surface for the set. It picks the Scene Director source, shows the
Palette Director clock and section, the MIDI out link (port open / not found / none selected), the
Next / Prev buttons, the automatic-switch triggers, the cooldown and Scene lead, the Next and Prev
message editors, and the log of every message with its beat, reason and bytes.

Reads the source through sdapi (thread-safe); every edit is saved in the source's settings. Its
refresh timer also steps the scheduler (sdapi::tick) so the feature keeps working when the source
is not in the active scene.
*/
#include "sd-dock.h"

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/midi_msg.h"
#include "obs/sd-api.h"
#include "obs/sd-midi.h"

namespace {

const char *kDockId = "scene-director-dock";
const char *kSectionNames[] = {"GROOVE", "BREAKDOWN", "DROP"};
const char *kSectionColours[] = {"#2d6a4f", "#1d4e89", "#9d0208"};

QString text_of(const std::string &s)
{
	return QString::fromStdString(s);
}

class SceneDirectorDock : public QWidget {
public:
	SceneDirectorDock() : QWidget(nullptr)
	{
		setObjectName("SceneDirectorDock");
		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(6, 6, 6, 6);
		root->setSpacing(6);

		/* source + clock + section */
		auto *top = new QHBoxLayout();
		sources_ = new QComboBox();
		sources_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		sources_->setMinimumContentsLength(12);
		top->addWidget(sources_, 1);
		clock_ = new QLabel("-");
		top->addWidget(clock_);
		section_ = new QLabel("GROOVE");
		section_->setAlignment(Qt::AlignCenter);
		section_->setMinimumWidth(96);
		top->addWidget(section_);
		root->addLayout(top);

		/* MIDI out: link chip, device, rescan */
		auto *link = new QHBoxLayout();
		chip_ = new QLabel(obs_module_text("Link.None"));
		chip_->setAlignment(Qt::AlignCenter);
		chip_->setMinimumWidth(112);
		link->addWidget(chip_);
		devices_ = new QComboBox();
		devices_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		devices_->setMinimumContentsLength(16);
		link->addWidget(devices_, 1);
		rescan_ = new QPushButton(obs_module_text("MidiRescan"));
		link->addWidget(rescan_);
		root->addLayout(link);

		/* Next / Prev */
		auto *manual = new QHBoxLayout();
		next_btn_ = new QPushButton(obs_module_text("Next"));
		next_btn_->setToolTip(obs_module_text("Next.Tip"));
		prev_btn_ = new QPushButton(obs_module_text("Prev"));
		prev_btn_->setToolTip(obs_module_text("Prev.Tip"));
		manual->addWidget(next_btn_);
		manual->addWidget(prev_btn_);
		manual->addStretch(1);
		root->addLayout(manual);

		/* triggers, cooldown, scene lead */
		auto *grid = new QGridLayout();
		head_ = new QLabel(obs_module_text("Head.Switches"));
		head_->setStyleSheet("font-weight:bold;");
		grid->addWidget(head_, 0, 0, 1, 3);
		on_breakdown_ = new QCheckBox(obs_module_text("Trigger.Breakdown"));
		on_drop_ = new QCheckBox(obs_module_text("Trigger.Drop"));
		on_groove_ = new QCheckBox(obs_module_text("Trigger.GroovePhrase"));
		grid->addWidget(on_breakdown_, 1, 0, 1, 3);
		grid->addWidget(on_drop_, 2, 0, 1, 3);
		grid->addWidget(on_groove_, 3, 0, 1, 3);
		grid->addWidget(new QLabel(obs_module_text("Cooldown")), 4, 0);
		cooldown_ = new QSpinBox();
		cooldown_->setRange(0, 256);
		cooldown_->setSuffix(obs_module_text("Beats"));
		cooldown_->setToolTip(obs_module_text("Cooldown.Tip"));
		grid->addWidget(cooldown_, 4, 1, 1, 2);
		grid->addWidget(new QLabel(obs_module_text("SceneLead")), 5, 0);
		lead_ = new QSlider(Qt::Horizontal);
		lead_->setRange(0, 500);
		lead_->setToolTip(obs_module_text("SceneLead.Tip"));
		lead_label_ = new QLabel("0 ms");
		lead_label_->setMinimumWidth(52);
		grid->addWidget(lead_, 5, 1);
		grid->addWidget(lead_label_, 5, 2);
		grid->setColumnStretch(1, 1);
		root->addLayout(grid);

		/* Next / Prev message editors */
		auto *msgs = new QGridLayout();
		auto *mh = new QLabel(obs_module_text("Head.Messages"));
		mh->setStyleSheet("font-weight:bold;");
		msgs->addWidget(mh, 0, 0, 1, 4);
		build_message_row(msgs, 1, obs_module_text("MidiNext"), &next_type_, &next_channel_, &next_number_);
		build_message_row(msgs, 2, obs_module_text("MidiPrev"), &prev_type_, &prev_channel_, &prev_number_);
		root->addLayout(msgs);

		/* log */
		log_head_ = new QLabel(obs_module_text("Head.Log"));
		log_head_->setStyleSheet("font-weight:bold;");
		root->addWidget(log_head_);
		log_ = new QPlainTextEdit();
		log_->setReadOnly(true);
		log_->setMaximumBlockCount(400);
		log_->setLineWrapMode(QPlainTextEdit::NoWrap);
		log_->setStyleSheet("font-family:Consolas,monospace; font-size:11px;");
		log_->setMinimumHeight(110);
		root->addWidget(log_, 1);

		footer_ = new QLabel();
		footer_->setStyleSheet("color:#999;");
		root->addWidget(footer_);

		/* wiring */
		connect(sources_, &QComboBox::currentIndexChanged, this, [this](int) { select_current(); });
		connect(rescan_, &QPushButton::clicked, this, [this] { list_devices(); });
		connect(devices_, &QComboBox::activated, this, [this](int) { push_device(); });
		connect(next_btn_, &QPushButton::clicked, this,
			[this] { with_source([](obs_source_t *s) { sdapi::manual_next(s); }); });
		connect(prev_btn_, &QPushButton::clicked, this,
			[this] { with_source([](obs_source_t *s) { sdapi::manual_prev(s); }); });
		connect(on_breakdown_, &QCheckBox::toggled, this, [this](bool) { push_triggers(); });
		connect(on_drop_, &QCheckBox::toggled, this, [this](bool) { push_triggers(); });
		connect(on_groove_, &QCheckBox::toggled, this, [this](bool) { push_triggers(); });
		connect(cooldown_, &QSpinBox::editingFinished, this, [this] { push_triggers(); });
		connect(lead_, &QSlider::valueChanged, this,
			[this](int v) { lead_label_->setText(QString("%1 ms").arg(v)); });
		connect(lead_, &QSlider::sliderReleased, this, [this] { push_triggers(); });
		connect(next_type_, &QComboBox::activated, this, [this](int) { push_messages(); });
		connect(prev_type_, &QComboBox::activated, this, [this](int) { push_messages(); });
		connect(next_channel_, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this](int) { push_messages(); });
		connect(next_number_, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this](int) { push_messages(); });
		connect(prev_channel_, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this](int) { push_messages(); });
		connect(prev_number_, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this](int) { push_messages(); });

		auto *fast = new QTimer(this);
		fast->setInterval(40);
		connect(fast, &QTimer::timeout, this, [this] { refresh(); });
		fast->start();
		auto *slow = new QTimer(this);
		slow->setInterval(2000);
		connect(slow, &QTimer::timeout, this, [this] {
			list_sources();
			list_devices();
		});
		slow->start();

		list_sources();
		list_devices();
	}

	~SceneDirectorDock() override { obs_weak_source_release(weak_); }

private:
	void build_message_row(QGridLayout *g, int row, const QString &label, QComboBox **type, QSpinBox **channel,
			       QSpinBox **number)
	{
		g->addWidget(new QLabel(label), row, 0);
		*type = new QComboBox();
		(*type)->addItem(obs_module_text("MidiType.Note"), (int)cd::midi::Type::Note);
		(*type)->addItem(obs_module_text("MidiType.Cc"), (int)cd::midi::Type::Control);
		g->addWidget(*type, row, 1);
		*channel = new QSpinBox();
		(*channel)->setRange(1, 16);
		(*channel)->setPrefix(obs_module_text("Ch"));
		g->addWidget(*channel, row, 2);
		*number = new QSpinBox();
		(*number)->setRange(0, 127);
		(*number)->setPrefix(obs_module_text("Num"));
		g->addWidget(*number, row, 3);
	}

	void list_sources()
	{
		std::vector<std::string> names;
		obs_enum_sources(
			[](void *p, obs_source_t *s) {
				if (sdapi::is_scene_director(s))
					static_cast<std::vector<std::string> *>(p)->push_back(obs_source_get_name(s));
				return true;
			},
			&names);
		QStringList now;
		for (const auto &n : names)
			now << text_of(n);
		QStringList had;
		for (int i = 0; i < sources_->count(); ++i)
			had << sources_->itemText(i);
		if (now == had)
			return;
		const QString keep = sources_->currentText();
		sources_->blockSignals(true);
		sources_->clear();
		sources_->addItems(now);
		const int at = now.indexOf(keep);
		sources_->setCurrentIndex(at >= 0 ? at : (now.isEmpty() ? -1 : 0));
		sources_->blockSignals(false);
		select_current();
	}

	void list_devices()
	{
		QStringList names;
		names << obs_module_text("MidiOutDevice.None");
		for (const sdmidi::Device &d : sdmidi::output_devices())
			names << text_of(d.name);
		QStringList had;
		for (int i = 0; i < devices_->count(); ++i)
			had << devices_->itemText(i);
		if (names == had)
			return;
		const QString keep = devices_->currentText();
		devices_->blockSignals(true);
		devices_->clear();
		devices_->addItem(obs_module_text("MidiOutDevice.None"), QString());
		for (const sdmidi::Device &d : sdmidi::output_devices())
			devices_->addItem(text_of(d.name), text_of(d.name));
		const int at = devices_->findText(keep);
		devices_->setCurrentIndex(at >= 0 ? at : 0);
		devices_->blockSignals(false);
	}

	void select_current()
	{
		obs_weak_source_release(weak_);
		weak_ = nullptr;
		log_seq_ = 0;
		log_->clear();
		const QString name = sources_->currentText();
		if (name.isEmpty())
			return;
		obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData());
		if (s) {
			weak_ = obs_source_get_weak_source(s);
			obs_source_release(s);
		}
	}

	template<typename F> void with_source(F &&fn)
	{
		obs_source_t *s = weak_ ? obs_weak_source_get_source(weak_) : nullptr;
		if (!s)
			return;
		fn(s);
		obs_source_release(s);
	}

	/* ------------------------------------------------------------------------------- push */

	void push_device()
	{
		const std::string device = devices_->currentData().toString().toStdString();
		with_source([&](obs_source_t *s) { sdapi::set_midi_out(s, device); });
	}

	void push_triggers()
	{
		const bool bd = on_breakdown_->isChecked();
		const bool dr = on_drop_->isChecked();
		const bool gr = on_groove_->isChecked();
		const int cooldown = cooldown_->value();
		const double lead = (double)lead_->value();
		with_source([&](obs_source_t *s) { sdapi::set_triggers(s, bd, dr, gr, cooldown, lead); });
	}

	void push_messages()
	{
		cd::midi::MsgSpec next, prev;
		next.type = (cd::midi::Type)next_type_->currentData().toInt();
		next.channel = next_channel_->value();
		next.number = next_number_->value();
		prev.type = (cd::midi::Type)prev_type_->currentData().toInt();
		prev.channel = prev_channel_->value();
		prev.number = prev_number_->value();
		with_source([&](obs_source_t *s) { sdapi::set_messages(s, next, prev); });
	}

	/* ----------------------------------------------------------------------------- refresh */

	void refresh()
	{
		sdapi::Status st;
		bool ok = false;
		with_source([&](obs_source_t *s) {
			sdapi::tick(s); /* drives the scheduler when the source's own tick is quiet */
			ok = sdapi::status(s, st);
		});
		setEnabled(ok || sources_->count() > 0);
		if (!ok) {
			clock_->setText(sources_->count() ? "-" : obs_module_text("NoSource"));
			set_chip(0, obs_module_text("Link.None"));
			return;
		}

		clock_->setText(QString("%1 %2 BPM").arg(st.clock_pd ? "PD" : "free").arg(st.bpm, 0, 'f', 1));
		clock_->setToolTip(st.clock_pd ? obs_module_text("Clock.Pd") : obs_module_text("Clock.Free"));
		const int sec = std::clamp(st.section, 0, 2);
		section_->setText(kSectionNames[sec]);
		section_->setStyleSheet(QString("background:%1; color:white; font-weight:bold; padding:2px 6px; "
						"border-radius:3px;")
						.arg(kSectionColours[sec]));

		/* link chip + device combo */
		if (!st.midi_available) {
			set_chip(2, obs_module_text("Link.NoBackend"));
		} else if (st.midi_device.empty()) {
			set_chip(0, obs_module_text("Link.None"));
		} else if (st.midi_open) {
			set_chip(1, obs_module_text("Link.Open"));
		} else {
			set_chip(2, obs_module_text("Link.NotFound"));
		}
		chip_->setToolTip(st.midi_open
					  ? QString(obs_module_text("Link.Open.Tip")).arg(text_of(st.midi_open_name))
					  : (!st.midi_device.empty() ? QString(obs_module_text("Link.NotFound.Tip"))
									       .arg(text_of(st.midi_device))
									       .arg((qulonglong)st.midi_failed)
								     : QString(obs_module_text("Link.None.Tip"))));
		const int at = devices_->findData(text_of(st.midi_device));
		if (at >= 0 && at != devices_->currentIndex() && !devices_->hasFocus()) {
			devices_->blockSignals(true);
			devices_->setCurrentIndex(at);
			devices_->blockSignals(false);
		}

		set_check(on_breakdown_, st.trigger_breakdown);
		set_check(on_drop_, st.trigger_drop);
		set_check(on_groove_, st.trigger_groove_phrase);
		set_spin(cooldown_, std::clamp(st.cooldown_beats, 0, 256));
		if (lead_->value() != (int)std::lround(st.lead_ms)) {
			lead_->blockSignals(true);
			lead_->setValue((int)std::lround(st.lead_ms));
			lead_->blockSignals(false);
			lead_label_->setText(QString("%1 ms").arg((int)std::lround(st.lead_ms)));
		}

		set_msg(next_type_, next_channel_, next_number_, st.next);
		set_msg(prev_type_, prev_channel_, prev_number_, st.prev);

		/* log */
		if (st.log_seq != log_seq_) {
			const uint64_t fresh = st.log_seq - log_seq_;
			const size_t from = fresh >= st.log.size() ? 0 : st.log.size() - (size_t)fresh;
			auto *sb = log_->verticalScrollBar();
			const bool at_end = sb->value() >= sb->maximum() - 4;
			for (size_t i = from; i < st.log.size(); ++i)
				log_->appendPlainText(text_of(st.log[i]));
			if (at_end)
				sb->setValue(sb->maximum());
			log_seq_ = st.log_seq;
		}
		log_head_->setText(QString("%1 (%2)").arg(obs_module_text("Head.Log")).arg(st.switches));

		footer_->setText(QString(obs_module_text("Footer"))
					 .arg(st.switches)
					 .arg(text_of(st.last_reason))
					 .arg(st.last_switch_beat, 0, 'f', 2)
					 .arg((qulonglong)st.midi_sent));
	}

	void set_chip(int state, const QString &text)
	{
		const char *col = state == 1 ? "#1f6a45" : (state == 2 ? "#7f1d1d" : "#495057");
		chip_->setText(text);
		chip_->setStyleSheet(
			QString("background:%1; color:white; font-weight:bold; padding:2px 6px; border-radius:3px;")
				.arg(col));
	}

	static void set_check(QCheckBox *c, bool on)
	{
		if (!c || c->isChecked() == on)
			return;
		c->blockSignals(true);
		c->setChecked(on);
		c->blockSignals(false);
	}

	static void set_spin(QSpinBox *s, int value)
	{
		if (!s || s->value() == value)
			return;
		s->blockSignals(true);
		s->setValue(value);
		s->blockSignals(false);
	}

	static void set_msg(QComboBox *type, QSpinBox *channel, QSpinBox *number, const cd::midi::MsgSpec &m)
	{
		if (!type)
			return;
		const int at = type->findData((int)m.type);
		if (at >= 0 && at != type->currentIndex() && !type->hasFocus()) {
			type->blockSignals(true);
			type->setCurrentIndex(at);
			type->blockSignals(false);
		}
		if (channel && !channel->hasFocus())
			set_spin(channel, (int)cd::midi::clamp_channel(m.channel));
		if (number && !number->hasFocus())
			set_spin(number, (int)cd::midi::clamp_number(m.number));
	}

private:
	obs_weak_source_t *weak_ = nullptr;
	QComboBox *sources_, *devices_, *next_type_, *prev_type_;
	QLabel *clock_, *section_, *chip_, *head_, *log_head_, *footer_, *lead_label_;
	QPushButton *rescan_, *next_btn_, *prev_btn_;
	QCheckBox *on_breakdown_, *on_drop_, *on_groove_;
	QSpinBox *cooldown_, *next_channel_, *next_number_, *prev_channel_, *prev_number_;
	QSlider *lead_;
	QPlainTextEdit *log_;
	uint64_t log_seq_ = 0;
};

SceneDirectorDock *g_dock = nullptr;

} // namespace

extern "C" void scene_director_dock_load(void)
{
	auto *dock = new SceneDirectorDock();
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("SceneDirector"), dock)) {
		obs_log(LOG_ERROR, "dock id already in use; dock not added");
		delete dock;
		return;
	}
	g_dock = dock;
	obs_log(LOG_INFO, "Scene Director dock registered");

#if defined(CD_TEST_HOOKS)
	/* sandbox only (-DCD_TEST_HOOKS=ON, never in releases): SD_DOCK_SNAPSHOT=<png path> opens the
	 * dock docked right and saves a grab of it every 2 s */
	const QString snap = qEnvironmentVariable("SD_DOCK_SNAPSHOT");
	if (!snap.isEmpty()) {
		obs_log(LOG_INFO, "test hooks: dock snapshots to %s", snap.toUtf8().constData());
		QTimer::singleShot(8000, dock, [dock] {
			auto *dw = qobject_cast<QDockWidget *>(dock->parentWidget());
			auto *mw = static_cast<QMainWindow *>(obs_frontend_get_main_window());
			if (!dw || !mw)
				return;
			dw->setFloating(false);
			mw->addDockWidget(Qt::RightDockWidgetArea, dw);
			dw->show();
			dw->raise();
			mw->resizeDocks({dw}, {460}, Qt::Horizontal);
		});
		auto *t = new QTimer(dock);
		t->setInterval(2000);
		QObject::connect(t, &QTimer::timeout, dock, [dock, snap] { dock->grab().save(snap); });
		t->start();
	}
#endif
}

#else

extern "C" void scene_director_dock_load(void) {}

#endif
