// SPDX-License-Identifier: GPL-3.0-or-later
//
// P5.4 — images + renderer seams. Three consumer-injectable seams, per the
// plan's own wording:
//   - Image blocks (`![alt](src)`) paint via a consumer-provided resource
//     lookup (`View::setImageResourceLookup`); a placeholder box when no
//     lookup is set or the lookup misses.
//   - `setMermaidRenderer` injection: fenced code blocks whose info-string
//     language is "mermaid" paint the renderer's pixmap when the caret is
//     outside the block, source (plain code-block text) when inside.
//   - Embed seam only (`![[target]]`, Obsidian block-embed syntax):
//     `EmbedRegistry` consumption (`hasExtension()`), always
//     placeholder-rendered — no real `MarkdownRenderChild` mount this task.
//
// Falsification protocol (plan's own): see the plan's findings log entry
// for the throwaway commit SHAs that broke one of these tests.

#include <QTest>

#include <markoff/canvas/MediaSeams.h>
#include <markoff/canvas/View.h>
#include <markoff/core/EmbedRegistry.h>
#include <markoff/core/MarkdownRenderChild.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::ImageResourceLookup;
using Markoff::Canvas::MermaidRenderer;
using Markoff::Canvas::View;

namespace {

/// Place the caret at byte 0 of `block` by clicking its rect, the way a
/// user would (same helper shape as tst_canvas_math.cpp's).
bool clickAtBlockStart(View &view, BlockId block)
{
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    return view.caretBlock() == block && view.caretByteOffset() == 0;
}

/// Fixed 4x4 pixmap a fake image lookup/mermaid renderer hands back — non-
/// null and non-default-constructed is all these tests need to verify.
QPixmap fakePixmap()
{
    QPixmap pm(4, 4);
    pm.fill(Qt::red);
    return pm;
}

/// Renders any "mermaid" source to a fixed pixmap; never called for a
/// non-mermaid fence (that's the point of the seam being keyed by
/// language).
class FakeMermaidRenderer : public MermaidRenderer {
public:
    QPixmap render(const QString &source) const override
    {
        lastSource = source;
        ++renderCount;
        return fakePixmap();
    }
    mutable QString lastSource;
    mutable int renderCount = 0;
};

}  // namespace

class TstCanvasMediaSeams : public QObject {
    Q_OBJECT

private slots:
    void image_block_paints_resource_lookup_result();
    void image_block_with_no_lookup_or_miss_paints_placeholder();
    void mermaid_block_paints_renderer_pixmap_only_while_caret_is_outside();
    void embed_block_renders_placeholder_via_embed_registry();
    void unpromoted_paragraph_image_shapes_still_paint_placeholder();
};

void TstCanvasMediaSeams::image_block_paints_resource_lookup_result()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("![a cat](cat.png)\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(block), BlockKind::Image);

    // No lookup wired yet: placeholder.
    QVERIFY(view.isImagePlaceholderActive(block));
    QVERIFY(!view.isImagePixmapActive(block));

    QString requestedTarget;
    view.setImageResourceLookup([&](const QString &target) {
        requestedTarget = target;
        return fakePixmap();
    });

    QCOMPARE(requestedTarget, QStringLiteral("cat.png"));
    QVERIFY(view.isImagePixmapActive(block));
    QVERIFY(!view.isImagePlaceholderActive(block));
}

void TstCanvasMediaSeams::image_block_with_no_lookup_or_miss_paints_placeholder()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("![missing](does-not-exist.png)\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(block), BlockKind::Image);

    // No lookup set at all.
    QVERIFY(view.isImagePlaceholderActive(block));
    QVERIFY(!view.isImagePixmapActive(block));

    // A lookup that always misses (returns a null pixmap) behaves the same.
    view.setImageResourceLookup([](const QString &) { return QPixmap(); });
    QVERIFY(view.isImagePlaceholderActive(block));
    QVERIFY(!view.isImagePixmapActive(block));
}

void TstCanvasMediaSeams::mermaid_block_paints_renderer_pixmap_only_while_caret_is_outside()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("```mermaid\ngraph TD; A-->B;\n```\n\nOther paragraph.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId mermaidBlock = blocks[0];
    const BlockId other        = blocks[1];
    QCOMPARE(doc.blockKind(mermaidBlock), BlockKind::CodeBlock);

    // Caret elsewhere, no renderer yet: no pixmap (falls back to plain
    // code-block text, not a placeholder — mermaid has none).
    QVERIFY(clickAtBlockStart(view, other));
    QVERIFY(!view.isMermaidPixmapActive(mermaidBlock));

    FakeMermaidRenderer renderer;
    view.setMermaidRenderer(&renderer);

    // Renderer set, caret still outside the block: pixmap active.
    QVERIFY(view.isMermaidPixmapActive(mermaidBlock));
    QVERIFY(renderer.renderCount >= 1);
    QVERIFY(renderer.lastSource.contains(QStringLiteral("graph TD")));

    // Caret enters the block: source revealed, pixmap drops. Not via
    // clickAtBlockStart(): while the pixmap is active, the fence lines are
    // a hidden per-block delimiter run (same "isCodeBlockFence" rule
    // display Math's "$$" prefix uses, InlineFormatting.cpp) — OMITTED
    // from the layout text entirely, so a click near the left edge lands
    // on the first VISIBLE character, not byte 0 (same note tst_canvas_
    // math.cpp's own final case makes about display_math's "$$" prefix).
    const QRectF mermaidRect = view.blockRect(mermaidBlock);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(mermaidRect.x()) + 1, int(mermaidRect.y()) + 8));
    QCOMPARE(view.caretBlock(), mermaidBlock);
    QVERIFY(!view.isMermaidPixmapActive(mermaidBlock));

    // Caret leaves again: pixmap returns.
    QVERIFY(clickAtBlockStart(view, other));
    QVERIFY(view.isMermaidPixmapActive(mermaidBlock));
}

void TstCanvasMediaSeams::embed_block_renders_placeholder_via_embed_registry()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("![[notes/other-note.md]]\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(block), BlockKind::Image);

    // No registry wired: still a placeholder (always, this task — never a
    // real MarkdownRenderChild mount), labeled "no factory".
    QVERIFY(view.isEmbedPlaceholderActive(block));
    const QString unregisteredLabel = view.mediaLabelFor(block);
    QVERIFY(unregisteredLabel.contains(QStringLiteral("no factory")));

    Markoff::EmbedRegistry registry;
    QVERIFY(!registry.hasExtension(QStringLiteral("md")));
    registry.registerExtension(QStringLiteral("md"), [](const Markoff::EmbedRequest &) {
        return std::make_unique<Markoff::MarkdownRenderChild>();
    });
    QVERIFY(registry.hasExtension(QStringLiteral("md")));

    view.setEmbedRegistry(&registry);

    // Consumption proven directly: still placeholder-rendered (per the
    // plan — never a real MarkdownRenderChild mount this task), but the
    // label text now reflects the registry's own hasExtension("md")
    // answer — proof BlockLayoutCache::rebuildInline actually called
    // through the injected registry rather than ignoring it.
    QVERIFY(view.isEmbedPlaceholderActive(block));
    const QString registeredLabel = view.mediaLabelFor(block);
    QVERIFY(registeredLabel.contains(QStringLiteral("Embed:")));
    QVERIFY(!registeredLabel.contains(QStringLiteral("no factory")));
    QVERIFY(registeredLabel != unregisteredLabel);
}

void TstCanvasMediaSeams::unpromoted_paragraph_image_shapes_still_paint_placeholder()
{
    // Punch-list [cluster-k]: "some remote image embeds render as an empty/
    // invisible line until clicked into". Reproduced with an actual
    // multi-block document (not a single-block doc, where the default
    // caret-on-load landing on the one and only block would mask this):
    // BlockKind promotion (View::promoteCaretBlockKind) only fires from an
    // actual document EDIT with the caret in that exact block
    // (onDocumentChanged's own call site) — never from a plain caret move
    // (View::setCaret has no such hook) and never from load alone (the
    // load-time caret only ever lands on block 0). So EVERY still-
    // BlockKind::Paragraph image/embed line — with alt text, without alt
    // text, or a wikilink embed — is equally unpromoted here; verified
    // this is NOT an empty-alt-specific gap before fixing it as one.
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(
        "First paragraph, so block 0 is not one of the images below.\n\n"
        "![alt text](https://example.com/a.jpg)\n\n"
        "![](https://example.com/b.jpg)\n\n"
        "![[wikitarget]]\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 4);
    const BlockId withAlt   = blocks[1];
    const BlockId noAlt     = blocks[2];
    const BlockId wikilink  = blocks[3];

    // None of these were ever visited by the caret: still Paragraph, not
    // promoted to BlockKind::Image — the presentation fix must not mutate
    // the document to make the placeholder show.
    QCOMPARE(doc.blockKind(withAlt), BlockKind::Paragraph);
    QCOMPARE(doc.blockKind(noAlt), BlockKind::Paragraph);
    QCOMPARE(doc.blockKind(wikilink), BlockKind::Paragraph);

    // All three now paint a placeholder (no lookup wired) with a non-empty
    // label, matching the already-promoted-BlockKind::Image behavior the
    // other tests in this file cover.
    QVERIFY(view.isImagePlaceholderActive(withAlt));
    QVERIFY(!view.mediaLabelFor(withAlt).isEmpty());

    QVERIFY(view.isImagePlaceholderActive(noAlt));
    const QString noAltLabel = view.mediaLabelFor(noAlt);
    QVERIFY(!noAltLabel.isEmpty());
    QCOMPARE(noAltLabel, QStringLiteral("https://example.com/b.jpg"));

    QVERIFY(view.isEmbedPlaceholderActive(wikilink));
    QVERIFY(!view.mediaLabelFor(wikilink).isEmpty());
}

QTEST_MAIN(TstCanvasMediaSeams)
#include "tst_canvas_media_seams.moc"
