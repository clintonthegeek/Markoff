// SPDX-License-Identifier: GPL-3.0-or-later
//
// Verification test: does Markoff::Editor actually emit
// wikiLinkTrigger / tagTrigger when the user types "[[" or "#"?
// Written to bisect whether the claimed trigger path works at all
// (reports from the host app say the completion popup never appears).

#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include <QFocusEvent>

#include "markoff/Editor.h"

using Markoff::Editor;

class TestCompletionTriggers : public QObject {
    Q_OBJECT

private:
    Editor *m_editor = nullptr;

    void loadAndFocus(const QString &text = QStringLiteral("paragraph\n"))
    {
        m_editor = new Editor();
        m_editor->setPlainText(text);
        m_editor->resize(600, 400);
        m_editor->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_editor));
        m_editor->setFocus();
        QApplication::setActiveWindow(m_editor);
        QTest::qWait(80);
        // Click into the text area to ensure a MarkdownTextItem has focus.
        QTest::mouseClick(m_editor->viewport(), Qt::LeftButton,
                          Qt::NoModifier, QPoint(40, 20));
        QTest::qWait(80);
    }

private Q_SLOTS:
    void cleanup()
    {
        delete m_editor;
        m_editor = nullptr;
    }

    void testDoubleBracketEmitsWikiLinkTrigger()
    {
        loadAndFocus();
        QSignalSpy spy(m_editor, &Editor::wikiLinkTrigger);

        // Simulate typing "[[" character by character.
        QTest::keyClick(m_editor, Qt::Key_BracketLeft);
        QTest::qWait(30);
        QTest::keyClick(m_editor, Qt::Key_BracketLeft);
        QTest::qWait(80);

        qDebug() << "wikiLinkTrigger emissions:" << spy.count();
        qDebug() << "Editor source after typing:" << m_editor->toPlainText();

        QCOMPARE(spy.count(), 1);
    }

    void testHashAfterCharEmitsTagTrigger()
    {
        loadAndFocus(QStringLiteral("abc\n"));
        QSignalSpy spy(m_editor, &Editor::tagTrigger);

        // Position cursor at end of "abc", then type "#".
        // keyClick to End to ensure cursor is at end.
        QTest::keyClick(m_editor, Qt::Key_End);
        QTest::qWait(30);
        QTest::keyClick(m_editor, '#', Qt::NoModifier);
        QTest::qWait(80);

        qDebug() << "tagTrigger emissions:" << spy.count();
        qDebug() << "Editor source after typing:" << m_editor->toPlainText();

        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestCompletionTriggers)
#include "tst_completion_triggers.moc"
