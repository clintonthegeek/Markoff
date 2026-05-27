// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

namespace {
QByteArray fullText(Markoff::MarkoffDocument &doc) {
    QByteArray out;
    bool first = true;
    for (Markoff::BlockId id : doc.iterateBlocks()) {
        if (!first) out += "\n\n";
        out += doc.blockText(id);
        first = false;
    }
    return out;
}
}  // namespace

class TstStyledBindingRoundtrip : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_propagates_to_markoff_document() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray());
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(e.textEdit(), QStringLiteral("abc"));
        QTRY_COMPARE(fullText(doc), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("initial"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("initial"));
        doc.applyFlatEdit(7, 7, QByteArrayLiteral(" tail"),
                          Markoff::Origin::UserEdit);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("initial tail"));
    }

    void document_change_rewires_binding_cleanly() {
        Markoff::Styled::Editor e;

        Markoff::MarkoffDocument docA(1);
        docA.loadFromMarkdown(QByteArrayLiteral("first"));
        auto *s = docA.createSession();
        e.setSession(s);
        e.setDocument(&docA);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("first"));

        Markoff::MarkoffDocument docB(1);
        docB.loadFromMarkdown(QByteArrayLiteral("second"));
        e.setDocument(&docB);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));

        // Edits to docA must NOT propagate to the editor.
        docA.applyFlatEdit(5, 5, QByteArrayLiteral("X"),
                           Markoff::Origin::UserEdit);
        QTest::qWait(50);
        QCOMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));
    }
};

QTEST_MAIN(TstStyledBindingRoundtrip)
#include "tst_styled_binding_roundtrip.moc"
