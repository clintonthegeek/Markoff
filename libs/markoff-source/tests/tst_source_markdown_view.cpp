// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/source/SourceEditor.h>

using namespace Markoff;

class TstSourceMarkdownView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void sourceIsAMarkdownView() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v != nullptr);
    }

    void capabilityProbes() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->hasCursor());
        QVERIFY(v->hasEditing());
        QVERIFY(v->hasFold());
        QVERIFY(!v->isReadOnly());
    }

    void setDocumentStoresPointer() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        MarkoffDocument doc;
        v->setDocument(&doc);
        QCOMPARE(v->document(), &doc);
    }

    void searchAdapterIsNonNull() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->searchAdapter() != nullptr);
    }

    void setReadOnlyRoundTrips() {
        Source::SourceEditor ed;
        MarkdownView *v = &ed;
        QVERIFY(v->setReadOnly(true));
        QVERIFY(v->isReadOnly());
        QVERIFY(v->setReadOnly(false));
        QVERIFY(!v->isReadOnly());
    }
};

QTEST_MAIN(TstSourceMarkdownView)
#include "tst_source_markdown_view.moc"
