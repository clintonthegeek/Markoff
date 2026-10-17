// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>
#include "CandidateDocumentFixture.h"

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
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArray());
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(e.textEdit(), QStringLiteral("abc"));
        QTRY_COMPARE(fullText(doc), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Styled::Editor e;
    auto fixture_doc = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("initial"));
    auto& doc = *fixture_doc.document;
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("initial"));
        QCOMPARE(doc.applyBlockEdit({doc.iterateBlocks().front(), 7, 0,
                                     QByteArrayLiteral(" tail")}).state,
                 Markoff::VNext::ActionPublicationState::committed);
        QTRY_COMPARE(e.textEdit()->toPlainText(),
                     QStringLiteral("initial tail"));
    }

    void document_change_rewires_binding_cleanly() {
        Markoff::Styled::Editor e;

    auto fixture_docA = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("first"));
    auto& docA = *fixture_docA.document;
        auto *s = docA.createSession();
        e.setSession(s);
        e.setDocument(&docA);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("first"));

    auto fixture_docB = Markoff::Styled::Tests::openCandidateDocument(QByteArrayLiteral("second"));
    auto& docB = *fixture_docB.document;
        e.setDocument(&docB);
        QTRY_COMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));

        // Edits to docA must NOT propagate to the editor.
        QCOMPARE(docA.applyBlockEdit({docA.iterateBlocks().front(), 5, 0,
                                      QByteArrayLiteral("X")}).state,
                 Markoff::VNext::ActionPublicationState::committed);
        QTest::qWait(50);
        QCOMPARE(e.textEdit()->toPlainText(), QStringLiteral("second"));
    }
};

QTEST_MAIN(TstStyledBindingRoundtrip)
#include "tst_styled_binding_roundtrip.moc"
