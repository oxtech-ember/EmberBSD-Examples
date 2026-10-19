// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 EmberBSD contributors.
// Origin: EmberBSD, AI-assisted. Check a rendered client through KWin capture.
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDeadlineTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVariantMap>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {
constexpr int width = 640;
constexpr int height = 480;
constexpr qsizetype maxBytes = 16 * 1024 * 1024;
struct Pipe {
    int fd[2] = {-1, -1};
    ~Pipe() { for (int value : fd) if (value >= 0) close(value); }
    Pipe() = default;
    Pipe(const Pipe &) = delete;
    Pipe &operator=(const Pipe &) = delete;
};

bool capture(const QString &path, int solidIndex = -1)
{
    if (solidIndex < -1 || solidIndex > 3) return false;
    Pipe channel;
    if (pipe(channel.fd) != 0) return false;
    for (int value : channel.fd) {
        const int flags = fcntl(value, F_GETFD);
        if (flags < 0 || fcntl(value, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
    }
    const int flags = fcntl(channel.fd[0], F_GETFL);
    if (flags < 0 || fcntl(channel.fd[0], F_SETFL, flags | O_NONBLOCK) < 0) return false;
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !(bus.connectionCapabilities() & QDBusConnection::UnixFileDescriptorPassing)) return false;
    QDBusUnixFileDescriptor descriptor(channel.fd[1]);
    if (!descriptor.isValid()) return false;
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin.ScreenShot2"),
        QStringLiteral("/org/kde/KWin/ScreenShot2"), QStringLiteral("org.kde.KWin.ScreenShot2"),
        QStringLiteral("CaptureWorkspace"));
    const QVariantMap options{{QStringLiteral("include-cursor"), false},
        {QStringLiteral("include-shadow"), false}, {QStringLiteral("hide-caller-windows"), false},
        {QStringLiteral("native-resolution"), true}};
    message << options << QVariant::fromValue(descriptor);
    const QDBusReply<QVariantMap> reply(bus.call(message, QDBus::Block, 10000));
    if (!reply.isValid()) { qCritical("KWin capture: %s", qPrintable(reply.error().message())); return false; }
    const auto info = reply.value();
    bool wok = false, hok = false, sok = false, fok = false;
    const auto w = info.value(QStringLiteral("width")).toUInt(&wok);
    const auto h = info.value(QStringLiteral("height")).toUInt(&hok);
    const auto stride = info.value(QStringLiteral("stride")).toUInt(&sok);
    const auto format = info.value(QStringLiteral("format")).toUInt(&fok);
    const quint64 count = quint64(stride) * h;
    if (!wok || !hok || !sok || !fok || w != width || h != height ||
        !stride || count > quint64(maxBytes) ||
        format <= QImage::Format_Invalid || format >= QImage::NImageFormats) return false;
    const auto imageFormat = static_cast<QImage::Format>(format);
    const QImage layout(width, height, imageFormat);
    if (layout.isNull() || stride < quint64(layout.bytesPerLine())) return false;
    QByteArray bytes(qsizetype(count), Qt::Uninitialized);
    qsizetype done = 0;
    QDeadlineTimer deadline(10000);
    while (done < bytes.size()) {
        if (deadline.hasExpired()) return false;
        const auto available = read(channel.fd[0], bytes.data() + done, size_t(bytes.size() - done));
        if (available > 0) { done += available; continue; }
        if (available == 0 || (errno != EINTR && errno != EAGAIN)) return false;
        if (deadline.hasExpired()) return false;
        pollfd item{channel.fd[0], POLLIN, 0};
        const int ready = poll(&item, 1, 100);
        if (ready < 0 && errno != EINTR) return false;
        if (ready > 0 && (item.revents & (POLLERR | POLLNVAL))) return false;
    }
    const QImage image(reinterpret_cast<const uchar *>(bytes.constData()), width, height,
        qsizetype(stride), imageFormat);
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) return false;
    constexpr std::array<std::array<unsigned char, 3>, 4> expected{{
        {{255, 0, 0}}, {{0, 255, 0}}, {{0, 0, 255}}, {{255, 255, 255}}}};
    for (int y = 0; y < height; ++y) {
        const auto *line = rgba.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const auto &pixel = expected[solidIndex >= 0 ? solidIndex :
                (y >= height / 2 ? 2 : 0) + (x >= width / 2 ? 1 : 0)];
            for (int c = 0; c < 3; ++c) {
                if (line[4 * x + c] != pixel[c]) {
                    qCritical("Pixel mismatch at %d,%d channel %d: %u != %u", x, y, c,
                        unsigned(line[4 * x + c]), unsigned(pixel[c])); return false;
                }
            }
            if (line[4 * x + 3] != 255) return false;
        }
    }
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    if (output.write("P6\n640 480\n255\n") != 15) return false;
    QByteArray row(width * 3, Qt::Uninitialized);
    for (int y = 0; y < height; ++y) {
        const auto *line = rgba.constScanLine(y);
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c) row[3 * x + c] = char(line[4 * x + c]);
        if (output.write(row) != row.size()) return false;
    }
    if (!output.flush()) return false;
    qInfo("PASS: 307200 composed pixels, format=%u stride=%u bytes=%lld", format, stride,
        static_cast<long long>(bytes.size()));
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) { std::fprintf(stderr, "usage: %s QUADRANTS_QML NEW_PPM EXPECTED_RENDERER [red|green|blue|white]\n", argv[0]); return 2; }
    int solidIndex = -1;
    if (argc == 5) {
        const QByteArray selected(argv[4]);
        const std::array<QByteArray, 4> names{{"red", "green", "blue", "white"}};
        for (int i = 0; i < 4; ++i) if (selected == names[i]) solidIndex = i;
        if (solidIndex < 0) return 2;
    }
    if (qEnvironmentVariableIsSet("QT_QUICK_BACKEND") || qEnvironmentVariableIsSet("MESA_GL_VERSION_OVERRIDE") ||
        qEnvironmentVariableIsSet("MESA_GLES_VERSION_OVERRIDE") || qEnvironmentVariableIsSet("MESA_GLSL_VERSION_OVERRIDE")) return 2;
    qputenv("QSG_RENDER_LOOP", "basic");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(2, 0);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    if (QGuiApplication::platformName() != QStringLiteral("wayland")) return 2;
    const auto expectedRenderer = QString::fromLocal8Bit(argv[3]);
    if (expectedRenderer.isEmpty()) return 2;
    std::atomic<bool> inspected{false}, hardware{false};
    QQuickView view;
    QObject::connect(&view, &QQuickWindow::beforeRendering, &view, [&] {
        if (inspected.exchange(true)) return;
        const auto *context = QOpenGLContext::currentContext();
        if (!context || !context->isOpenGLES()) return;
        auto *functions = QOpenGLContext::currentContext()->functions();
        const auto *raw = functions->glGetString(GL_RENDERER);
        if (!raw) return;
        const auto renderer = QString::fromLatin1(reinterpret_cast<const char *>(raw));
        const auto lower = renderer.toLower();
        qInfo("Client GL_RENDERER=%s", qPrintable(renderer));
        if (lower.contains(QStringLiteral("llvmpipe")) || lower.contains(QStringLiteral("softpipe")) ||
            lower.contains(QStringLiteral("lavapipe")) || lower.contains(QStringLiteral("swiftshader")) ||
            lower.contains(QStringLiteral("software"))) return;
        hardware.store(renderer.contains(expectedRenderer, Qt::CaseInsensitive));
    }, Qt::DirectConnection);
    QObject::connect(&view, &QQuickWindow::sceneGraphError, &app, [&](QQuickWindow::SceneGraphError, const QString &error) {
        qCritical("Scene graph: %s", qPrintable(error)); app.exit(1);
    });
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setInitialProperties({{QStringLiteral("solidIndex"), solidIndex}});
    view.setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (view.status() != QQuickView::Ready) return 1;
    view.setFlags(Qt::FramelessWindowHint);
    view.resize(width, height);
    view.showFullScreen();
    QTimer::singleShot(3000, &app, [&] {
        if (!hardware.load()) { qCritical("Hardware GLES client was not established"); app.exit(1); return; }
        const bool passed = capture(QString::fromLocal8Bit(argv[2]), solidIndex);
        if (!passed) qCritical("Composed frame capture failed");
        app.exit(passed ? 0 : 1);
    });
    QTimer::singleShot(24000, &app, [&] { qCritical("Frame check timed out"); app.exit(1); });
    alarm(30);
    return app.exec();
}
