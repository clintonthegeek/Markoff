// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>
#include <markoff/reading/ReadingView.h>

using namespace Markoff;

class TstReadingMarkdownView : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void readingIsAMarkdownView() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(v != nullptr);
    }

    void capabilityProbes() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(!v->hasCursor());
        QVERIFY(!v->hasEditing());
        QVERIFY(v->hasFold());
        QVERIFY(v->isReadOnly());
    }

    void setReadOnlyFalseIsRefused() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(!v->setReadOnly(false));
        QVERIFY(v->isReadOnly());
    }

    void searchAdapterRefusesReplace() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        QVERIFY(v->searchAdapter() != nullptr);
        QVERIFY(!v->searchAdapter()->supportsReplace());
    }

    void setDocumentStoresPointer() {
        Reading::ReadingView rv;
        MarkdownView *v = &rv;
        MarkoffDocument doc;
        v->setDocument(&doc);
        QCOMPARE(v->document(), &doc);
    }
};

QTEST_MAIN(TstReadingMarkdownView)
#include "tst_reading_markdown_view.moc"
