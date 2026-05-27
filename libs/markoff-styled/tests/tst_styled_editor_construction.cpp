// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Theme.h>
#include <markoff/styled/Editor.h>

class TstStyledEditorConstruction : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructs_and_is_a_markdown_view() {
        Markoff::Styled::Editor e;
        QVERIFY(qobject_cast<Markoff::MarkdownView *>(&e) != nullptr);
        QVERIFY(e.hasCursor());
        QVERIFY(e.hasEditing());
        QVERIFY(!e.isReadOnly());
    }

    void document_setter_round_trips_and_signals() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray());
        QSignalSpy spy(&e, &Markoff::MarkdownView::documentChanged);
        e.setDocument(&doc);
        QCOMPARE(e.document(), &doc);
        QCOMPARE(spy.count(), 1);
    }

    void session_setter_round_trips() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArray());
        Markoff::Session *s = doc.createSession();
        e.setSession(s);
        QCOMPARE(e.session(), s);
        doc.destroySession(s);
    }

    void font_scale_default_and_setter() {
        Markoff::Styled::Editor e;
        QCOMPARE(e.fontScale(), 1.0);
        QSignalSpy spy(&e, &Markoff::Styled::Editor::fontScaleChanged);
        e.setFontScale(1.25);
        QCOMPARE(e.fontScale(), 1.25);
        QCOMPARE(spy.count(), 1);
    }

    void read_only_round_trips() {
        Markoff::Styled::Editor e;
        e.setReadOnly(true);
        QVERIFY(e.isReadOnly());
        QVERIFY(!e.hasEditing());
    }
};

QTEST_MAIN(TstStyledEditorConstruction)
#include "tst_styled_editor_construction.moc"
