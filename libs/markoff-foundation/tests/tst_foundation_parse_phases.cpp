// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "../src/IncrementalParseSession.h"
#include "../src/ParsePhases.h"

using namespace Markoff::Parse::Detail;

class TstFoundationParsePhases : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void applyEdit_populatesAllPhases_whenTableIsWired();
    void applyEdit_doesNothingExtra_whenTableIsNull();
    void snapshot_recordsSnapshotPhase();
    void resetClearsTableForReuse();
};

namespace {

QString seedDoc()
{
    return QStringLiteral(
        "---\n"
        "title: Phase test\n"
        "---\n\n"
        "# Heading\n\n"
        "A paragraph with **bold**, *italic*, and a [link](https://example.com).\n\n"
        "Another paragraph[^1] with a footnote ref.\n\n"
        "[^1]: A footnote definition.\n");
}

}  // namespace

void TstFoundationParsePhases::applyEdit_populatesAllPhases_whenTableIsWired()
{
    IncrementalParseSession session;
    session.reset(seedDoc());

    ParsePhaseTable table{};
    session.setPhaseTable(&table);

    // A typical typing edit: append " more." to the body.
    QString edited = seedDoc();
    edited.insert(edited.size() - 1, QStringLiteral(" more."));
    session.applyEdit(edited);

    auto v = [&](ParsePhase p) { return table[static_cast<int>(p)]; };

    // Each phase that is exercised by applyEdit must report > 0 ns. Snapshot
    // is exercised by snapshot() (separate test). The remaining five must
    // populate.
    QVERIFY2(v(ParsePhase::Extract) > 0,
             qPrintable(QString("Extract=%1").arg(v(ParsePhase::Extract))));
    QVERIFY2(v(ParsePhase::Diff) > 0,
             qPrintable(QString("Diff=%1").arg(v(ParsePhase::Diff))));
    QVERIFY2(v(ParsePhase::ParseBlock) > 0,
             qPrintable(QString("ParseBlock=%1").arg(v(ParsePhase::ParseBlock))));
    QVERIFY2(v(ParsePhase::ParseInline) > 0,
             qPrintable(QString("ParseInline=%1").arg(v(ParsePhase::ParseInline))));
    QVERIFY2(v(ParsePhase::Queries) > 0,
             qPrintable(QString("Queries=%1").arg(v(ParsePhase::Queries))));
}

void TstFoundationParsePhases::applyEdit_doesNothingExtra_whenTableIsNull()
{
    IncrementalParseSession session;
    session.reset(seedDoc());

    // Default state: no phase table wired. applyEdit must complete normally
    // without touching any external accumulator.
    QString edited = seedDoc();
    edited.insert(edited.size() - 1, QStringLiteral(" more."));
    session.applyEdit(edited);  // must not crash, must not touch any state

    QVERIFY(session.snapshot() != nullptr);
}

void TstFoundationParsePhases::snapshot_recordsSnapshotPhase()
{
    IncrementalParseSession session;
    session.reset(seedDoc());

    ParsePhaseTable table{};
    session.setPhaseTable(&table);

    auto doc = session.snapshot();
    QVERIFY(doc != nullptr);

    QVERIFY2(table[static_cast<int>(ParsePhase::Snapshot)] > 0,
             qPrintable(QString("Snapshot=%1")
                            .arg(table[static_cast<int>(ParsePhase::Snapshot)])));
}

void TstFoundationParsePhases::resetClearsTableForReuse()
{
    // The table is owned by the caller; the session writes into it but does
    // not clear it. The caller is responsible for resetting the table between
    // iterations. Verify that the session writes monotonically (each
    // applyEdit accumulates onto whatever was there before).
    IncrementalParseSession session;
    session.reset(seedDoc());

    ParsePhaseTable table{};
    session.setPhaseTable(&table);

    QString edited1 = seedDoc();
    edited1.insert(edited1.size() - 1, QStringLiteral(" more."));
    session.applyEdit(edited1);
    const quint64 firstParseBlock = table[static_cast<int>(ParsePhase::ParseBlock)];
    QVERIFY(firstParseBlock > 0);

    QString edited2 = edited1;
    edited2.insert(edited2.size() - 1, QStringLiteral(" still more."));
    session.applyEdit(edited2);
    const quint64 secondParseBlock = table[static_cast<int>(ParsePhase::ParseBlock)];
    // After the second applyEdit, the accumulated total should have grown.
    QVERIFY2(secondParseBlock > firstParseBlock,
             qPrintable(QString("first=%1 second=%2")
                            .arg(firstParseBlock).arg(secondParseBlock)));
}

QTEST_APPLESS_MAIN(TstFoundationParsePhases)
#include "tst_foundation_parse_phases.moc"
