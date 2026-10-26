// SPDX-License-Identifier: BSD-2-Clause
// ember-wifi: a small Wi-Fi manager for wpa_supplicant (tray icon and
// network list).  It talks to the control socket directly, so passwords
// never appear on a command line.
#include "wpa.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

#include <KDBusService>

#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

#include <cstring>
#include <optional>

static const char *kCtrlDir = "/var/run/wpa_supplicant";

// Request/response client for the wpa_supplicant control socket.
class WpaCtrl {
public:
	~WpaCtrl() { close(); }

	bool open(const std::string &path)
	{
		close();
		fd_ = ::socket(AF_UNIX, SOCK_DGRAM, 0);
		if (fd_ < 0)
			return false;
		static int counter;
		local_ = "/tmp/ember-wifi-" + std::to_string(getpid()) + "-" +
		    std::to_string(counter++);
		::unlink(local_.c_str());
		sockaddr_un a{};
		a.sun_family = AF_UNIX;
		std::strncpy(a.sun_path, local_.c_str(), sizeof(a.sun_path) - 1);
		if (::bind(fd_, reinterpret_cast<sockaddr *>(&a), sizeof(a)) < 0) {
			close();
			return false;
		}
		sockaddr_un r{};
		r.sun_family = AF_UNIX;
		std::strncpy(r.sun_path, path.c_str(), sizeof(r.sun_path) - 1);
		if (::connect(fd_, reinterpret_cast<sockaddr *>(&r), sizeof(r)) < 0) {
			close();
			return false;
		}
		return true;
	}

	void close()
	{
		if (fd_ >= 0) {
			::close(fd_);
			::unlink(local_.c_str());
		}
		fd_ = -1;
	}

	bool isOpen() const { return fd_ >= 0; }

	// Returns false on a transport error; the reply goes to `out'.
	bool request(const std::string &cmd, std::string &out)
	{
		out.clear();
		if (fd_ < 0 || ::send(fd_, cmd.data(), cmd.size(), 0) < 0) {
			close();
			return false;
		}
		char buf[16384];
		for (;;) {
			pollfd p{fd_, POLLIN, 0};
			if (::poll(&p, 1, 3000) <= 0) {
				close();
				return false;
			}
			ssize_t n = ::recv(fd_, buf, sizeof(buf) - 1, 0);
			if (n < 0) {
				close();
				return false;
			}
			// Unsolicited events start with '<'; we did not attach,
			// but skip them anyway.
			if (n > 0 && buf[0] == '<')
				continue;
			out.assign(buf, size_t(n));
			return true;
		}
	}

	std::string ask(const std::string &cmd)
	{
		std::string out;
		request(cmd, out);
		return out;
	}

private:
	int fd_ = -1;
	std::string local_;
};

static QString q(const std::string &s) { return QString::fromStdString(s); }

static QIcon wifiIcon(int percent, bool connected)
{
	QPixmap pm(64, 64);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	int bars = !connected ? 0 : percent >= 70 ? 4 : percent >= 45 ? 3 :
	    percent >= 20 ? 2 : 1;
	const QPalette palette = QApplication::palette();
	const QColor active = palette.color(QPalette::Active, QPalette::WindowText);
	const QColor inactive = palette.color(QPalette::Disabled, QPalette::WindowText);
	QPointF c(32, 54);
	for (int i = 0; i < 4; i++) {
		QColor col = i < bars ? active : inactive;
		if (i == 0) {
			p.setPen(Qt::NoPen);
			p.setBrush(col);
			p.drawEllipse(c, 5, 5);
			continue;
		}
		p.setPen(QPen(col, 6, Qt::SolidLine, Qt::RoundCap));
		p.setBrush(Qt::NoBrush);
		qreal r = 12.0 * i + 4;
		p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 45 * 16, 90 * 16);
	}
	if (!connected) {
		p.setPen(QPen(QColor(230, 80, 80), 6, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(44, 8, 60, 24);
		p.drawLine(60, 8, 44, 24);
	}
	return QIcon(pm);
}

class Manager : public QWidget {
public:
	Manager()
	{
		setWindowTitle("Сети Wi-Fi");
		resize(420, 480);
		status_ = new QLabel(this);
		status_->setWordWrap(true);
		list_ = new QListWidget(this);
		list_->setIconSize(QSize(24, 24));
		auto *connectBtn = new QPushButton("Подключить", this);
		auto *forgetBtn = new QPushButton("Забыть сеть", this);
		auto *rescanBtn = new QPushButton("Обновить", this);
		auto *buttons = new QHBoxLayout;
		buttons->addWidget(connectBtn);
		buttons->addWidget(forgetBtn);
		buttons->addStretch();
		buttons->addWidget(rescanBtn);
		auto *lay = new QVBoxLayout(this);
		lay->addWidget(status_);
		lay->addWidget(list_, 1);
		lay->addLayout(buttons);

		connect(connectBtn, &QPushButton::clicked, this, [this] { connectSelected(); });
		connect(list_, &QListWidget::itemActivated, this, [this] { connectSelected(); });
		connect(forgetBtn, &QPushButton::clicked, this, [this] { forgetSelected(); });
		rescanBtn->setToolTip("При активном соединении обновляется сохранённый список сетей без разрыва связи.");
		connect(rescanBtn, &QPushButton::clicked, this, [this] { refreshScan(); });

		tray_ = new QSystemTrayIcon(wifiIcon(0, false), this);
		auto *menu = new QMenu(this);
		menu->addAction("Сети Wi-Fi…", this, [this] { showWindow(); });
		menu->addSeparator();
		menu->addAction("Выйти", qApp, &QApplication::quit);
		tray_->setContextMenu(menu);
		connect(tray_, &QSystemTrayIcon::activated, this,
		    [this](QSystemTrayIcon::ActivationReason r) {
			if (r == QSystemTrayIcon::Trigger ||
			    r == QSystemTrayIcon::DoubleClick) {
				if (isVisible())
					hide();
				else
					showWindow();
			}
		    });
		tray_->show();

		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] { tick(); });
		timer->start(2000);
		tick();
	}

	void showWindow()
	{
		refreshScan();
		show();
		raise();
		activateWindow();
	}

protected:
	void closeEvent(QCloseEvent *e) override
	{
		hide();		// keep running in the tray
		e->ignore();
	}

private:
	bool ensureCtrl()
	{
		if (ctrl_.isOpen())
			return true;
		QDir d(kCtrlDir);
		const auto entries = d.entryList(QDir::System | QDir::NoDotAndDotDot);
		for (const auto &e : entries) {
			if (ctrl_.open(d.filePath(e).toStdString())) {
				iface_ = e;
				return true;
			}
		}
		iface_.clear();
		return false;
	}

	std::string ctrlAsk(const std::string &cmd)
	{
		if (!ensureCtrl())
			return {};
		return ctrl_.ask(cmd);
	}

	void refreshScan()
	{
		auto st = wpa::parseKeyValues(ctrlAsk("STATUS"));
		ctrlAsk(wpa::scanRequest(st["wpa_state"]));
		refreshList();
	}

	void tick()
	{
		if (!ensureCtrl()) {
			status_->setText("Wi-Fi недоступен: wpa_supplicant не запущен "
			    "или нет беспроводного интерфейса.");
			tray_->setIcon(wifiIcon(0, false));
			tray_->setToolTip("Wi-Fi недоступен");
			return;
		}
		auto st = wpa::parseKeyValues(ctrlAsk("STATUS"));
		std::string state = st["wpa_state"];
		int sig = 0;
		auto poll = wpa::parseKeyValues(ctrlAsk("SIGNAL_POLL"));
		if (!poll["RSSI"].empty())
			sig = wpa::signalPercent(std::atoi(poll["RSSI"].c_str()));
		else
			sig = signalFor(st["ssid"]);
		QString text;
		bool connected = state == "COMPLETED";
		if (connected) {
			text = QString("Подключено к «%1»").arg(q(wpa::unescape(st["ssid"])));
			if (!st["ip_address"].empty())
				text += QString(", адрес %1").arg(q(st["ip_address"]));
			if (sig > 0)
				text += QString(", сигнал %1%").arg(sig);
		} else if (state == "SCANNING" || state == "DISCONNECTED" ||
		    state == "INACTIVE") {
			text = "Не подключено";
		} else {
			text = QString("Подключение… (%1)").arg(q(state));
		}
		status_->setText(QString("%1: %2").arg(iface_, text));
		tray_->setIcon(wifiIcon(sig, connected));
		tray_->setToolTip(text);

		if (pending_ >= 0)
			checkPending(connected, st);
		if (isVisible() && ++ticks_ % 2 == 0)
			refreshList();
		if (isVisible() && ticks_ % 10 == 0)
			ctrlAsk(wpa::scanRequest(state));
	}

	int signalFor(const std::string &ssid)
	{
		for (const auto &n : nets_)
			if (n.ssid == ssid)
				return wpa::signalPercent(n.signal);
		return 0;
	}

	void refreshList()
	{
		nets_ = wpa::groupNetworks(wpa::parseScan(ctrlAsk("SCAN_RESULTS")));
		saved_ = wpa::parseNetworks(ctrlAsk("LIST_NETWORKS"));
		QString keep = list_->currentItem() ?
		    list_->currentItem()->data(Qt::UserRole).toString() : QString();
		list_->clear();
		for (const auto &n : nets_) {
			QStringList info;
			if (n.band5 && n.band24)
				info << "2,4 и 5 ГГц";
			else
				info << (n.band5 ? "5 ГГц" : "2,4 ГГц");
			switch (n.security) {
			case wpa::Security::Open: info << "открытая"; break;
			case wpa::Security::Psk: info << "защищена паролем"; break;
			case wpa::Security::SaeOnly: info << "только WPA3, не поддерживается"; break;
			case wpa::Security::Enterprise: info << "WPA-Enterprise, не поддерживается"; break;
			case wpa::Security::Unsupported: info << "WEP, не поддерживается"; break;
			}
			int sig = wpa::signalPercent(n.signal);
			info << QString("сигнал %1%").arg(sig);
			int id = wpa::findNetwork(saved_, n.ssid);
			bool current = false;
			for (const auto &c : saved_)
				if (c.id == id && c.current)
					current = true;
			if (current)
				info << "подключено";
			else if (id >= 0)
				info << "сохранена";
			auto *it = new QListWidgetItem(wifiIcon(sig, true),
			    q(n.ssid) + "\n" + info.join(" · "), list_);
			it->setData(Qt::UserRole, q(n.ssid));
			QFont f = it->font();
			f.setBold(current);
			it->setFont(f);
			if (q(n.ssid) == keep)
				list_->setCurrentItem(it);
		}
	}

	// A copy: the dialogs below run a nested event loop, and the timer
	// may rebuild nets_ while they are open.
	std::optional<wpa::Network> selected()
	{
		auto *it = list_->currentItem();
		if (!it)
			return std::nullopt;
		std::string ssid = it->data(Qt::UserRole).toString().toStdString();
		for (const auto &n : nets_)
			if (n.ssid == ssid)
				return n;
		return std::nullopt;
	}

	void connectSelected()
	{
		const std::optional<wpa::Network> n = selected();
		if (!n)
			return;
		std::string ssid = n->ssid;
		int id = wpa::findNetwork(saved_, ssid);
		if (id >= 0) {
			startConnect(id, ssid, false);
			return;
		}
		if (n->security != wpa::Security::Open &&
		    n->security != wpa::Security::Psk) {
			QMessageBox::warning(this, "Сети Wi-Fi",
			    "Этот тип защиты пока не поддерживается драйвером.");
			return;
		}
		std::string pass;
		if (n->security == wpa::Security::Psk) {
			bool ok = false;
			QString p = QInputDialog::getText(this, "Пароль сети",
			    QString("Пароль для «%1»:").arg(q(ssid)),
			    QLineEdit::Password, QString(), &ok);
			if (!ok)
				return;
			pass = p.toStdString();
			if (!wpa::validPassphrase(pass)) {
				QMessageBox::warning(this, "Сети Wi-Fi",
				    "Пароль должен быть длиной от 8 до 63 символов "
				    "(латиница, цифры, знаки).");
				return;
			}
		}
		std::string r = ctrlAsk("ADD_NETWORK");
		id = std::atoi(r.c_str());
		if (r.empty() || r.compare(0, 4, "FAIL") == 0) {
			QMessageBox::warning(this, "Сети Wi-Fi", "wpa_supplicant не принял сеть.");
			return;
		}
		for (const auto &cmd : wpa::addNetworkCommands(id, ssid, pass, n->security)) {
			if (ctrlAsk(cmd).compare(0, 2, "OK") != 0) {
				ctrlAsk("REMOVE_NETWORK " + std::to_string(id));
				QMessageBox::warning(this, "Сети Wi-Fi",
				    "wpa_supplicant не принял параметры сети.");
				return;
			}
		}
		startConnect(id, ssid, true);
	}

	void forgetSelected()
	{
		const std::optional<wpa::Network> n = selected();
		if (!n)
			return;
		int id = wpa::findNetwork(saved_, n->ssid);
		if (id < 0)
			return;
		if (QMessageBox::question(this, "Сети Wi-Fi",
		    QString("Забыть сеть «%1» и её пароль?").arg(q(n->ssid))) !=
		    QMessageBox::Yes)
			return;
		ctrlAsk("REMOVE_NETWORK " + std::to_string(id));
		ctrlAsk("SAVE_CONFIG");
		refreshList();
	}

	void startConnect(int id, const std::string &ssid, bool isNew)
	{
		ctrlAsk("SELECT_NETWORK " + std::to_string(id));
		pending_ = id;
		pendingSsid_ = ssid;
		pendingNew_ = isNew;
		pendingTicks_ = 0;
		status_->setText(QString("Подключение к «%1»…").arg(q(ssid)));
	}

	void checkPending(bool connected, std::map<std::string, std::string> &st)
	{
		if (connected && wpa::unescape(st["ssid"]) == pendingSsid_) {
			// SELECT_NETWORK disabled every other network; re-enable
			// them so that known networks still connect automatically.
			ctrlAsk("ENABLE_NETWORK all");
			ctrlAsk("SAVE_CONFIG");
			pending_ = -1;
			refreshList();
			return;
		}
		if (++pendingTicks_ < 15)	// about 30 s
			return;
		int id = pending_;
		pending_ = -1;
		if (pendingNew_)
			ctrlAsk("REMOVE_NETWORK " + std::to_string(id));
		ctrlAsk("ENABLE_NETWORK all");
		ctrlAsk("SAVE_CONFIG");
		ctrlAsk("REASSOCIATE");
		QMessageBox::warning(this, "Сети Wi-Fi",
		    QString("Не удалось подключиться к «%1». Проверьте пароль "
		    "и попробуйте ещё раз.").arg(q(pendingSsid_)));
		refreshList();
	}

	WpaCtrl ctrl_;
	QString iface_;
	QLabel *status_;
	QListWidget *list_;
	QSystemTrayIcon *tray_;
	std::vector<wpa::Network> nets_;
	std::vector<wpa::Configured> saved_;
	int ticks_ = 0;
	int pending_ = -1;
	std::string pendingSsid_;
	bool pendingNew_ = false;
	int pendingTicks_ = 0;
};

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	app.setApplicationName("ember-wifi");
	app.setOrganizationDomain("emberbsd.org");
	app.setDesktopFileName("ember-wifi");
	app.setWindowIcon(QIcon::fromTheme(QStringLiteral("network-wireless")));
	KDBusService service(KDBusService::Unique);
	Manager m;
	QObject::connect(&service, &KDBusService::activateRequested, &m,
	    [&m](const QStringList &, const QString &) { m.showWindow(); });
	if (app.arguments().contains("--show"))
		QTimer::singleShot(0, &m, [&m] { m.showWindow(); });
	return app.exec();
}
