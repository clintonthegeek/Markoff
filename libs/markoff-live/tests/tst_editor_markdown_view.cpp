// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/Editor.h>
#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>

using namespace Markoff;

class TstEditorMarkdownView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editorIsAMarkdownView() {
        Editor ed;
        MarkdownView *v = &ed;        // upcast compiles -> inheritance holds
        QVERIFY(v != nullptr);
    }

    void capabilityProbes() {
        Editor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->hasCursor());
        QVERIFY(v->hasEditing());
        QVERIFY(v->hasFold());
        QVERIFY(!v->isReadOnly());
    }

    void setDocumentStoresPointer() {
        Editor ed;
        MarkdownView *v = &ed;
        MarkoffDocument doc;
        v->setDocument(&doc);
        QCOMPARE(v->document(), &doc);
    }

    void scrollRoundTrips() {
        Editor ed;
        MarkdownView *v = &ed;
        ed.setPlainText(QStringLiteral("a\nb\nc\n"));
        v->setScrollPosition(1.0f);
        QVERIFY(v->scrollPosition() >= 0.0f);
    }

    void searchAdapterIsNonNull() {
        Editor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->searchAdapter() != nullptr);
    }
};

QTEST_MAIN(TstEditorMarkdownView)
#include "tst_editor_markdown_view.moc"
