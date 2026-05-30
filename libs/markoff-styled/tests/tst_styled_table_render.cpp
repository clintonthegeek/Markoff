// SPDX-License-Identifier: GPL-3.0-or-later
//
// Integration: a BlockKind::Table renders as a native QTextTable frame inside
// the styled Editor, surrounding text stays editable, and the model buffer is
// untouched by materialization. Drives the real Editor wiring (binding opaque
// renderer + FormatPass Table skip).
#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

using namespace Markoff;

static void pumpEvents() { QCoreApplication::processEvents(); }

namespace {
int frameCount(QTextDocument *doc) {
    int n = 0;
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (qobject_cast<QTextTable *>(f)) ++n;
    return n;
}
QTextTable *firstTable(QTextDocument *doc) {
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f)) return t;
    return nullptr;
}
bool hasBlockText(QTextDocument *doc, const QString &text) {
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (b.text() == text) return true;
    return false;
}
}  // namespace

class TstStyledTableRender : public QObject {
    Q_OBJECT
private slots:
    void table_renders_as_frame_in_editor() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro"));
        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        QTextDocument *qdoc = editor.textEdit()->document();
        QCOMPARE(frameCount(qdoc), 1);

        QTextTable *t = firstTable(qdoc);
        QVERIFY(t != nullptr);
        QCOMPARE(t->rows(), 2);     // header + 1 body
        QCOMPARE(t->columns(), 2);
        QCOMPARE(t->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("A"));
        QCOMPARE(t->cellAt(1, 1).firstCursorPosition().block().text(),
                 QStringLiteral("2"));
        // No raw pipe text leaked into the rendered document.
        QVERIFY(!qdoc->toPlainText().contains(QLatin1Char('|')));
        // Surrounding paragraphs intact.
        QVERIFY(hasBlockText(qdoc, QStringLiteral("intro")));
        QVERIFY(hasBlockText(qdoc, QStringLiteral("outro")));
    }

    void materialization_does_not_mutate_model() {
        const QByteArray src = QByteArrayLiteral(
            "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro");
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(src);
        const quint64 seqBefore = doc.d2EditSequence();

        Styled::Editor editor;
        auto *s = doc.createSession();
        editor.setSession(s);
        editor.setDocument(&doc);
        pumpEvents();

        // Rendering the frame must not have issued any model edit.
        QCOMPARE(doc.d2EditSequence(), seqBefore);
        // The table block buffer is still the exact pipe source; save
        // round-trips it.
        const QByteArray saved = doc.serializeForSave();
        QVERIFY(saved.contains("| A | B |"));
        QVERIFY(saved.contains("|---|---|"));
    }
};

QTEST_MAIN(TstStyledTableRender)
#include "tst_styled_table_render.moc"
