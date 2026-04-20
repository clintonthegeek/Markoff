// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include "support/textcontrol_testutil.h"

using namespace Markoff;
using namespace Markoff::TestUtil;

class TstTextControlLinks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void anchorAt_onAnchor_returnsHref();
    void anchorAt_offAnchor_returnsEmpty();
    void click_onAnchor_emitsLinkActivated();
    void click_offAnchor_doesNotEmit();
    void click_onEmptyHrefAnchor_doesNotEmit();
    void hover_onAnchor_emitsLinkHovered();

private:
    /// Insert `text` as an anchor with `href` at document end.
    /// Returns the center point of the inserted range.
    static QPointF insertAnchor(TextControlFixture &fx,
                                 const QString &text,
                                 const QString &href);
};

QPointF TstTextControlLinks::insertAnchor(TextControlFixture &fx,
                                          const QString &text,
                                          const QString &href)
{
    QTextCursor c(fx.document.get());
    c.movePosition(QTextCursor::End);
    int startPos = c.position();
    QTextCharFormat fmt;
    fmt.setAnchor(true);
    fmt.setAnchorHref(href);
    c.insertText(text, fmt);
    QTextCursor mid(fx.document.get());
    mid.setPosition(startPos + text.length() / 2);
    return fx.control.cursorRect(mid).center();
}

void TstTextControlLinks::anchorAt_onAnchor_returnsHref()
{
    auto fx = makeFixture();
    QPointF pt = insertAnchor(fx, QStringLiteral("click me"),
                               QStringLiteral("https://example.org"));
    QCOMPARE(fx.control.anchorAt(pt),
             QStringLiteral("https://example.org"));
}

void TstTextControlLinks::anchorAt_offAnchor_returnsEmpty()
{
    auto fx = makeFixture(QStringLiteral("plain text with no anchor"), 0);
    QTextCursor c(fx.document.get());
    c.setPosition(5);
    QPointF pt = fx.control.cursorRect(c).center();
    QCOMPARE(fx.control.anchorAt(pt), QString());
}

void TstTextControlLinks::click_onAnchor_emitsLinkActivated()
{
    auto fx = makeFixture();
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPointF pt = insertAnchor(fx, QStringLiteral("click me"),
                               QStringLiteral("https://example.org"));
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkActivated);
    sendMousePress(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(),
             QStringLiteral("https://example.org"));
}

void TstTextControlLinks::click_offAnchor_doesNotEmit()
{
    auto fx = makeFixture(QStringLiteral("plain text"), 0);
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QTextCursor c(fx.document.get());
    c.setPosition(5);
    QPointF pt = fx.control.cursorRect(c).center();
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkActivated);
    sendMousePress(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(spy.count(), 0);
}

void TstTextControlLinks::click_onEmptyHrefAnchor_doesNotEmit()
{
    auto fx = makeFixture();
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPointF pt = insertAnchor(fx, QStringLiteral("click"), QString());
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkActivated);
    sendMousePress(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                   fx.contextWidget.get());
    sendMouseRelease(fx.control, pt, Qt::LeftButton, Qt::NoModifier,
                     fx.contextWidget.get());
    QCOMPARE(spy.count(), 0);
}

void TstTextControlLinks::hover_onAnchor_emitsLinkHovered()
{
    auto fx = makeFixture();
    fx.control.setTextInteractionFlags(Qt::TextBrowserInteraction);
    QPointF pt = insertAnchor(fx, QStringLiteral("click me"),
                               QStringLiteral("https://example.org"));
    QSignalSpy spy(&fx.control, &Markoff::TextControl::linkHovered);
    sendMouseMove(fx.control, pt, Qt::NoButton, Qt::NoModifier,
                  fx.contextWidget.get());
    QVERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().first().toString(),
             QStringLiteral("https://example.org"));
}

QTEST_MAIN(TstTextControlLinks)
#include "tst_textcontrol_links.moc"
