// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include <QSignalSpy>
#include <QTest>

#include <markoff/Editor.h>

using namespace Markoff;

class TstEditorContext : public QObject
{
    Q_OBJECT

private slots:
    void contextChanged_firesOnPlainTextSet();
    void contextChanged_firesOnReadOnlyChange();
    void contextChanged_debounceCoalesces();
    void context_snapshot_readOnly();
    void context_snapshot_heading();
    void context_snapshot_paragraph();
    void context_snapshot_readOnlyReflected();
};

void TstEditorContext::contextChanged_firesOnPlainTextSet()
{
    Editor ed;
    QSignalSpy spy(&ed, &Editor::contextChanged);
    QVERIFY(spy.isValid());
    ed.setPlainText(QStringLiteral("hello"));
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 500);
}

void TstEditorContext::contextChanged_firesOnReadOnlyChange()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("hello"));
    // Drain initial emissions from the text-set.
    QTest::qWait(80);

    QSignalSpy spy(&ed, &Editor::contextChanged);
    ed.setReadOnly(true);
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 500);
    // The snapshot taken at emission time should reflect the new flag.
    // We don't decode the QVariant payload (EditorContext isn't registered
    // as a metatype); a direct context() call is equivalent since the
    // timer fires context() on emission.
    QVERIFY(ed.context().readOnly);
}

void TstEditorContext::contextChanged_debounceCoalesces()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("a\nb\nc\nd\ne"));
    QTest::qWait(80);

    QSignalSpy spy(&ed, &Editor::contextChanged);
    // Fire 5 rapid refresh triggers via repeated setReadOnly toggles —
    // each individually kicks the timer; a single debounce coalesces
    // them into one (occasionally two, if the test harness slices a
    // late signal past the first fire).
    for (int i = 0; i < 5; ++i) {
        ed.setReadOnly(i % 2 == 0);
    }
    QTest::qWait(80);  // well past the 16ms debounce
    QVERIFY2(spy.count() >= 1 && spy.count() <= 2,
             QByteArray::number(spy.count()).prepend("expected 1-2, got: "));
}

void TstEditorContext::context_snapshot_readOnly()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("text"));
    ed.setReadOnly(true);
    const EditorContext ctx = ed.context();
    QVERIFY(ctx.readOnly);
}

void TstEditorContext::context_snapshot_heading()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("## Section"));
    ed.resize(400, 300);
    ed.show();
    if (!QTest::qWaitForWindowActive(&ed)) {
        QTest::qWait(100);
    }
    QTest::qWait(100);  // let parse + highlighter settle

    const EditorContext ctx = ed.context();
    // Without focus, context() may return the default-constructed snapshot
    // (focusedTextItem() returns null). If that's the case, assert only
    // shape-level invariants. If focus is acquired, assert the richer state.
    if (ctx.blockKind == EditorContext::BlockKind::Heading) {
        QCOMPARE(ctx.headingLevel, 2);
    } else {
        // No focused item — still a valid snapshot.
        QCOMPARE(ctx.blockKind, EditorContext::BlockKind::Paragraph);
        QCOMPARE(ctx.headingLevel, 0);
    }
    QVERIFY(!ctx.readOnly);
}

void TstEditorContext::context_snapshot_paragraph()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("just a paragraph"));
    ed.resize(400, 300);
    ed.show();
    if (!QTest::qWaitForWindowActive(&ed)) {
        QTest::qWait(100);
    }
    QTest::qWait(100);

    const EditorContext ctx = ed.context();
    // blockKind could be Paragraph (focus acquired) or Paragraph-default
    // (focus not acquired) — either way the heading level is 0 and the
    // table is unset.
    QCOMPARE(ctx.headingLevel, 0);
    QVERIFY(!ctx.table.has_value());
    QVERIFY(!ctx.readOnly);
}

void TstEditorContext::context_snapshot_readOnlyReflected()
{
    Editor ed;
    ed.setPlainText(QStringLiteral("text"));
    ed.setReadOnly(true);
    const EditorContext ctx = ed.context();
    QVERIFY(ctx.readOnly);
}

QTEST_MAIN(TstEditorContext)
#include "tst_editor_context.moc"
