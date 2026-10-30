/*
Scene Director dock: the control surface for the set (spec 4.7). It picks the Scene Director
source and shows the Music Director clock and section, the OSC link chip, host and ports (with the
spec 4.6 port guard), sent / received / dropped, confirmation latency and counters, Next / Prev,
the breakdown and drop triggers with cooldown and Scene lead, the shared event map (path, Reload,
Open folder, last load summary) and the log.

The dock only calls sdapi (thread-safe); it never touches a socket. Triggers and cooldown edit the
shared event map entries breakdown-next / drop-next, the single source of truth; Scene lead and the
ports are the source's own settings. Its refresh timer also steps the scheduler (sdapi::tick) so the
feature keeps working when the source is not in the active scene.
*/
#include "sd-dock.h"

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "obs/sd-api.h"

namespace {

const char *kDockId = "scene-director-dock";
const char *kSectionNames[] = {"GROOVE", "BREAKDOWN", "DROP"};
const char *kSectionColours[] = {"#2d6a4f", "#1d4e89", "#9d0208"};
const char *kBreakdownId = "breakdown-next";
const char *kDropId = "drop-next";
const int kLogLines = 200;
const char *kAmber = "color:#f59f00; font-weight:bold;";

QString text_of(const std::string &s)
{
	return QString::fromStdString(s);
}

QString ms_of(int64_t ns)
{
	return ns < 0 ? QString("-") : QString("%1 ms").arg((double)ns / 1e6, 0, 'f', 0);
}

const sdapi::Status::MappingInfo *find_mapping(const sdapi::Status &st, const char *id)
{
	for (const auto &m : st.mappings)
		if (m.id == id)
			return &m;
	return nullptr;
}

/* set a spin box only when the user is not editing it */
void sync_spin(QSpinBox *w, int v)
{
	if (w->hasFocus() || w->value() == v)
		return;
	w->blockSignals(true);
	w->setValue(v);
	w->blockSignals(false);
}

void sync_check(QCheckBox *w, const sdapi::Status::MappingInfo *m)
{
	w->setEnabled(m != nullptr);
	const bool on = m && m->enabled;
	if (w->isChecked() == on)
		return;
	w->blockSignals(true);
	w->setChecked(on);
	w->blockSignals(false);
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

		/* OSC link chip + traffic */
		auto *link = new QHBoxLayout();
		chip_ = new QLabel(obs_module_text("OscSending"));
		chip_->setAlignment(Qt::AlignCenter);
		chip_->setMinimumWidth(112);
		link->addWidget(chip_);
		traffic_ = new QLabel();
		link->addWidget(traffic_, 1);
		root->addLayout(link);

		/* host and ports (spec 4.6 port guard) */
		auto *ports = new QHBoxLayout();
		host_ = new QLineEdit();
		host_->setPlaceholderText(obs_module_text("OscHost"));
		host_->setToolTip(obs_module_text("OscHost"));
		send_port_ = new QSpinBox();
		send_port_->setRange(1, 65535);
		send_port_->setToolTip(obs_module_text("OscSendPort"));
		feedback_port_ = new QSpinBox();
		feedback_port_->setRange(0, 65535);
		feedback_port_->setToolTip(obs_module_text("Dock.FeedbackPort.Tip"));
		apply_ports_ = new QPushButton(obs_module_text("Dock.Apply"));
		ports->addWidget(new QLabel(obs_module_text("OscHost")));
		ports->addWidget(host_, 1);
		ports->addWidget(new QLabel(obs_module_text("Dock.Send")));
		ports->addWidget(send_port_);
		ports->addWidget(new QLabel(obs_module_text("Dock.Feedback")));
		ports->addWidget(feedback_port_);
		ports->addWidget(apply_ports_);
		root->addLayout(ports);
		port_warn_ = new QLabel();
		port_warn_->setWordWrap(true);
		port_warn_->setStyleSheet(kAmber);
		port_warn_->hide();
		root->addWidget(port_warn_);

		/* confirmations (feedback is best-effort: misses are counters, not errors) */
		confirm_ = new QLabel();
		confirm_->setToolTip(obs_module_text("Dock.Confirm.Tip"));
		root->addWidget(confirm_);

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

		/* triggers: the event map entries breakdown-next / drop-next + Scene lead */
		auto *trig = new QGridLayout();
		breakdown_ = new QCheckBox(obs_module_text("Dock.Breakdown"));
		breakdown_->setToolTip(obs_module_text("Dock.Breakdown.Tip"));
		drop_ = new QCheckBox(obs_module_text("Dock.Drop"));
		drop_->setToolTip(obs_module_text("Dock.Drop.Tip"));
		cooldown_ = new QSpinBox();
		cooldown_->setRange(0, 256);
		cooldown_->setSuffix(QString(" ") + obs_module_text("Dock.Beats"));
		cooldown_->setToolTip(obs_module_text("Dock.Cooldown.Tip"));
		lead_ = new QSpinBox();
		lead_->setRange(0, 500);
		lead_->setSuffix(" ms");
		lead_->setToolTip(obs_module_text("Dock.Lead.Tip"));
		trig->addWidget(breakdown_, 0, 0);
		trig->addWidget(drop_, 0, 1);
		trig->addWidget(new QLabel(obs_module_text("Dock.Cooldown")), 1, 0);
		trig->addWidget(cooldown_, 1, 1);
		trig->addWidget(new QLabel(obs_module_text("SceneLead")), 2, 0);
		trig->addWidget(lead_, 2, 1);
		root->addLayout(trig);
		trig_msg_ = new QLabel();
		trig_msg_->setStyleSheet(kAmber);
		trig_msg_->hide();
		root->addWidget(trig_msg_);

		/* the shared event map */
		auto *map = new QHBoxLayout();
		map_path_ = new QLabel("-");
		map_path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
		map_path_->setMinimumWidth(40);
		map_path_->setStyleSheet("color:#999;");
		reload_ = new QPushButton(obs_module_text("Dock.Reload"));
		reload_->setToolTip(obs_module_text("Dock.Reload.Tip"));
		open_dir_ = new QPushButton(obs_module_text("Dock.OpenFolder"));
		map->addWidget(new QLabel(obs_module_text("Dock.Map")));
		map->addWidget(map_path_, 1);
		map->addWidget(reload_);
		map->addWidget(open_dir_);
		root->addLayout(map);
		map_summary_ = new QLabel();
		map_summary_->setWordWrap(true);
		root->addWidget(map_summary_);

		/* log: route, args, beat, reason, seq, confirm result; newest last */
		log_head_ = new QLabel(obs_module_text("Head.Log"));
		log_head_->setStyleSheet("font-weight:bold;");
		root->addWidget(log_head_);
		log_ = new QPlainTextEdit();
		log_->setReadOnly(true);
		log_->setMaximumBlockCount(kLogLines);
		log_->setLineWrapMode(QPlainTextEdit::NoWrap);
		log_->setStyleSheet("font-family:Consolas,monospace; font-size:11px;");
		log_->setMinimumHeight(110);
		root->addWidget(log_, 1);

		connect(sources_, &QComboBox::currentIndexChanged, this, [this](int) { select_current(); });
		connect(next_btn_, &QPushButton::clicked, this,
			[this] { with_source([](obs_source_t *s) { sdapi::manual_next(s); }); });
		connect(prev_btn_, &QPushButton::clicked, this,
			[this] { with_source([](obs_source_t *s) { sdapi::manual_prev(s); }); });
		connect(apply_ports_, &QPushButton::clicked, this, [this] { save_ports(); });
		connect(host_, &QLineEdit::returnPressed, this, [this] { save_ports(); });
		connect(breakdown_, &QCheckBox::toggled, this,
			[this](bool on) { map_result(sdapi::set_mapping_enabled(kBreakdownId, on), kBreakdownId); });
		connect(drop_, &QCheckBox::toggled, this,
			[this](bool on) { map_result(sdapi::set_mapping_enabled(kDropId, on), kDropId); });
		connect(cooldown_, &QSpinBox::editingFinished, this, [this] { save_cooldown(); });
		connect(lead_, &QSpinBox::editingFinished, this, [this] {
			const double v = (double)lead_->value();
			with_source([v](obs_source_t *s) { sdapi::set_scene_lead(s, v); });
		});
		connect(reload_, &QPushButton::clicked, this, [this] {
			std::string err;
			if (sdapi::reload_map(&err))
				show_trig_msg(QString());
			else
				show_trig_msg(QString(obs_module_text("Dock.ReloadFailed")).arg(text_of(err)));
		});
		connect(open_dir_, &QPushButton::clicked, this, [this] {
			if (!map_file_.isEmpty())
				QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(map_file_).absolutePath()));
		});

		auto *fast = new QTimer(this);
		fast->setInterval(40);
		connect(fast, &QTimer::timeout, this, [this] { refresh(); });
		fast->start();
		auto *slow = new QTimer(this);
		slow->setInterval(2000);
		connect(slow, &QTimer::timeout, this, [this] { list_sources(); });
		slow->start();

		list_sources();
	}

	~SceneDirectorDock() override { obs_weak_source_release(weak_); }

private:
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

	void select_current()
	{
		obs_weak_source_release(weak_);
		weak_ = nullptr;
		log_seq_ = 0;
		log_->clear();
		ports_loaded_ = false;
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

	/* spec 4.6: equal non-zero send / feedback ports are refused here, never written */
	void save_ports()
	{
		const int send = send_port_->value();
		const int fb = feedback_port_->value();
		if (fb != 0 && fb == send) {
			port_edit_msg_ = QString(obs_module_text("Dock.PortsEqual")).arg(fb);
			return;
		}
		port_edit_msg_.clear();
		const std::string host = host_->text().trimmed().toStdString();
		if (host.empty())
			return;
		with_source([&](obs_source_t *s) { sdapi::set_osc(s, host, send, fb); });
		ports_loaded_ = false; /* re-read what the source applied */
	}

	void save_cooldown()
	{
		const int beats = cooldown_->value();
		bool ok = true;
		if (have_mapping_[0])
			ok = sdapi::set_min_interval(kBreakdownId, beats) && ok;
		if (have_mapping_[1])
			ok = sdapi::set_min_interval(kDropId, beats) && ok;
		show_trig_msg(ok ? QString() : QString(obs_module_text("Dock.MapWriteFailed")));
	}

	void map_result(bool ok, const char *id)
	{
		show_trig_msg(ok ? QString() : QString(obs_module_text("Dock.MappingMissing")).arg(id));
	}

	void show_trig_msg(const QString &msg)
	{
		trig_msg_->setText(msg);
		trig_msg_->setVisible(!msg.isEmpty());
	}

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
			set_chip(0, obs_module_text("OscSending"));
			return;
		}

		if (st.clock_following) {
			clock_->setText(QString("MD %1 BPM").arg(st.bpm, 0, 'f', 1));
			clock_->setToolTip(obs_module_text("Clock.Md"));
		} else {
			clock_->setText(obs_module_text("Clock.Waiting"));
			clock_->setToolTip(obs_module_text("Clock.Waiting"));
		}
		const int sec = std::clamp(st.section, 0, 2);
		section_->setText(kSectionNames[sec]);
		section_->setStyleSheet(QString("background:%1; color:white; font-weight:bold; padding:2px 6px; "
						"border-radius:3px;")
						.arg(kSectionColours[sec]));

		/* OSC link chip: linked / sending / error (spec 4.6). Error is a socket error or a
		 * send-queue overflow only; a missed confirmation never turns it red. */
		switch (st.link_state) {
		case 1: /* cd::LinkState::Linked */
			set_chip(1, obs_module_text("OscLinked"));
			break;
		case 2: /* cd::LinkState::Error */
			set_chip(2, obs_module_text("OscError"));
			break;
		default:
			set_chip(0, obs_module_text("OscSending"));
			break;
		}
		traffic_->setText(QString(obs_module_text("Dock.Traffic"))
					  .arg((qulonglong)st.sent)
					  .arg((qulonglong)st.received)
					  .arg((qulonglong)st.dropped));

		/* host and ports: load from the source until the user edits them */
		if (!ports_loaded_ && !host_->hasFocus()) {
			host_->setText(text_of(st.osc_host));
			ports_loaded_ = true;
		}
		sync_spin(send_port_, st.osc_send_port);
		sync_spin(feedback_port_, st.osc_feedback_port);
		QString warn;
		if (!port_edit_msg_.isEmpty())
			warn = port_edit_msg_;
		else if (st.port_conflict)
			warn = QString(obs_module_text("Dock.PortConflict")).arg(st.osc_feedback_port);
		else if (st.osc_feedback_port != 0 && !st.feedback_bound)
			warn = QString(obs_module_text("Dock.PortInUse")).arg(st.osc_feedback_port);
		port_warn_->setText(warn);
		port_warn_->setVisible(!warn.isEmpty());

		confirm_->setText(QString(obs_module_text("Dock.Confirm"))
					  .arg(ms_of(st.last_latency_ns))
					  .arg(ms_of(st.median_latency_ns))
					  .arg((qlonglong)st.confirmed)
					  .arg((qlonglong)st.missed)
					  .arg((qlonglong)st.mismatched)
					  .arg((qlonglong)st.external));

		/* triggers from the shared event map */
		const sdapi::Status::MappingInfo *bd = find_mapping(st, kBreakdownId);
		const sdapi::Status::MappingInfo *dr = find_mapping(st, kDropId);
		have_mapping_[0] = bd != nullptr;
		have_mapping_[1] = dr != nullptr;
		sync_check(breakdown_, bd);
		sync_check(drop_, dr);
		cooldown_->setEnabled(bd || dr);
		if (dr || bd)
			sync_spin(cooldown_, (dr ? dr : bd)->min_interval_beats);
		sync_spin(lead_, (int)std::lround(st.lead_ms));

		/* map path + last load summary */
		map_file_ = text_of(st.map_path);
		map_path_->setText(map_file_.isEmpty() ? "-" : QFileInfo(map_file_).fileName());
		map_path_->setToolTip(map_file_);
		open_dir_->setEnabled(!map_file_.isEmpty());
		map_summary_->setText(text_of(st.map_summary));

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
		log_head_->setText(QString("%1 (%2)").arg(obs_module_text("Head.Log")).arg((qulonglong)st.switches));
	}

	void set_chip(int state, const QString &text)
	{
		const char *col = state == 1 ? "#1f6a45" : (state == 2 ? "#7f1d1d" : "#495057");
		chip_->setText(text);
		chip_->setStyleSheet(
			QString("background:%1; color:white; font-weight:bold; padding:2px 6px; border-radius:3px;")
				.arg(col));
	}

private:
	obs_weak_source_t *weak_ = nullptr;
	QComboBox *sources_;
	QLabel *clock_, *section_, *chip_, *traffic_, *port_warn_, *confirm_, *trig_msg_, *map_path_, *map_summary_,
		*log_head_;
	QLineEdit *host_;
	QSpinBox *send_port_, *feedback_port_, *cooldown_, *lead_;
	QPushButton *apply_ports_, *next_btn_, *prev_btn_, *reload_, *open_dir_;
	QCheckBox *breakdown_, *drop_;
	QPlainTextEdit *log_;
	QString map_file_, port_edit_msg_;
	bool ports_loaded_ = false;
	bool have_mapping_[2] = {false, false};
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
