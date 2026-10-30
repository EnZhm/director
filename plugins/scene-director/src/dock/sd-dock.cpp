/*
Scene Director dock: the control surface for the set. It picks the Scene Director source, shows the
Music Director clock and section, the OSC link chip, the Next / Prev buttons and the log.

This 2.1 dock is a stripped placeholder: the MIDI device picker and message editors are gone (MIDI
out was removed), and the full OSC control surface (host / ports, latency stats, map editor) is
rebuilt by the SD-DOCK card. It reads the source through sdapi (thread-safe); its refresh timer
also steps the scheduler (sdapi::tick) so the feature keeps working when the source is not in the
active scene.
*/
#include "sd-dock.h"

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QComboBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
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

		/* OSC link chip */
		auto *link = new QHBoxLayout();
		chip_ = new QLabel(obs_module_text("OscSending"));
		chip_->setAlignment(Qt::AlignCenter);
		chip_->setMinimumWidth(112);
		link->addWidget(chip_);
		link->addStretch(1);
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

		connect(sources_, &QComboBox::currentIndexChanged, this, [this](int) { select_current(); });
		connect(next_btn_, &QPushButton::clicked, this,
			[this] { with_source([](obs_source_t *s) { sdapi::manual_next(s); }); });
		connect(prev_btn_, &QPushButton::clicked, this,
			[this] { with_source([](obs_source_t *s) { sdapi::manual_prev(s); }); });

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

		/* OSC link chip: linked / sending / error (spec 4.6). */
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
		chip_->setToolTip(QString("%1:%2 (feedback %3, %4)")
					  .arg(text_of(st.osc_host))
					  .arg(st.osc_send_port)
					  .arg(st.osc_feedback_port)
					  .arg(st.feedback_bound ? "bound" : "unbound"));

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
					 .arg((qulonglong)st.switches)
					 .arg((qulonglong)st.confirmed)
					 .arg((qulonglong)st.missed)
					 .arg((qulonglong)st.mismatched)
					 .arg((qulonglong)st.external));
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
	QLabel *clock_, *section_, *chip_, *log_head_, *footer_;
	QPushButton *next_btn_, *prev_btn_;
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
