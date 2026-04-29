// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QtPlugin>

Q_IMPORT_PLUGIN(org_markoff_view_qmlPlugin)

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Theme.h>

class TstLiveViewQml : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            QSKIP("QGuiApplication required for QQuickView");
        }
    }

    void placeholder() {
        QVERIFY(true);
    }
};

QTEST_MAIN(TstLiveViewQml)
#include "tst_view_qml_live_view_qml.moc"
