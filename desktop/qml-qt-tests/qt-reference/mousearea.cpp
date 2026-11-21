// SPDX-License-Identifier: BSD-2-Clause
#include <QGuiApplication>
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
    auto *context = qmlContext(view.rootObject());
    auto *info = context->objectForName("info");
    auto *btn = context->objectForName("btn");
    auto *pos = context->objectForName("posInfo");
    auto *blue = context->objectForName("blueSquare");
    auto *red = context->objectForName("redSquare");
    if (!info || !btn || !pos || !blue || !red)
        return 1;
    const auto text = [](QObject *o) { return o->property("text").toString().toUtf8(); };
    const QPoint p(30, 40);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, p);
    std::printf("press: %s | %s | opacity %.1f\n", text(info).constData(),
                text(pos).constData(), red->property("opacity").toDouble());
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, p);
    std::printf("tap: %s\n", text(btn).constData());
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, p, 800);
    if (!QTest::qWaitFor([&] { return text(btn) == "Press and hold"; }, 2000))
        return 1;
    std::printf("hold: %s\n", text(btn).constData());
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, p);
    std::printf("lift: %s\n", text(btn).constData());
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, p, 800);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, p, 10);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, p, 100);
    std::printf("double: %s\n", text(btn).constData());
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, p, 10);
    std::printf("double-lift: %s\n", text(btn).constData());
    QTest::mousePress(&view, Qt::RightButton, Qt::ShiftModifier, p, 800);
    std::printf("right: %s\n", text(info).constData());
    QTest::mouseMove(&view, QPoint(140, 150));
    std::printf("outside: %s | opacity %.1f\n", text(info).constData(),
                red->property("opacity").toDouble());
    QTest::mouseMove(&view, p);
    QTest::mouseRelease(&view, Qt::RightButton, Qt::ShiftModifier, p);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(240, 50), 800);
    for (auto point : {QPoint(224, 75), QPoint(160, 150), QPoint(6, 460)})
        QTest::mouseMove(&view, point);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(6, 460));
    std::printf("drag: %d,%d\n", blue->property("x").toInt(), blue->property("y").toInt());
    return 0;
}
