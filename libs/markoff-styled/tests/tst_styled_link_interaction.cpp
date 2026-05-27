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
};

QTEST_MAIN(TstStyledLinkInteraction)
#include "tst_styled_link_interaction.moc"
