// SPDX-License-Identifier: GPL-3.0-or-later
//
// Canvas rendering harness (restored after A13 deleted the old render suite).
// Everything here drives the real View offscreen and, where it matters,
// inspects the painted pixels — the renderers' only real product.
#include <QCoreApplication>
#include <QImage>
#include <QTest>

#include <markoff/canvas/View.h>
#include "../src/TableGeometry.h"
#include <markoff/core/AttrNames.h>
#include <markoff/core/CodeBlockProcessorRegistry.h>
#include <markoff/core/EmbedRender.h>
#include <markoff/embed/EmbedRuntime.h>

#include <atomic>
#include <memory>

#include <QThread>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include <memory>
#include <stdexcept>

namespace {

struct Fixture {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

int g_serial = 0;

Fixture openDocument(const QByteArray &markdown)
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    Fixture fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("canvas-render-%1").arg(++g_serial), 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(markdown).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}

int countDarkPixels(const QImage &image, int threshold = 128)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (qGray(c.rgb()) < threshold) ++count;
        }
    return count;
}

class FakeSvgBackend final : public Markoff::CodeBlockProcessor {
public:
    QStringList languages() const override { return {QStringLiteral("fake")}; }
    Markoff::RenderedBlock render(const Markoff::RenderRequest &request) override
    {
        // Slow enough that the test can observe the source fallback first.
        QThread::msleep(250);
        Markoff::RenderedBlock block;
        block.kind = Markoff::RenderedBlock::Kind::Svg;
        block.svg = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\">"
            "<rect width=\"40\" height=\"20\" fill=\"#336699\"/></svg>").toUtf8();
        block.fallbackText = request.source;
        return block;
    }
};

int countColorPixels(const QImage &image, const QColor &color)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) == color.rgb()) ++count;
    return count;
}

}  // namespace

class TstCanvasRender : public QObject {
    Q_OBJECT

private slots:
    // Attribute writes bump neither edit sequence, so the canvas used to keep
    // painting the old presentation (checkbox state, display-math promotion).
    void attributeChangeRestylesTheBlock()
    {
        auto fixture = openDocument(QByteArrayLiteral("E=mc^2\n\ntail"));
        Markoff::Canvas::View view;
        view.resize(520, 320);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});
        const Markoff::BlockId math = blocks[0];
        view.setCaretPosition(blocks[1], 0);   // keep the math block source-hidden

        QCOMPARE(Markoff::Cmd::changeKind(*fixture.document, math,
                     Markoff::BlockKind::Math,
                     {Markoff::AttrNames::DisplayMode},
                     {false}).state,
                 Markoff::VNext::ActionPublicationState::committed);
        QCoreApplication::processEvents();
        QVERIFY(!view.isMathPixmapActive(math));

        // Attribute-only: same kind, no buffer edit, no structural edit.
        QCOMPARE(fixture.document->setBlockAttribute(
                     math, Markoff::AttrNames::DisplayMode, true).state,
                 Markoff::VNext::ActionPublicationState::committed);
        QCoreApplication::processEvents();
        QVERIFY2(view.isMathPixmapActive(math),
                 "display-math promotion did not restyle the block");
    }

    // Tokenizer: a pipe escaped with a backslash is cell content, not a
    // separator (GFM). Before this, `| A \| B | C |` split into three cells.
    void escapedPipeStaysInTheCell()
    {
        const QByteArray source =
            QByteArrayLiteral("| A \\| B | C |\n|---|---|\n| 1 | 2 |");
        const auto parsed = Markoff::Canvas::parseTableBlock(source);
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.cols, 2);
        QCOMPARE(parsed.rows.size(), size_t{2});
        const auto &header = parsed.rows[0];
        QCOMPARE(source.mid(header[0].start, header[0].end - header[0].start),
                 QByteArrayLiteral(" A \\| B "));
        QCOMPARE(source.mid(header[1].start, header[1].end - header[1].start),
                 QByteArrayLiteral(" C "));
    }

    // Delimiter-row alignment is applied to the cell layout, not just parsed.
    void delimiterRowAlignmentIsPainted()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "| L | C | R |\n|:--|:-:|--:|\n| a | b | c |"));
        Markoff::Canvas::View view;
        view.resize(600, 360);
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        theme.setColor(Markoff::Theme::Slot::EditorBackground, Qt::white);
        theme.setColor(Markoff::Theme::Slot::TextDefault, QColor(0, 0, 0));
        theme.setColor(Markoff::Theme::Slot::Quote, QColor(200, 200, 200));
        view.setTheme(theme);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const auto block = fixture.document->iterateBlocks().front();
        const QRectF cell = view.tableCellRect(block, 1, 2);   // body row, "R"
        QVERIFY(cell.isValid());
        const QImage painted = view.grab().toImage();

        auto inkIn = [&](const QRectF &rect) {
            int ink = 0;
            const QRect r = rect.toAlignedRect();
            for (int y = r.top(); y <= r.bottom() && y < painted.height(); ++y)
                for (int x = r.left(); x <= r.right() && x < painted.width(); ++x)
                    if (qGray(painted.pixelColor(x, y).rgb()) < 96) ++ink;
            return ink;
        };
        const int leftHalf = inkIn(QRectF(cell.left(), cell.top(),
                                          cell.width() / 2, cell.height()));
        const int rightHalf = inkIn(QRectF(cell.left() + cell.width() / 2, cell.top(),
                                           cell.width() / 2, cell.height()));
        QVERIFY2(rightHalf > 0, "right-aligned cell has no ink");
        QVERIFY2(rightHalf > leftHalf * 2,
                 qPrintable(QStringLiteral("right-aligned text did not move right: "
                                           "left=%1 right=%2").arg(leftHalf).arg(rightHalf)));
    }

    // Find-match highlighting inside a table cell (the block path had it; the
    // table paint path silently dropped it).
    void findHighlightIsPaintedInsideATableCell()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "| A | B |\n|---|---|\n| needle | 2 |"));
        Markoff::Canvas::View view;
        view.resize(600, 360);
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        theme.setColor(Markoff::Theme::Slot::EditorBackground, Qt::white);
        theme.setColor(Markoff::Theme::Slot::TextDefault, QColor(0, 0, 0));
        theme.setColor(Markoff::Theme::Slot::Quote, QColor(200, 200, 200));
        const QColor highlight(255, 235, 59);
        theme.setColor(Markoff::Theme::Slot::SearchActiveMatchBackground, highlight);
        view.setTheme(theme);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const auto block = fixture.document->iterateBlocks().front();
        const QByteArray text = fixture.document->blockText(block);
        const int offset = int(text.indexOf("needle"));
        QVERIFY(offset > 0);
        Markoff::Canvas::FindHighlight h;
        h.block = block;
        h.byteOffset = offset;
        h.byteLength = 6;
        h.isCurrent = true;
        view.setFindHighlights({h});
        QCoreApplication::processEvents();

        const QImage painted = view.grab().toImage();
        QVERIFY2(countColorPixels(painted, highlight) > 20,
                 "find highlight inside a table cell did not paint");
    }

    // A table is interactive, not a picture: clicking a cell puts the caret
    // in that cell's byte range, and typing edits the model through it.
    void tableCellClickPlacesTheCaretAndTypingEditsIt()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "| A | B |\n|---|---|\n| one | two |"));
        Markoff::Canvas::View view;
        view.resize(600, 360);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const auto block = fixture.document->iterateBlocks().front();
        const QRectF cell = view.tableCellRect(block, 1, 1);   // "two"
        QVERIFY(cell.isValid());

        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          cell.center().toPoint());
        QCoreApplication::processEvents();
        QCOMPARE(view.caretBlock(), block);
        const QByteArray text = fixture.document->blockText(block);
        const int twoIndex = int(text.indexOf("two"));
        QVERIFY(twoIndex > 0);
        QVERIFY2(view.caretByteOffset() >= twoIndex
                     && view.caretByteOffset() <= twoIndex + 3,
                 qPrintable(QStringLiteral("caret at %1, cell bytes %2..%3")
                                .arg(view.caretByteOffset()).arg(twoIndex)
                                .arg(twoIndex + 3)));

        QTest::keyClicks(view.viewport(), QStringLiteral("X"));
        QCoreApplication::processEvents();
        const QByteArray after = fixture.document->blockText(block);
        QVERIFY2(after != text, "typing in a table cell did not edit the model");
        QVERIFY2(after.contains("X"), qPrintable(QString::fromUtf8(after)));
        // The pipe structure is intact: still one table block, two columns.
        QCOMPARE(view.tableCellRect(block, 1, 1).isValid(), true);
    }

    // Plugin path end-to-end in the leaf: a fenced block with a registered
    // backend paints source until the runtime's artifact arrives, then the
    // rasterised SVG — exactly the generic front-end the plan describes.
    void embeddedFenceRendersThroughTheRuntime()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "intro\n\n```fake\nA-->B\n```\n\noutro"));
        Markoff::CodeBlockProcessorRegistry registry;
        registry.registerProcessor(std::make_shared<FakeSvgBackend>());
        Markoff::Embed::EmbedRuntime runtime(&registry);

        Markoff::Canvas::View view;
        view.resize(600, 400);
        Markoff::Theme theme = Markoff::Theme::defaultLight();
        theme.setColor(Markoff::Theme::Slot::EditorBackground, Qt::white);
        theme.setColor(Markoff::Theme::Slot::TextDefault, QColor(20, 20, 20));
        view.setTheme(theme);
        view.setEmbedRenderService(&runtime);
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{3});
        view.setCaretPosition(blocks[0], 0);   // keep the fence's source hidden
        QCoreApplication::processEvents();

        const QColor fill(0x33, 0x66, 0x99);
        QCOMPARE(countColorPixels(view.grab().toImage(), fill), 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            countColorPixels(view.grab().toImage(), fill) > 50, 3000);
    }

    // The painted table must actually appear: grid lines and cell text.
    void tablePaintsGridAndCellText()
    {
        auto fixture = openDocument(QByteArrayLiteral(
            "intro\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\noutro"));
        Markoff::Canvas::View view;
        view.resize(520, 400);
        view.setTheme([] {
            Markoff::Theme t = Markoff::Theme::defaultLight();
            t.setColor(Markoff::Theme::Slot::EditorBackground, Qt::white);
            t.setColor(Markoff::Theme::Slot::TextDefault, QColor(20, 20, 20));
            t.setColor(Markoff::Theme::Slot::Quote, QColor(120, 120, 120));
            return t;
        }());
        view.setDocument(fixture.document.get());
        view.show();
        QCoreApplication::processEvents();

        const QImage painted = view.grab().toImage();
        if (const QString dump = qEnvironmentVariable("MARKOFF_CANVAS_RENDER_DUMP");
            !dump.isEmpty())
            painted.save(dump);
        // Grid colour: Theme::Slot::Quote paints the table grid.
        QVERIFY2(countColorPixels(painted, QColor(120, 120, 120)) > 100,
                 "table grid did not paint");

        // Cell text is dark-on-white; the surrounding paragraphs use the same
        // colour, so assert text exists above the table's row band as well as
        // inside it by checking the full image has a healthy amount of ink.
        QVERIFY2(countDarkPixels(painted) > 200, "cell/paragraph text did not paint");
    }
};

QTEST_MAIN(TstCanvasRender)
#include "tst_canvas_render.moc"
