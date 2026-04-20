// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>
#include <markoff/Editor.h>                        // live
#include <markoff/source/SourceEditor.h>
#include <markoff/reading/ReadingView.h>

using namespace Markoff;

class TstTriViewSmoke : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void allThreeAreMarkdownViews() {
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;

        const QVector<MarkdownView *> views = {&live, &source, &reading};
        for (MarkdownView *v : views) QVERIFY(v != nullptr);
    }

    void capabilityMatrix() {
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;

        QVERIFY(live.hasCursor());
        QVERIFY(live.hasEditing());
        QVERIFY(live.hasFold());

        QVERIFY(source.hasCursor());
        QVERIFY(source.hasEditing());
        QVERIFY(source.hasFold());

        QVERIFY(!reading.hasCursor());
        QVERIFY(!reading.hasEditing());
        QVERIFY(reading.hasFold());
    }

    void allAttachToOneDocument() {
        MarkoffDocument doc;
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;
        live.setDocument(&doc);
        source.setDocument(&doc);
        reading.setDocument(&doc);

        QCOMPARE(live.document(), &doc);
        QCOMPARE(source.document(), &doc);
        QCOMPARE(reading.document(), &doc);
    }

    void searchAdapterDispatchIsPolymorphic() {
        Editor live;
        Source::SourceEditor source;
        Reading::ReadingView reading;

        const QVector<MarkdownView *> views = {&live, &source, &reading};
        for (MarkdownView *v : views) QVERIFY(v->searchAdapter() != nullptr);

        // Only Reading refuses replace.
        QVERIFY(live.searchAdapter()->supportsReplace());
        QVERIFY(source.searchAdapter()->supportsReplace());
        QVERIFY(!reading.searchAdapter()->supportsReplace());
    }
};

QTEST_MAIN(TstTriViewSmoke)
#include "tst_tri_view_smoke.moc"
