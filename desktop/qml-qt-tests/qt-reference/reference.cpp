// SPDX-License-Identifier: BSD-2-Clause
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTimer>
#include <QUrl>
#include <cstdlib>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 5) {
        std::fprintf(stderr, "usage: qt_reference input.qml output.rgb565 width height\n");
        return 2;
    }
    const int width = std::atoi(argv[3]);
    const int height = std::atoi(argv[4]);
    if (width <= 0 || width > 320 || height <= 0 || height > 480) {
        std::fprintf(stderr, "unsupported frame dimensions\n");
        return 2;
    }

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (view.status() != QQuickView::Ready || view.rootObject() == nullptr) {
        std::fprintf(stderr, "Qt Quick failed to load %s\n", argv[1]);
        return 1;
    }
    view.resize(width, height);
    view.show();

    int result = 1;
    bool captured = false;
    QObject::connect(&view, &QQuickWindow::frameSwapped, &app, [&]() {
        if (captured)
            return;
        captured = true;
        const QImage image = view.grabWindow().convertToFormat(QImage::Format_RGB16);
        if (image.size() != QSize(width, height)) {
            std::fprintf(stderr, "unexpected Qt frame dimensions: %dx%d\n",
                image.width(), image.height());
            app.exit(1);
            return;
        }
        QFile output(QString::fromLocal8Bit(argv[2]));
        if (!output.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "cannot open reference output\n");
            app.exit(1);
            return;
        }
        for (int y = 0; y < image.height(); ++y) {
            const auto *row = reinterpret_cast<const quint16 *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const char pixel[] = {char(row[x] & 0xff), char(row[x] >> 8)};
                if (output.write(pixel, sizeof(pixel)) != sizeof(pixel)) {
                    std::fprintf(stderr, "short write for Qt reference\n");
                    app.exit(1);
                    return;
                }
            }
        }
        result = 0;
        app.quit();
    });
    QTimer::singleShot(5000, &app, [&]() {
        if (!captured) {
            std::fprintf(stderr, "Qt offscreen frame was not rendered\n");
            app.exit(1);
        }
    });
    app.exec();
    return result;
}
