// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "SceneCoordinator.h"
#include "SelectableItem.h"
#include "TextControl.h"

using namespace Markoff;

class TstTextControlIntegration : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cursorRight_overRealMathGlyph_advancesByOne();
    void cursorLeft_overRealMathGlyph_retreatsByOne();
    void backspace_adjacentToMathGlyph_removesGlyphAndSource();
    void selection_spanningMathGlyph_yieldsSourceInToPlainText();
    void cursorRight_overRealCheckbox_advancesByOne();
    void arrowKey_traversesMixedMathAndCheckboxLine();

private:
    struct EditorHandle {
        Editor *editor;
        MarkdownTextItem *item;
    };
    EditorHandle makeEditor(const QString &markdown);
};

TstTextControlIntegration::EditorHandle
TstTextControlIntegration::makeEditor(const QString &markdown)
{
    auto *editor = new Editor;
    editor->resize(600, 400);
    editor->setPlainText(markdown);
    editor->show();
    QApplication::processEvents();
    // Wait for the 150 ms reparse debounce so inline substitution applies.
    QTest::qWait(300);
    QApplication::processEvents();
    MarkdownTextItem *ti = nullptr;
    for (auto *item : editor->coordinatorForTesting()->items()) {
        if (item->isTextItem()) {
            ti = static_cast<MarkdownTextItem *>(item);
            break;
        }
    }
    return {editor, ti};
}

void TstTextControlIntegration::cursorRight_overRealMathGlyph_advancesByOne()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    const QString docText = h.item->document()->toPlainText();
    int fffcIdx = docText.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY2(fffcIdx != -1,
             qPrintable(QStringLiteral("expected U+FFFC in doc: '%1'")
                        .arg(docText)));
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QCOMPARE(h.item->textControl()->textCursor().position(), fffcIdx + 1);
    delete h.editor;
}

void TstTextControlIntegration::cursorLeft_overRealMathGlyph_retreatsByOne()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    const QString docText = h.item->document()->toPlainText();
    int fffcIdx = docText.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY(fffcIdx != -1);
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx + 1);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QCOMPARE(h.item->textControl()->textCursor().position(), fffcIdx);
    delete h.editor;
}

void TstTextControlIntegration::backspace_adjacentToMathGlyph_removesGlyphAndSource()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    const QString initialDoc = h.item->document()->toPlainText();
    int fffcIdx = initialDoc.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY(fffcIdx != -1);
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx + 1);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QApplication::processEvents();
    QTest::qWait(300);
    QApplication::processEvents();
    auto *coord = h.editor->coordinatorForTesting();
    const QString serialised = coord->toMarkdown();
    QVERIFY2(!serialised.contains(QStringLiteral("$x^2$")),
             qPrintable(QStringLiteral("expected math source removed, got '%1'")
                        .arg(serialised)));
    delete h.editor;
}

void TstTextControlIntegration::selection_spanningMathGlyph_yieldsSourceInToPlainText()
{
    auto h = makeEditor(QStringLiteral("a $x^2$ b"));
    QVERIFY(h.item);
    h.item->textControl()->selectAll();
    const QString selected = h.item->textControl()
                                 ->textCursor()
                                 .selectedText();
    QVERIFY(selected.contains(QChar(QChar::ObjectReplacementCharacter)));
    delete h.editor;
}

void TstTextControlIntegration::cursorRight_overRealCheckbox_advancesByOne()
{
    auto h = makeEditor(QStringLiteral("- [ ] task"));
    QVERIFY(h.item);
    const QString docText = h.item->document()->toPlainText();
    int fffcIdx = docText.indexOf(QChar(QChar::ObjectReplacementCharacter));
    QVERIFY2(fffcIdx != -1,
             qPrintable(QStringLiteral("expected checkbox FFFC in doc: '%1'")
                        .arg(docText)));
    QTextCursor c(h.item->document());
    c.setPosition(fffcIdx);
    h.item->textControl()->setTextCursor(c);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    QCOMPARE(h.item->textControl()->textCursor().position(), fffcIdx + 1);
    delete h.editor;
}

void TstTextControlIntegration::arrowKey_traversesMixedMathAndCheckboxLine()
{
    auto h = makeEditor(QStringLiteral("- [ ] see $x^2$"));
    QVERIFY(h.item);
    QTextCursor start(h.item->document());
    start.setPosition(0);
    h.item->textControl()->setTextCursor(start);
    int lastPos = -1;
    const int totalLen = h.item->document()->characterCount() - 1;
    for (int i = 0; i < totalLen + 2; ++i) {
        int curPos = h.item->textControl()->textCursor().position();
        QVERIFY2(curPos >= lastPos,
                 qPrintable(QStringLiteral("cursor retreated at step %1")
                            .arg(i)));
        lastPos = curPos;
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        h.item->textControl()->processEvent(&ev, QPointF(0, 0));
    }
    QCOMPARE(h.item->textControl()->textCursor().position(), totalLen);
    delete h.editor;
}

QTEST_MAIN(TstTextControlIntegration)
#include "tst_textcontrol_integration.moc"
