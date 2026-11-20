// SPDX-License-Identifier: BSD-2-Clause
#include <QGuiApplication>
#include <QQuickView>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickView view;
    view.loadFromModule("Ember.Weather", "Weather");
    if (view.status() != QQuickView::Ready)
        return 1;
    view.show();
    return app.exec();
}
