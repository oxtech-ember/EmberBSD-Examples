// SPDX-License-Identifier: BSD-2-Clause
#include <QGuiApplication>
#include <QImage>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>
#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 2;
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
    if (view.status() != QQuickView::Ready)
        return 1;
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return 1;
    auto *top = qobject_cast<QQuickItem *>(qmlContext(view.rootObject())->objectForName("topRect"));
    if (!top)
        return 1;
    const QPoint taps[] = {{10, 10}, {125, 375}, {70, 320}};
    for (int state = 0; state < 4; ++state) {
        if (state)
            QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, taps[state - 1]);
        if (top->isVisible() != (state != 2))
            return 1;
        const QImage image = view.grabWindow().convertToFormat(QImage::Format_RGB16);
        if (image.size() != QSize(320, 480))
            return 1;
        for (int y = 0; y < image.height(); ++y) {
            const auto *row = reinterpret_cast<const quint16 *>(image.constScanLine(y));
            for (int x = 0, end; x < image.width(); x = end) {
                for (end = x + 1; end < image.width() && row[end] == row[x]; ++end) {}
                std::printf("%d %d %d %d %04x\n", state, y, x, end, unsigned(row[x]));
            }
        }
    }
    return 0;
}
