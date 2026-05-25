// SPDX-License-Identifier: GPL-3.0-or-later
#include "WikilinkDecomposition.h"

#include <QTest>

using Markoff::LinkTarget;
using Markoff::Detail::decomposeWikilinkInner;

class TestLinkTargetDecomposition : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void plain_page() {
        const LinkTarget t = decomposeWikilinkInner(u"Page");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QVERIFY(t.alias.isEmpty());
        QVERIFY(t.section.isEmpty());
        QVERIFY(t.blockRef.isEmpty());
    }

    void page_with_alias() {
        const LinkTarget t = decomposeWikilinkInner(u"Page|Alias text");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.alias, QStringLiteral("Alias text"));
    }

    void page_with_section() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#Section");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.section, QStringLiteral("Section"));
        QVERIFY(t.blockRef.isEmpty());
    }

    void page_with_section_and_alias() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#Section|Alias");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.section, QStringLiteral("Section"));
        QCOMPARE(t.alias, QStringLiteral("Alias"));
    }

    void page_with_block_ref() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#^abc123");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.blockRef, QStringLiteral("abc123"));
        QVERIFY(t.section.isEmpty());
    }

    void page_with_block_ref_and_alias() {
        const LinkTarget t = decomposeWikilinkInner(u"Page#^abc123|Alias");
        QCOMPARE(t.page, QStringLiteral("Page"));
        QCOMPARE(t.blockRef, QStringLiteral("abc123"));
        QCOMPARE(t.alias, QStringLiteral("Alias"));
    }

    void same_doc_section() {
        const LinkTarget t = decomposeWikilinkInner(u"#Section");
        QVERIFY(t.page.isEmpty());
        QCOMPARE(t.section, QStringLiteral("Section"));
    }

    void same_doc_block_ref() {
        const LinkTarget t = decomposeWikilinkInner(u"#^abc123");
        QVERIFY(t.page.isEmpty());
        QCOMPARE(t.blockRef, QStringLiteral("abc123"));
    }

    void empty_inner_is_empty_target() {
        const LinkTarget t = decomposeWikilinkInner(u"");
        QVERIFY(t.isEmpty());
    }

    void embed_image_filename() {
        const LinkTarget t = decomposeWikilinkInner(u"image.png");
        QCOMPARE(t.page, QStringLiteral("image.png"));
    }
};

QTEST_APPLESS_MAIN(TestLinkTargetDecomposition)
#include "tst_link_target_decomposition.moc"
