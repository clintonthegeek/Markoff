// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QFile>
#include <QDir>
#include <QFileInfo>

#include <markoff-foundation/MarkoffDocument.h>

using namespace Markoff;

class TstD2Roundtrip : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void corpus_data();
    void corpus();
};

void TstD2Roundtrip::corpus_data()
{
    QTest::addColumn<QString>("path");
    QDir dir(CORPUS_PATH);
    const auto entries = dir.entryInfoList({"*.md"}, QDir::Files, QDir::Name);
    if (entries.isEmpty())
        QSKIP("No corpus files found in " CORPUS_PATH);
    for (const auto &fi : entries)
        QTest::newRow(qPrintable(fi.fileName())) << fi.absoluteFilePath();
}

void TstD2Roundtrip::corpus()
{
    QFETCH(QString, path);

    QFile in(path);
    QVERIFY2(in.open(QIODevice::ReadOnly), qPrintable("Cannot open: " + path));
    QByteArray original = in.readAll();
    in.close();

    MarkoffDocument doc(1);
    doc.loadFromMarkdown(original);

    // For untouched docs, load-time bytes produce identical output
    QByteArray serialized = doc.serializeForSave();

    if (serialized != original) {
        qDebug() << "Original  :" << original;
        qDebug() << "Serialized:" << serialized;
    }

    QCOMPARE(serialized, original);
}

QTEST_GUILESS_MAIN(TstD2Roundtrip)
#include "tst_d2_roundtrip.moc"
