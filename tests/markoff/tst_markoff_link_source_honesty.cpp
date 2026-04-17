// SPDX-License-Identifier: GPL-3.0-or-later
//
// Regression gate for Cluster J phase 3 — enforce that no source file
// in libs/markoff/ ever fabricates `"bases"` as a hover-link source
// string. That value is reserved for the Bases plugin surface
// (Cluster N); Markoff emissions must identify themselves honestly as
// "markoff:editor" / "markoff:livepreview" / "markoff:reading".
//
// Trivially passes as of 2026-04-15 (no "bases" hardcode exists); the
// test fails loudly if a future change introduces one.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QtTest>

class TstMarkoffLinkSourceHonesty : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testNoBasesHardcodeInMarkoffSources()
    {
        // Resolve libs/markoff/src relative to this source file so the
        // test is location-independent (works from build/ and ctest).
        const QString thisFile = QString::fromUtf8(__FILE__);
        QDir dir(QFileInfo(thisFile).absolutePath());
        // tests/markoff/tst_*.cpp -> ../../libs/markoff/src
        dir.cdUp(); // tests/
        dir.cdUp(); // repo root
        dir.cd(QStringLiteral("libs/markoff/src"));

        const QString srcRoot = dir.absolutePath();
        QVERIFY2(QFileInfo::exists(srcRoot),
                 qPrintable(QStringLiteral("libs/markoff/src not found at %1")
                                .arg(srcRoot)));

        QDirIterator it(srcRoot, {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                         QDir::Files, QDirIterator::Subdirectories);
        int scanned = 0;
        while (it.hasNext()) {
            const QString path = it.next();
            QFile f(path);
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(path));
            const QByteArray body = f.readAll();
            QVERIFY2(!body.contains("\"bases\""),
                     qPrintable(QStringLiteral("Forbidden 'bases' hardcode in %1")
                                    .arg(path)));
            ++scanned;
        }
        // Sanity: ensure the walk actually scanned something.
        QVERIFY(scanned > 0);
    }
};

QTEST_APPLESS_MAIN(TstMarkoffLinkSourceHonesty)
#include "tst_markoff_link_source_honesty.moc"
