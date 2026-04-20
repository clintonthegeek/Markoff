// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/MarkdownView.h>
#include <markoff/MarkoffDocument.h>
#include <markoff/SearchAdapter.h>

using namespace Markoff;

namespace {
class DummyAdapter : public SearchAdapter {
public:
    int cursorSourceOffset() const override { return 0; }
    void highlightMatches(QVector<TextSpan>) override {}
    void clearMatchHighlight() override {}
    void scrollMatchIntoView(TextSpan) override {}
};

class MinimalView : public MarkdownView {
    Q_OBJECT
public:
    void setDocument(MarkoffDocument *d) override { m_doc = d; }
    MarkoffDocument *document() const override { return m_doc; }
    void setViewTheme(const Theme &) override {}
    void setViewResourceProvider(ResourceProvider *) override {}
    void setViewLinkResolver(LinkResolver *) override {}
    float scrollPosition() const override { return 0.f; }
    void setScrollPosition(float) override {}
    void zoomIn() override {}
    void zoomOut() override {}
    void resetZoom() override {}
    QJsonObject ephemeralState() const override { return {}; }
    void setEphemeralState(const QJsonObject &) override {}
    SearchAdapter *searchAdapter() override { return &m_adapter; }
private:
    MarkoffDocument *m_doc = nullptr;
    DummyAdapter m_adapter;
};
}  // namespace

class TstMarkdownViewCompile : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void constructsAndCasts() {
        MinimalView v;
        MarkdownView *base = &v;
        MarkoffDocument d;
        base->setDocument(&d);
        QCOMPARE(base->document(), &d);
        QVERIFY(!base->hasCursor());
        QVERIFY(!base->hasEditing());
        QVERIFY(!base->hasFold());
        QVERIFY(base->isReadOnly());            // !hasEditing() → true
        QVERIFY(!base->setReadOnly(false));     // capability-denied default
        QVERIFY(base->searchAdapter() != nullptr);
    }
};

QTEST_MAIN(TstMarkdownViewCompile)
#include "tst_markdown_view_compile.moc"
