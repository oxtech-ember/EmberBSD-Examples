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
    auto *rect = qobject_cast<QQuickItem *>(qmlContext(view.rootObject())->objectForName("rect"));
    if (!rect || rect->x() != 0)
        return 1;
    for (int start : {0, 50}) {
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(start + 90, 150));
        if (!QTest::qWaitFor([&] { return rect->x() > start && rect->x() < start + 50; }, 2000)
            || !QTest::qWaitFor([&] { return rect->x() == start + 50; }, 2000))
            return 1;
        std::printf("x %d -> intermediate -> %d\n", start, start + 50);
    }
    return 0;
}
