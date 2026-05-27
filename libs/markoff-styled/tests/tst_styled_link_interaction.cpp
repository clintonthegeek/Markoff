// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextCursor>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

#include "support/RecordingLinkService.h"

namespace {
QPoint pointForChar(QTextEdit *edit, int charPos) {
    QTextCursor c = edit->textCursor();
    c.setPosition(charPos);
    edit->setTextCursor(c);
    const QRect r = edit->cursorRect();
    return r.center() + QPoint(3, 0);
}
}  // namespace

class TstStyledLinkInteraction : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void click_inside_link_calls_activate() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // 'e' of 'text' is at char 4: "a [t[e]xt](...)" — index 4.
        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);

        QTRY_COMPARE(svc.activates.size(), 1);
    }

    void click_outside_link_does_not_activate() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("just plain text"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        const QPoint p = pointForChar(e.textEdit(), 3);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);

        QTest::qWait(50);
        QCOMPARE(svc.activates.size(), 0);
    }

    void editor_provides_default_link_service_when_none_set() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        // Do NOT call setLinkService — exercise the lazy default path.
        QVERIFY(e.linkService() != nullptr);

        // A click on a link should not crash (the default service is a no-op
        // for activate, just emits the signal).
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());
        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseClick(e.textEdit()->viewport(), Qt::LeftButton,
                          Qt::NoModifier, p);
        QTest::qWait(50);
        // No assertion on the service's state — DefaultLinkService doesn't
        // expose recording surface. The test verifies no crash + non-null.
    }

    void hover_inside_link_calls_notify_hover() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        const QPoint p = pointForChar(e.textEdit(), 4);
        QTest::mouseMove(e.textEdit()->viewport(), p);

        QTRY_COMPARE(svc.hovers.size(), 1);
    }

    void hover_idempotent_within_same_link() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("a [text](http://x.test) b"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Ensure the mouse starts from a neutral position so the first move
        // is a real position change regardless of prior-test mouse state.
        QTest::mouseMove(e.textEdit()->viewport(), QPoint(0, 0));
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);
        // Move 1 char within the same link.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 5));
        QTest::qWait(50);
        QCOMPARE(svc.hovers.size(), 1);
    }

    void hover_off_link_then_on_emits_left_and_new_hover() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral(
            "a [first](http://1.test) b [second](http://2.test) c"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        RecordingLinkService svc;
        e.setLinkService(&svc);
        e.resize(800, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 4));
        QTRY_COMPARE(svc.hovers.size(), 1);

        // Move into plain text between links.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 26));
        QTRY_COMPARE(svc.hoverLefts.size(), 1);

        // Move into the second link.
        QTest::mouseMove(e.textEdit()->viewport(), pointForChar(e.textEdit(), 31));
        QTRY_COMPARE(svc.hovers.size(), 2);
    }
};

QTEST_MAIN(TstStyledLinkInteraction)
#include "tst_styled_link_interaction.moc"
