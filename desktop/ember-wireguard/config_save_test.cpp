// SPDX-License-Identifier: BSD-2-Clause
#define main ember_wireguard_gui_main
#include "main.cpp"
#undef main

#include <QTemporaryDir>
#include <sys/resource.h>
#include <sys/stat.h>
#include <signal.h>
#include <cstdio>

static bool check(bool ok, const char *what)
{
	if (!ok) std::fprintf(stderr, "FAIL: %s\n", what);
	return ok;
}

static QByteArray readFile(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) return {};
	return f.readAll();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (!check(uiText("Activate") == QStringLiteral("Activate"),
	    "English default")) return 1;
	QTranslator translator;
	if (!check(translator.load(QStringLiteral(":/translations/ember-wireguard_ru.qm")),
	    "load Russian translation")) return 1;
	app.installTranslator(&translator);
	if (!check(uiText("Activate") == QString::fromUtf8("Подключить"),
	    "Russian button translation")) return 1;
	if (!check(uiText("Deactivate") == QString::fromUtf8("Отключить"),
	    "Russian deactivate button")) return 1;
	QTemporaryDir dir;
	if (!check(dir.isValid(), "temporary directory")) return 1;
	const QString path = dir.filePath(QStringLiteral("test.conf"));
	struct stat st;
	bool ok = check(savePrivateFile(path, "first"), "create");
	ok &= check(::stat(QFile::encodeName(path).constData(), &st) == 0 &&
	    (st.st_mode & 0777) == 0600, "new file mode 0600");
	ok &= check(::chmod(QFile::encodeName(path).constData(), 0644) == 0,
	    "prepare existing file");
	ok &= check(savePrivateFile(path, "original"), "replace");
	ok &= check(::stat(QFile::encodeName(path).constData(), &st) == 0 &&
	    (st.st_mode & 0777) == 0600 && readFile(path) == "original",
	    "replacement mode and content");
	struct rlimit oldLimit;
	if (!check(getrlimit(RLIMIT_FSIZE, &oldLimit) == 0, "getrlimit")) return 1;
	struct rlimit limit = oldLimit;
	limit.rlim_cur = 1024;
	if (!check(oldLimit.rlim_max == RLIM_INFINITY || oldLimit.rlim_max >= 1024,
	    "file limit available")) return 1;
	auto oldSignal = signal(SIGXFSZ, SIG_IGN);
	if (!check(oldSignal != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limit) == 0,
	    "setrlimit")) return 1;
	const bool saved = savePrivateFile(path, QByteArray(8192, 'x'));
	const bool restored = setrlimit(RLIMIT_FSIZE, &oldLimit) == 0;
	signal(SIGXFSZ, oldSignal);
	ok &= check(restored, "restore file limit");
	ok &= check(!saved && readFile(path) == "original",
	    "failed write preserves original");
	const auto rows = parseList("test\tfailed\ttun0\t/tmp/test.conf\n");
	ok &= check(rows.size() == 1 && rows[0].state == QLatin1String("failed"),
	    "failed state parsed");
	ok &= check(nameTaken(rows, QStringLiteral("test")) &&
	    !nameTaken(rows, QStringLiteral("test"), QStringLiteral("/tmp/test.conf")) &&
	    !nameTaken(rows, QStringLiteral("other")), "profile name collision");
	return ok ? 0 : 1;
}
