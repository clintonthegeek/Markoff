// SPDX-License-Identifier: GPL-3.0-or-later
//
// R5.5 Task 16: end-to-end marker-paragraph flow tests.
//
// These are the load-bearing race-verification tests for the marker
// design (architectural review §3.5). The previous v2 hole-stress test
// motivated the marker design choice over v2 holes; this file is its
// successor under the marker scheme.
//
// PATH B (direct API). The tests drive a `QTextEdit`-backed
// `LiveEditBinding` rather than a full `QQuickView`/`LiveView.qml`
// scene. Reasons:
//   - The existing race-bearing tests in `tst_live_render_paragraph_edit.cpp`
//     already drive `contentsChange` through a `QTextEdit`'s real
//     `QTextDocument`, which is what `LiveEditBinding` listens to in
//     production. The same code path exercises the `applyLocalEdit ->
//     ParsePool::schedule -> worker thread -> parseUpdated` async
//     pipeline that the marker design must survive.
//   - Path A (loading `LiveView.qml` into a `QQuickView`) brings in
//     the full QML delegate stack and needs the QML module
//     discoverable to the test binary; significant infrastructure, no
//     incremental race coverage over Path B for the assertions below.
//     (Path A would additionally cover focus routing and delegate
//     recycling — out of scope for the marker contract.)
//
// `QTest::qWait` between simulated keystrokes lets the parse worker
// thread interleave, exposing the same async race the real keyboard
// would (the v0 holes' character-scramble bug surfaced via this exact
// pattern in `typing_two_chars_before_parse_arrives_does_not_scramble`).

#include <QTest>
#include <QSignalSpy>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QElapsedTimer>

#include <markoff/live-render/LiveEditBinding.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/MarkerScrubber.h>
#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/Marker.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff::LiveRender;

namespace {

// Reset content + wait for the parse to land in the model.
bool waitForRows(LiveListModelBinding &binding,
                 Markoff::MarkoffDocument &doc,
                 const QByteArray &content,
                 int expectedRows,
                 int timeoutMs = 2000)
{
    QSignalSpy spy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    doc.resetContent(content, Markoff::Origin::FirstOpen);
    QElapsedTimer t; t.start();
    while (binding.model()->rowCount() != expectedRows) {
        const int remaining = timeoutMs - int(t.elapsed());
        if (remaining <= 0) return false;
        if (!spy.wait(remaining)) return false;
    }
    return true;
}

// Wait until the model rowCount equals `expected`, polling the async
// parse pipeline. Returns false on timeout.
bool waitForRowCount(LiveListModelBinding &binding, int expected, int timeoutMs = 2000)
{
    QElapsedTimer t; t.start();
    while (binding.model()->rowCount() != expected) {
        if (t.elapsed() > timeoutMs) return false;
        QTest::qWait(20);
    }
    return true;
}

}  // namespace

class TstLiveRenderMarkerFlow : public QObject {
    Q_OBJECT
private Q_SLOTS:
    /// Step 1: EOB-Enter then a single typed char produces clean source
    /// in exactly two `editSequence` bumps (one for the marker insert,
    /// one for the bundled scrub+insert on first keystroke).
    void step1_eobEnter_thenType_atomicScrub();

    /// Step 2: stress-typing race verification. After EOB-Enter, type
    /// 200 characters with 30 ms inter-keystroke gap (matching the
    /// LiveRealisticInputHarness default) — interleaving with the
    /// async parse worker that may deliver `parseUpdated` mid-typing.
    /// The final source must end with the typed string in order, with
    /// no character scrambling and no marker bytes.
    void step2_eobEnter_stressType_noScramble();

    /// Step 3: focus-out without typing scrubs the marker. EOB-Enter
    /// inserts a marker; LiveEditBinding::onFocusLost() (called when
    /// the delegate loses focus) routes through MarkerScrubber. Source
    /// must return to "alpha\n".
    void step3_focusOutWithoutTyping_scrubsMarker();
};

// ---------- Step 1 ----------

void TstLiveRenderMarkerFlow::step1_eobEnter_thenType_atomicScrub()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    QVERIFY(waitForRows(binding, doc, QByteArrayLiteral("alpha"), 1));

    const quint64 seqAfterLoad = doc.editSequence();

    // EOB-Enter via the structural-key handler — inserts a marker paragraph.
    const bool consumed = binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier,
        /*blockIndex=*/0, /*qtPos=*/5,
        /*selectionEmpty=*/true,
        QStringLiteral("alpha"));
    QVERIFY(consumed);

    // Wait for parse: model now has 2 rows (alpha + marker).
    QVERIFY(waitForRowCount(binding, 2));
    const quint64 seqAfterMarkerInsert = doc.editSequence();
    QCOMPARE(seqAfterMarkerInsert, seqAfterLoad + 1);
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
             QStringLiteral("alpha\n\n%1").arg(kMarkerChar));

    // Wire LiveEditBinding to a QTextEdit-backed document mirroring the
    // marker block (model index 1). See LiveEditBinding.h friend grant.
    QTextEdit editor;
    editor.setPlainText(QString(kMarkerChar));
    LiveEditBinding eb;
    eb.setBinding(&binding);
    eb.setModelIndex(1);
    // Mirror QML ordering: text Q_PROPERTY bound to model.text BEFORE
    // the document is wired (see paragraph_edit test rationale).
    eb.setText(QString(kMarkerChar));
    eb.setRawTextDocument(editor.document());

    // Simulate the user typing 'x' at qtPos 0 (before the marker).
    QTextCursor cur(editor.document());
    cur.setPosition(0);
    cur.insertText(QStringLiteral("x"));

    // The bundled scrub+insert primitive (Task 5) must produce exactly
    // ONE additional editSequence bump and the source must end clean.
    QCOMPARE(doc.editSequence(), seqAfterMarkerInsert + 1);
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
             QStringLiteral("alpha\n\nx\n"));
    QVERIFY(!QString::fromUtf8(doc.toMarkdownUtf8()).contains(kMarkerChar));
}

// ---------- Step 2 ----------

void TstLiveRenderMarkerFlow::step2_eobEnter_stressType_noScramble()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    QVERIFY(waitForRows(binding, doc, QByteArrayLiteral("alpha"), 1));

    // EOB-Enter inserts a marker paragraph.
    QVERIFY(binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, /*blockIndex=*/0, /*qtPos=*/5,
        /*selectionEmpty=*/true, QStringLiteral("alpha")));
    QVERIFY(waitForRowCount(binding, 2));

    // Wire LiveEditBinding to a QTextEdit-backed document mirroring the
    // marker block (model index 1). The marker text is the model's
    // payload; user typing into a marker-only block triggers the
    // bundled scrub+insert primitive, which on the FIRST keystroke
    // also wipes the marker. Subsequent keystrokes are plain inserts.
    QTextEdit editor;
    editor.setPlainText(QString(kMarkerChar));
    LiveEditBinding eb;
    eb.setBinding(&binding);
    eb.setModelIndex(1);
    eb.setText(QString(kMarkerChar));
    eb.setRawTextDocument(editor.document());

    // Replicate the QML `text: model.text` binding the production
    // delegate uses: when the row's text changes (e.g. after a parse
    // arrival), push the new text into `eb.setText(...)`. Without this
    // re-sync, `m_previousText` lags and qtPos→byte translation
    // computes against stale pre-edit text.
    QObject::connect(binding.model(), &QAbstractItemModel::dataChanged,
        [&binding, &eb](const QModelIndex &tl, const QModelIndex &br,
                        const QList<int> &roles) {
            const int row = eb.modelIndex();
            if (row < tl.row() || row > br.row()) return;
            // TextRole == Qt::UserRole + 1 typically; LiveBlockModel
            // exposes role names via model.text. We sniff via record.
            if (row >= binding.model()->rowCount()) return;
            const QString newText = binding.model()->recordAt(row).text;
            eb.setText(newText);
        });

    // Deterministic 200-character payload with no spaces (mirrors the
    // pattern called out in the task description: spaces have a
    // separate code path in QTest::keyClick — and the production
    // edit-binding path doesn't care, but we keep the test honest).
    // The string is a 200-char alphanumeric stream chosen to exhibit
    // any ordering bug (mixed letters + digits, no repeating runs).
    QString payload;
    payload.reserve(200);
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < 200; ++i) {
        payload.append(QLatin1Char(kAlphabet[i % (sizeof(kAlphabet) - 1)]));
    }
    QCOMPARE(payload.size(), 200);

    // Type each character at the current end of the document.
    // Between keystrokes, qWait + processEvents lets the parse worker
    // interleave (matching LiveRealisticInputHarness::typeChar timing).
    for (int i = 0; i < payload.size(); ++i) {
        QTextCursor cur(editor.document());
        cur.movePosition(QTextCursor::End);
        cur.insertText(QString(payload.at(i)));
        QTest::qWait(30);
        QCoreApplication::processEvents();
    }

    // Drain any in-flight parse so the doc reflects the final state.
    QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
    parseSpy.wait(500);

    const QString src = QString::fromUtf8(doc.toMarkdownUtf8());
    QVERIFY2(!src.contains(kMarkerChar),
             "marker byte must not survive the bundled-edit primitive");
    // Source must end with the payload — preceded by the alpha block
    // and its inter-block separator.
    const QString expectedTail = payload + QStringLiteral("\n");
    QVERIFY2(src.endsWith(expectedTail),
             qPrintable(QStringLiteral("source did not end with payload; tail=%1")
                            .arg(src.right(40))));
    // And starts with "alpha\n\n".
    QVERIFY(src.startsWith(QStringLiteral("alpha\n\n")));
    // Stronger: the full source equals exactly "alpha\n\n<payload>\n".
    QCOMPARE(src, QStringLiteral("alpha\n\n%1\n").arg(payload));
}

// ---------- Step 3 ----------

void TstLiveRenderMarkerFlow::step3_focusOutWithoutTyping_scrubsMarker()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);

    QVERIFY(waitForRows(binding, doc, QByteArrayLiteral("alpha"), 1));

    // EOB-Enter inserts a marker paragraph.
    QVERIFY(binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, /*blockIndex=*/0, /*qtPos=*/5,
        /*selectionEmpty=*/true, QStringLiteral("alpha")));
    QVERIFY(waitForRowCount(binding, 2));

    // Wire LiveEditBinding to the marker block's text. setText with
    // marker-only content arms `m_pendingMarkerScrub` (Task 5+8).
    QTextEdit editor;
    editor.setPlainText(QString(kMarkerChar));
    LiveEditBinding eb;
    eb.setBinding(&binding);
    eb.setModelIndex(1);
    eb.setText(QString(kMarkerChar));
    eb.setRawTextDocument(editor.document());

    // Sanity: source contains the marker pre-focus-out.
    QVERIFY(QString::fromUtf8(doc.toMarkdownUtf8()).contains(kMarkerChar));

    // Simulate focus moving away. In QML, the delegate's
    // `onActiveFocusChanged: if (!activeFocus) onFocusLost()` calls
    // this. Direct invocation here exercises the same code path.
    eb.onFocusLost();

    // Wait for the scrubber's applyLocalEdit to land in the model.
    QVERIFY(waitForRowCount(binding, 1));
    QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()), QStringLiteral("alpha\n"));
}

QTEST_MAIN(TstLiveRenderMarkerFlow)
#include "tst_live_render_marker_flow.moc"
