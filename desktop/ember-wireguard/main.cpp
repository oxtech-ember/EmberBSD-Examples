// SPDX-License-Identifier: BSD-2-Clause
// ember-wireguard: Qt 6 manager for WireGuard tunnels via wg + wireguard-go.
// Layout mirrors the official WireGuard client (tunnel list, status,
// Activate/Deactivate, Import, Add Empty Tunnel, Edit, Delete, public key,
// transfer). Implementation is independent of the Windows/Swift sources.
#include "tunnel.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSaveFile>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>
#include <QVBoxLayout>

#include <KDBusService>

#include <cstdio>
#include <cstring>
#include <optional>
#include <sys/stat.h>

static QString uiText(const char *text)
{
	return QCoreApplication::translate("WireGuard", text);
}

struct TunnelRow {
	QString name;
	QString state;	 // "up" / "down"
	QString iface;
	QString path;
	QString publicKey;
	QString address;
	unsigned long long rx = 0;
	unsigned long long tx = 0;
};

static const char *kCtl = "/usr/pkg/libexec/ember-wireguard-ctl";
static const char *kWg = "/usr/pkg/bin/wg";
static const char *kAppVersion = "0.1.3";

static QIcon vpnIcon(bool connected)
{
	QPixmap pm(64, 64);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	const QPalette palette = QApplication::palette();
	const QColor active = palette.color(QPalette::Active, QPalette::WindowText);
	const QColor accent = connected ? QColor(46, 160, 90) : active;
	// Neutral tunnel glyph (not the WireGuard trademark logo).
	p.setPen(QPen(accent, 5, Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(QRectF(14, 22, 36, 26), 8, 8);
	p.drawArc(QRectF(22, 8, 20, 20), 0, 180 * 16);
	p.setPen(QPen(accent, 3));
	p.drawText(QRectF(14, 22, 36, 26), Qt::AlignCenter, QStringLiteral("WG"));
	if (!connected) {
		p.setPen(QPen(QColor(230, 80, 80), 6, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(44, 8, 60, 24);
		p.drawLine(60, 8, 44, 24);
	}
	return QIcon(pm);
}

static QByteArray runCmd(const QString &bin, const QStringList &args, int *code = nullptr)
{
	QProcess p;
	p.start(bin, args);
	if (!p.waitForStarted(3000)) {
		if (code)
			*code = 127;
		return {};
	}
	if (!p.waitForFinished(60000)) {
		p.kill();
		p.waitForFinished();
		if (code)
			*code = 124;
		return QByteArrayLiteral("Helper timed out; check tunnel state before retrying.");
	}
	if (code)
		*code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : 125;
	return p.readAllStandardOutput() + p.readAllStandardError();
}

static bool savePrivateFile(const QString &path, const QByteArray &data)
{
	QSaveFile out(path);
	out.setDirectWriteFallback(false);
	const mode_t oldMask = umask(077);
	const bool opened = out.open(QIODevice::WriteOnly);
	umask(oldMask);
	if (!opened)
		return false;
	if (!out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
	    out.write(data) != data.size()) {
		out.cancelWriting();
		return false;
	}
	return out.commit();
}

static QByteArray runCtl(const QStringList &args, int *code = nullptr)
{
	return runCmd(QString::fromUtf8(kCtl), args, code);
}

static QString userConfigDir()
{
	const QString home = QDir::homePath();
	const QString preferred = home + QStringLiteral("/.config/ember-wireguard");
	QDir().mkpath(preferred);
	return preferred;
}

static bool isUserOwnedPath(const QString &path)
{
	const QString home = QDir::homePath();
	return path.startsWith(home + QStringLiteral("/.config/ember-wireguard/")) ||
	    path.startsWith(home + QStringLiteral("/.config/wireguard/"));
}

static QVector<TunnelRow> parseList(const QByteArray &out)
{
	QVector<TunnelRow> rows;
	const QList<QByteArray> lines = out.split('\n');
	for (const QByteArray &line : lines) {
		if (line.trimmed().isEmpty())
			continue;
		const QList<QByteArray> cols = line.split('\t');
		if (cols.size() < 4)
			continue;
		TunnelRow r;
		r.name = QString::fromUtf8(cols[0]);
		r.state = QString::fromUtf8(cols[1]);
		r.iface = QString::fromUtf8(cols[2]);
		r.path = QString::fromUtf8(cols[3]);
		rows.push_back(r);
	}
	return rows;
}

static bool nameTaken(const QVector<TunnelRow> &rows, const QString &name,
    const QString &exceptPath = {})
{
	for (const TunnelRow &row : rows)
		if (row.name == name && (exceptPath.isEmpty() || row.path != exceptPath))
			return true;
	return false;
}

static void fillDetail(TunnelRow &r)
{
	int code = 0;
	const QByteArray out = runCtl({QStringLiteral("detail"), r.name}, &code);
	if (code != 0)
		return;
	const QList<QByteArray> lines = out.split('\n');
	for (int i = 0; i < lines.size(); ++i) {
		const QByteArray &line = lines[i];
		if (line.trimmed().isEmpty())
			continue;
		const QList<QByteArray> cols = line.split('\t');
		if (i == 0 && cols.size() >= 4) {
			r.state = QString::fromUtf8(cols[1]);
			r.iface = QString::fromUtf8(cols[2]);
			r.path = QString::fromUtf8(cols[3]);
			continue;
		}
		if (cols.size() < 2)
			continue;
		const QString key = QString::fromUtf8(cols[0]);
		const QString val = QString::fromUtf8(cols[1]);
		if (key == QLatin1String("public_key"))
			r.publicKey = val;
		else if (key == QLatin1String("address"))
			r.address = val;
		else if (key == QLatin1String("transfer_rx"))
			r.rx = val.toULongLong();
		else if (key == QLatin1String("transfer_tx"))
			r.tx = val.toULongLong();
	}
}

static QString genPrivateKey()
{
	int code = 0;
	const QByteArray out = runCmd(QString::fromUtf8(kWg), {QStringLiteral("genkey")},
	    &code);
	if (code != 0)
		return {};
	return QString::fromUtf8(out).trimmed();
}

class EditDialog : public QDialog {
public:
	EditDialog(const QString &title, const QString &name, const QString &text,
	    QWidget *parent = nullptr)
	    : QDialog(parent)
	{
		setWindowTitle(title);
		resize(560, 480);
		nameEdit_ = new QLineEdit(name, this);
		nameEdit_->setPlaceholderText(uiText("Name"));
		editor_ = new QPlainTextEdit(text, this);
		editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		auto *buttons = new QDialogButtonBox(
		    QDialogButtonBox::Save | QDialogButtonBox::Discard, this);
		buttons->button(QDialogButtonBox::Discard)->setText(uiText("Discard"));
		buttons->button(QDialogButtonBox::Save)->setText(uiText("Save"));
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		auto *form = new QFormLayout;
		form->addRow(uiText("Name:"), nameEdit_);
		auto *lay = new QVBoxLayout(this);
		lay->addLayout(form);
		lay->addWidget(editor_, 1);
		lay->addWidget(buttons);
	}

	QString tunnelName() const { return nameEdit_->text().trimmed(); }
	QString confText() const { return editor_->toPlainText(); }

private:
	QLineEdit *nameEdit_;
	QPlainTextEdit *editor_;
};

class Manager : public QWidget {
public:
	Manager()
	{
		setWindowTitle(QStringLiteral("WireGuard"));
		resize(720, 460);

		list_ = new QListWidget(this);
		list_->setIconSize(QSize(28, 28));
		list_->setMinimumWidth(200);
		list_->setAlternatingRowColors(true);

		auto *importBtn = new QPushButton(uiText("Import tunnel(s) from file"),
		    this);
		auto *addBtn = new QPushButton(uiText("Add Empty Tunnel…"), this);
		auto *leftButtons = new QVBoxLayout;
		leftButtons->addWidget(importBtn);
		leftButtons->addWidget(addBtn);

		auto *left = new QWidget(this);
		auto *leftLay = new QVBoxLayout(left);
		leftLay->setContentsMargins(0, 0, 0, 0);
		auto *tunnelsLabel = new QLabel(uiText("Tunnels"), left);
		QFont hf = tunnelsLabel->font();
		hf.setBold(true);
		tunnelsLabel->setFont(hf);
		leftLay->addWidget(tunnelsLabel);
		leftLay->addWidget(list_, 1);
		leftLay->addLayout(leftButtons);

		nameLabel_ = new QLabel(QStringLiteral("—"), this);
		QFont title = nameLabel_->font();
		title.setPointSize(title.pointSize() + 4);
		title.setBold(true);
		nameLabel_->setFont(title);

		statusLabel_ = new QLabel(uiText("Status: Inactive"), this);
		pubkeyLabel_ = new QLabel(uiText("Public key: (unknown)"), this);
		pubkeyLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
		pubkeyLabel_->setWordWrap(true);
		addressLabel_ = new QLabel(uiText("Addresses: (none)"), this);
		addressLabel_->setWordWrap(true);
		transferLabel_ = new QLabel(uiText("Transfer: —"), this);
		ifaceLabel_ = new QLabel(QString(), this);
		ifaceLabel_->setStyleSheet(QStringLiteral("color: gray;"));

		toggleBtn_ = new QPushButton(uiText("Activate"), this);
		editBtn_ = new QPushButton(uiText("Edit"), this);
		deleteBtn_ = new QPushButton(uiText("Delete"), this);
		auto *actionRow = new QHBoxLayout;
		actionRow->addWidget(toggleBtn_);
		actionRow->addWidget(editBtn_);
		actionRow->addWidget(deleteBtn_);
		actionRow->addStretch();

		auto *right = new QWidget(this);
		auto *rightLay = new QVBoxLayout(right);
		rightLay->setContentsMargins(8, 0, 0, 0);
		rightLay->addWidget(nameLabel_);
		rightLay->addSpacing(8);
		rightLay->addWidget(statusLabel_);
		rightLay->addWidget(pubkeyLabel_);
		rightLay->addWidget(addressLabel_);
		rightLay->addWidget(transferLabel_);
		rightLay->addWidget(ifaceLabel_);
		rightLay->addStretch();
		rightLay->addLayout(actionRow);

		auto *split = new QSplitter(Qt::Horizontal, this);
		split->addWidget(left);
		split->addWidget(right);
		split->setStretchFactor(0, 1);
		split->setStretchFactor(1, 2);

		auto *root = new QVBoxLayout(this);
		root->addWidget(split, 1);

		connect(importBtn, &QPushButton::clicked, this, [this] { importTunnels(); });
		connect(addBtn, &QPushButton::clicked, this, [this] { addEmptyTunnel(); });
		connect(toggleBtn_, &QPushButton::clicked, this, [this] { toggleSelected(); });
		connect(editBtn_, &QPushButton::clicked, this, [this] { editSelected(); });
		connect(deleteBtn_, &QPushButton::clicked, this, [this] { deleteSelected(); });
		connect(list_, &QListWidget::currentRowChanged, this,
		    [this](int) { showSelectedDetail(); });

		tray_ = new QSystemTrayIcon(vpnIcon(false), this);
		auto *menu = new QMenu(this);
		menu->addAction(uiText("Manage tunnels…"), this,
		    [this] { showWindow(); });
		menu->addAction(uiText("Import tunnel(s) from file…"), this,
		    [this] {
			    showWindow();
			    importTunnels();
		    });
		menu->addSeparator();
		menu->addAction(uiText("Exit"), qApp, &QApplication::quit);
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
		connect(timer, &QTimer::timeout, this, [this] { refresh(); });
		timer->start(2000);
		refresh();
	}

	void showWindow()
	{
		refresh();
		show();
		raise();
		activateWindow();
	}

protected:
	void closeEvent(QCloseEvent *e) override
	{
		hide();
		e->ignore();
	}

private:
	std::optional<TunnelRow> selected() const
	{
		auto *it = list_->currentItem();
		if (!it)
			return std::nullopt;
		const QString name = it->data(Qt::UserRole).toString();
		for (const TunnelRow &r : rows_) {
			if (r.name == name)
				return r;
		}
		return std::nullopt;
	}

	void refresh()
	{
		int code = 0;
		const QByteArray out = runCtl({QStringLiteral("list")}, &code);
		if (code != 0) {
			rows_.clear();
			list_->clear();
			showSelectedDetail();
			statusLabel_->setText(code == 127 ?
			    uiText("Status: helper unavailable") :
			    uiText("Status: helper error (%1)").arg(code));
			tray_->setIcon(vpnIcon(false));
			tray_->setToolTip(uiText("WireGuard status unavailable"));
			return;
		}
		const QString keep = selected() ? selected()->name : QString();
		rows_ = parseList(out);
		for (TunnelRow &r : rows_)
			fillDetail(r);

		list_->clear();
		int upCount = 0;
		bool failedTunnel = false;
		QString upNames;
		for (const TunnelRow &r : rows_) {
			const bool up = r.state == QLatin1String("up");
			failedTunnel |= r.state == QLatin1String("failed");
			if (up) {
				++upCount;
				if (!upNames.isEmpty())
					upNames += QLatin1String(", ");
				upNames += r.name;
			}
			auto *it = new QListWidgetItem(vpnIcon(up),
			    r.name + QLatin1Char('\n') +
				(up ? uiText("Active") :
				 r.state == QLatin1String("failed") ? uiText("Failed") :
				      uiText("Inactive")),
			    list_);
			it->setData(Qt::UserRole, r.name);
			QFont f = it->font();
			f.setBold(up);
			it->setFont(f);
			if (r.name == keep)
				list_->setCurrentItem(it);
		}
		if (list_->currentRow() < 0 && list_->count() > 0)
			list_->setCurrentRow(0);

		tray_->setIcon(vpnIcon(upCount > 0));
		tray_->setToolTip(upCount > 0 ?
			QStringLiteral("WireGuard: %1").arg(upNames) :
			failedTunnel ? uiText("WireGuard: cleanup required") :
			uiText("WireGuard: Deactivated"));
		showSelectedDetail();
	}

	void showSelectedDetail()
	{
		auto row = selected();
		if (!row) {
			nameLabel_->setText(QStringLiteral("—"));
			statusLabel_->setText(uiText("Status: Inactive"));
			pubkeyLabel_->setText(uiText("Public key: (unknown)"));
			addressLabel_->setText(uiText("Addresses: (none)"));
			transferLabel_->setText(uiText("Transfer: —"));
			ifaceLabel_->clear();
			toggleBtn_->setEnabled(false);
			editBtn_->setEnabled(false);
			deleteBtn_->setEnabled(false);
			return;
		}
		const bool up = row->state == QLatin1String("up");
		nameLabel_->setText(row->name);
		const bool failed = row->state == QLatin1String("failed");
		statusLabel_->setText(up ? uiText("Status: Active") :
			failed ? uiText("Status: Failed (cleanup required)") :
				 uiText("Status: Inactive"));
		pubkeyLabel_->setText(uiText("Public key: %1")
					  .arg(row->publicKey.isEmpty() ?
						  uiText("(unknown)") :
						  row->publicKey));
		addressLabel_->setText(uiText("Addresses: %1")
					   .arg(row->address.isEmpty() ?
						   uiText("(none)") :
						   row->address));
		if (up) {
			transferLabel_->setText(uiText("Transfer: %1 received, %2 sent")
						    .arg(QString::fromStdString(
							     tunnel::formatBytes(row->rx)),
							QString::fromStdString(
							    tunnel::formatBytes(row->tx))));
			ifaceLabel_->setText(uiText("Interface: %1").arg(row->iface));
		} else {
			transferLabel_->setText(uiText("Transfer: —"));
			ifaceLabel_->clear();
		}
		toggleBtn_->setEnabled(true);
		toggleBtn_->setText((up || failed) ? uiText("Deactivate") :
					 uiText("Activate"));
		editBtn_->setEnabled(true);
		deleteBtn_->setEnabled(isUserOwnedPath(row->path));
	}

	void toggleSelected()
	{
		auto row = selected();
		if (!row)
			return;
		const bool down = row->state == QLatin1String("up") ||
		    row->state == QLatin1String("failed");
		int code = 0;
		const QByteArray out = runCtl(
		    {down ? QStringLiteral("down") : QStringLiteral("up"), row->name}, &code);
		if (code != 0) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Failed to %1 «%2»:\n%3")
				.arg(down ? uiText("deactivate") :
					  uiText("activate"),
				    row->name, QString::fromUtf8(out).trimmed()));
		}
		refresh();
	}

	void importTunnels()
	{
		const QStringList files = QFileDialog::getOpenFileNames(this,
		    uiText("Import tunnel(s) from file"), QDir::homePath(),
		    uiText("WireGuard tunnels (*.conf);;All files (*)"), nullptr,
		    QFileDialog::DontUseNativeDialog);
		if (files.isEmpty())
			return;
		const QString destDir = userConfigDir();
		int ok = 0;
		for (const QString &src : files) {
			QFile f(src);
			if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
				continue;
			const QByteArray data = f.readAll();
			f.close();
			QString stem = QFileInfo(src).completeBaseName();
			stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
			    QStringLiteral("_"));
			if (!tunnel::validName(stem.toStdString()))
				stem = QStringLiteral("tunnel");
			QString candidate = stem;
			QString dest = destDir + QLatin1Char('/') + candidate +
			    QStringLiteral(".conf");
			int n = 2;
			while (nameTaken(rows_, candidate) || QFile::exists(dest)) {
				candidate = stem + QLatin1Char('-') + QString::number(n++);
				dest = destDir + QLatin1Char('/') + candidate + QStringLiteral(".conf");
			}
			if (!savePrivateFile(dest, data))
				continue;
			TunnelRow imported;
			imported.name = candidate;
			imported.path = dest;
			rows_.push_back(imported);
			++ok;
		}
		if (ok == 0) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("No tunnels were imported."));
		}
		refresh();
	}

	void addEmptyTunnel()
	{
		bool ok = false;
		QString name = QInputDialog::getText(this, uiText("Add Empty Tunnel"),
		    uiText("Tunnel name:"), QLineEdit::Normal,
		    QStringLiteral("new-tunnel"), &ok);
		if (!ok)
			return;
		name = name.trimmed();
		name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
		    QStringLiteral("_"));
		if (!tunnel::validName(name.toStdString())) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Invalid tunnel name."));
			return;
		}
		const QString priv = genPrivateKey();
		if (priv.isEmpty()) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("wg genkey failed."));
			return;
		}
		const QString text = QString::fromStdString(
		    tunnel::emptyTunnelTemplate(priv.toStdString()));
		EditDialog dlg(uiText("Create new tunnel"), name, text, this);
		if (dlg.exec() != QDialog::Accepted)
			return;
		saveTunnel(dlg.tunnelName(), dlg.confText(), {});
	}

	void editSelected()
	{
		auto row = selected();
		if (!row)
			return;
		QFile f(row->path);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Cannot read %1").arg(row->path));
			return;
		}
		const QString text = QString::fromUtf8(f.readAll());
		f.close();
		EditDialog dlg(uiText("Edit tunnel"), row->name, text, this);
		if (!isUserOwnedPath(row->path))
			dlg.setWindowTitle(uiText("Edit tunnel (read-only source)"));
		if (dlg.exec() != QDialog::Accepted)
			return;
		if ((row->state == QLatin1String("up") ||
		     row->state == QLatin1String("failed")) &&
		    dlg.tunnelName() != row->name) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Deactivate the tunnel before renaming it."));
			return;
		}
		saveTunnel(dlg.tunnelName(), dlg.confText(), row->path);
	}

	void saveTunnel(const QString &name, const QString &text, const QString &oldPath)
	{
		QString n = name.trimmed();
		if (!tunnel::validName(n.toStdString())) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Invalid tunnel name."));
			return;
		}
		tunnel::Conf conf;
		if (!tunnel::parseConfText(text.toStdString(), conf)) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Configuration needs Interface Address."));
			return;
		}
		QString dest;
		if (!oldPath.isEmpty() && isUserOwnedPath(oldPath) &&
		    tunnel::basenameStem(oldPath.toStdString()) == n.toStdString()) {
			dest = oldPath;
		} else {
			dest = userConfigDir() + QLatin1Char('/') + n +
			    QStringLiteral(".conf");
		}
		if (nameTaken(rows_, n, oldPath) || (dest != oldPath && QFile::exists(dest))) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Tunnel already exists: %1").arg(dest));
			return;
		}
		QByteArray data = text.toUtf8();
		if (!data.endsWith('\n'))
			data.append('\n');
		if (!savePrivateFile(dest, data)) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Cannot save %1").arg(dest));
			return;
		}
		if (!oldPath.isEmpty() && oldPath != dest && isUserOwnedPath(oldPath))
			QFile::remove(oldPath);
		refresh();
	}

	void deleteSelected()
	{
		auto row = selected();
		if (!row || !isUserOwnedPath(row->path))
			return;
		const auto reply = QMessageBox::question(this, QStringLiteral("WireGuard"),
		    uiText("Delete tunnel ‘%1’?").arg(row->name));
		if (reply != QMessageBox::Yes)
			return;
		if (row->state == QLatin1String("up") ||
		    row->state == QLatin1String("failed")) {
			int code = 0;
			const QByteArray out = runCtl(
			    {QStringLiteral("down"), row->name}, &code);
			if (code != 0) {
				QMessageBox::warning(this, QStringLiteral("WireGuard"),
				    uiText("Failed to deactivate ‘%1’:\n%2")
					.arg(row->name, QString::fromUtf8(out).trimmed()));
				refresh();
				return;
			}
		}
		if (!QFile::remove(row->path)) {
			QMessageBox::warning(this, QStringLiteral("WireGuard"),
			    uiText("Cannot delete %1").arg(row->path));
		}
		refresh();
	}

	QListWidget *list_;
	QLabel *nameLabel_;
	QLabel *statusLabel_;
	QLabel *pubkeyLabel_;
	QLabel *addressLabel_;
	QLabel *transferLabel_;
	QLabel *ifaceLabel_;
	QPushButton *toggleBtn_;
	QPushButton *editBtn_;
	QPushButton *deleteBtn_;
	QSystemTrayIcon *tray_;
	QVector<TunnelRow> rows_;
};

int main(int argc, char **argv)
{
	bool offscreen = false;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--version") == 0) {
			std::printf("ember-wireguard %s\n", kAppVersion);
			return 0;
		}
		if (std::strcmp(argv[i], "--offscreen-smoke") == 0)
			offscreen = true;
	}
	if (offscreen)
		qputenv("QT_QPA_PLATFORM", "offscreen");

	QString language;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--language") == 0) {
			if (++i >= argc || (std::strcmp(argv[i], "ru") != 0 &&
			    std::strcmp(argv[i], "en") != 0)) {
				std::fprintf(stderr, "--language requires ru or en\n");
				return 2;
			}
			language = QString::fromUtf8(argv[i]);
		}
	}
	QApplication app(argc, argv);
	QTranslator translator;
	QTranslator qtTranslator;
	if (language == QLatin1String("ru") ||
	    (language.isEmpty() && QLocale::system().language() == QLocale::Russian)) {
		if (!translator.load(QStringLiteral(":/translations/ember-wireguard_ru.qm"))) {
			std::fprintf(stderr, "Cannot load Russian translation\n");
			return 1;
		}
		if (!qtTranslator.load(QLocale(QLocale::Russian), QStringLiteral("qtbase"),
		    QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
			std::fprintf(stderr, "Cannot load Qt Russian translation (qt6-qttranslations)\n");
			return 1;
		}
		app.installTranslator(&qtTranslator);
		app.installTranslator(&translator);
	}
	app.setQuitOnLastWindowClosed(false);
	app.setApplicationName(QStringLiteral("ember-wireguard"));
	app.setApplicationVersion(QString::fromUtf8(kAppVersion));
	app.setOrganizationDomain(QStringLiteral("emberbsd.org"));
	app.setDesktopFileName(QStringLiteral("ember-wireguard"));
	app.setWindowIcon(vpnIcon(false));

	if (offscreen) {
		Manager m;
		m.showWindow();
		QTimer::singleShot(200, &app, &QApplication::quit);
		return app.exec();
	}

	KDBusService service(KDBusService::Unique);
	Manager m;
	QObject::connect(&service, &KDBusService::activateRequested, &m,
	    [&m](const QStringList &, const QString &) { m.showWindow(); });
	if (app.arguments().contains(QStringLiteral("--show")))
		QTimer::singleShot(0, &m, [&m] { m.showWindow(); });
	return app.exec();
}
