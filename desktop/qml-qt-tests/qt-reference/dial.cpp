// SPDX-License-Identifier: BSD-2-Clause
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>
#include <cstdio>

// Render only the moving layers from Dial.qml. Keep each nontransparent crop.
static int hands(const QDir &source, const QDir &out, bool verify)
{
    QImage needle(source.filePath("needle.png")), shadow(source.filePath("needle_shadow.png"));
    if (needle.isNull() || shadow.isNull())
        return 1;
    for (int level : {0, 25, 50, 75, 100}) {
        QImage canvas(210, 210, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        auto layer = [&](QPoint position, QPoint pivot, const QImage &image) {
            painter.save();
            painter.translate(position + pivot);
            painter.rotate(level * 2.6 - 130);
            painter.drawImage(-pivot, image);
            painter.restore();
        };
        layer({96, 35}, {9, 67}, shadow);
        layer({98, 33}, {5, 65}, needle);
        painter.end();
        QRect ink;
        for (int y = 0; y < canvas.height(); ++y)
            for (int x = 0; x < canvas.width(); ++x)
                if (qAlpha(canvas.pixel(x, y)))
                    ink |= QRect(x, y, 1, 1);
        const auto name = out.filePath(QString("hand%1.png").arg(level));
        const auto cropped = canvas.copy(ink).convertToFormat(QImage::Format_RGBA8888);
        if (ink.isEmpty() || (verify ? QImage(name).convertToFormat(cropped.format()) != cropped
                                     : !cropped.save(name)))
            return 1;
        std::printf("%d %d %d %d %d\n", level, ink.x(), ink.y(), ink.width(), ink.height());
    }
    return 0;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 4)
        return 2;
    const QDir source(QString::fromLocal8Bit(argv[2])), out(QString::fromLocal8Bit(argv[3]));
    const auto mode = QString::fromLocal8Bit(argv[1]);
    if (mode == "hands" || mode == "verify-hands")
        return hands(source, out, mode == "verify-hands");
    if (mode != "reference")
        return 2;
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(source.absoluteFilePath("dialcontrol.qml")));
    if (view.status() != QQuickView::Ready)
        return 1;
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return 1;
    auto *ctx = qmlContext(view.rootObject());
    auto *dial = qobject_cast<QQuickItem *>(ctx->objectForName("dial"));
    auto *slider = qobject_cast<QQuickItem *>(ctx->objectForName("slider"));
    if (!dial || !slider)
        return 1;
    // Exercise the unchanged original slider with a real Qt pointer sequence.
    const QPoint press = slider->mapToScene({15, 7}).toPoint();
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, press);
    for (int dx : {20, 60, 120})
        QTest::mouseMove(&view, press + QPoint(dx, 0), 30);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, press + QPoint(120, 0));
    if (dial->property("value").toDouble() < 20 || dial->childItems().size() < 3)
        return 1;
    auto *angle = qmlContext(dial->childItems().at(2))->objectForName("needleRotation");
    if (!angle)
        return 1;
    for (int level : {0, 25, 50, 75, 100}) {
        // Select the original slider's exact detent to compare settled instrument faces.
        slider->setX(level * 228.0 / 100.0);
        QElapsedTimer stable;
        if (!QTest::qWaitFor([&] {
                if (qAbs(angle->property("angle").toDouble() - (level * 2.6 - 130)) >= .01)
                    stable.invalidate();
                else if (!stable.isValid())
                    stable.start();
                return stable.isValid() && stable.elapsed() >= 150;
            }, 8000))
            return 1;
        QImage frame = view.grabWindow();
        if (frame.size() != QSize(300, 300) ||
            !frame.copy(45, 45, 210, 210).save(out.filePath(QString("original%1.ppm").arg(level))))
            return 1;
    }
    return 0;
}
