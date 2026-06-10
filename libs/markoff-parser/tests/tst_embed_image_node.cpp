// SPDX-License-Identifier: GPL-3.0-or-later
//
// `![[…]]` wiki-embeds that tree-sitter parses as `image` nodes (rather
// than `wiki_link`) must yield the same LinkInfo as the wiki_link path:
// type=Embed, bracket-free target, consistent displayText convention.
// Spec: Corbomite steer 2026-06-04 (embed-image-node-target).
#include <QTest>
#include <markoff/parser/Document.h>

using namespace Markoff;

class TestEmbedImageNode : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void embedWithExtension()
    {
        auto doc = Document::fromMarkdown(QStringLiteral("![[Image.png]]\n"));
        QCOMPARE(doc->links().size(), 1);
        const LinkInfo &l = doc->links().at(0);
        QCOMPARE(l.type, LinkInfo::Embed);
        QCOMPARE(l.target, QStringLiteral("Image.png"));
        QVERIFY(!l.displayText.startsWith(QLatin1Char('[')));
    }

    void embedWithoutExtension()
    {
        auto doc = Document::fromMarkdown(QStringLiteral("![[Note]]\n"));
        QCOMPARE(doc->links().size(), 1);
        const LinkInfo &l = doc->links().at(0);
        QCOMPARE(l.type, LinkInfo::Embed);
        QCOMPARE(l.target, QStringLiteral("Note"));
        QVERIFY(!l.displayText.startsWith(QLatin1Char('[')));
    }

    void embedWithAlias()
    {
        auto doc = Document::fromMarkdown(QStringLiteral("![[Target|Alias]]\n"));
        QCOMPARE(doc->links().size(), 1);
        const LinkInfo &l = doc->links().at(0);
        QCOMPARE(l.type, LinkInfo::Embed);
        QCOMPARE(l.target, QStringLiteral("Target"));
        QCOMPARE(l.displayText, QStringLiteral("Alias"));
    }

    void standardImageUnchanged()
    {
        auto doc = Document::fromMarkdown(QStringLiteral("![alt](path.png)\n"));
        QCOMPARE(doc->links().size(), 1);
        const LinkInfo &l = doc->links().at(0);
        QCOMPARE(l.type, LinkInfo::Image);
        QCOMPARE(l.target, QStringLiteral("path.png"));
        QCOMPARE(l.displayText, QStringLiteral("alt"));
    }
};

QTEST_MAIN(TestEmbedImageNode)
#include "tst_embed_image_node.moc"
