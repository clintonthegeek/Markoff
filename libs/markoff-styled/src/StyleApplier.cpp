// SPDX-License-Identifier: GPL-3.0-or-later
#include "StyleApplier.h"

#include <QFont>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace {

QTextBlockFormat baseBlockFormat() {
    QTextBlockFormat fmt;
    fmt.setTopMargin(0);
    fmt.setBottomMargin(0);
    fmt.setLeftMargin(0);
    fmt.setIndent(0);
    return fmt;
}

void applyHeading(QTextCursor &cursor, int level, qreal fontScale) {
    static constexpr qreal kBaseSize = 11.0;
    static constexpr qreal kRatios[6] = { 2.0, 1.7, 1.4, 1.2, 1.0, 0.9 };
    const int idx = qBound(1, level, 6) - 1;
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(8 * fontScale);
    bf.setBottomMargin(4 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(kBaseSize * kRatios[idx] * fontScale);
    cf.setFontWeight(QFont::Bold);
    cursor.mergeBlockCharFormat(cf);
}

void applyParagraph(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(2 * fontScale);
    bf.setBottomMargin(2 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cf.setFontWeight(QFont::Normal);
    cursor.setBlockCharFormat(cf);
}

void applyCodeBlock(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(12 * fontScale);
    bf.setTopMargin(2);
    bf.setBottomMargin(2);
    bf.setBackground(QColor(245, 245, 245));  // Theme::CodeBlockBackground
                                              // resolved in Task 9 wiring.
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setFontPointSize(10.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

void applyBlockquote(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(16 * fontScale * qMax(1, depth));
    bf.setTopMargin(2);
    bf.setBottomMargin(2);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cf.setForeground(QColor(100, 100, 100));  // Theme::Quote.
    cursor.setBlockCharFormat(cf);
}

void applyListItem(QTextCursor &cursor, int depth, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setLeftMargin(16 * fontScale * qMax(1, depth + 1));
    bf.setTopMargin(1);
    bf.setBottomMargin(1);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontPointSize(11.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

void applyHorizontalRule(QTextCursor &cursor, qreal fontScale) {
    QTextBlockFormat bf = baseBlockFormat();
    bf.setTopMargin(6 * fontScale);
    bf.setBottomMargin(6 * fontScale);
    cursor.setBlockFormat(bf);

    QTextCharFormat cf;
    cf.setFontFamilies({QStringLiteral("monospace")});
    cf.setFontFixedPitch(true);
    cf.setForeground(QColor(180, 180, 180));
    cf.setFontPointSize(11.0 * fontScale);
    cursor.setBlockCharFormat(cf);
}

}  // namespace

namespace Markoff::Styled {

StyleApplier::StyleApplier(QObject *parent) : QObject(parent) {}
StyleApplier::~StyleApplier() = default;

void StyleApplier::setTextDocument(QTextDocument *doc) {
    if (m_textDocument == doc) return;
    m_textDocument = doc;
    rerender();
}

void StyleApplier::setMarkoffDocument(Markoff::MarkoffDocument *doc) {
    if (m_markoffDocument == doc) return;
    if (m_markoffDocument) {
        disconnect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                   this, &StyleApplier::onD2Changed);
    }
    m_markoffDocument = doc;
    if (m_markoffDocument) {
        connect(m_markoffDocument, &Markoff::MarkoffDocument::d2DocumentChanged,
                this, &StyleApplier::onD2Changed);
    }
    rerender();
}

void StyleApplier::setTheme(const Markoff::Theme *theme) {
    if (m_theme == theme) return;
    m_theme = theme;
    rerender();
}

void StyleApplier::setFontScale(qreal s) {
    if (qFuzzyCompare(m_fontScale, s)) return;
    m_fontScale = s;
    rerender();
}

void StyleApplier::rerender() {
    if (!m_textDocument || !m_markoffDocument) return;
    applyFormats();
}

void StyleApplier::onD2Changed() { applyFormats(); }

void StyleApplier::applyFormats() {
    if (m_applyingFormats) return;
    if (!m_textDocument || !m_markoffDocument) return;
    m_applyingFormats = true;
    {
        QSignalBlocker block(m_textDocument);
        QTextCursor cursor(m_textDocument);
        cursor.beginEditBlock();

        // The QTextDocument is populated with flatView() (separator-bearing:
        // blocks joined by "\n\n"). Compute block byte positions in that
        // coordinate space by summing block sizes + separator lengths, then
        // convert to Qt UTF-16 char positions.
        const QByteArray flatBytes = m_markoffDocument->flatView();
        const std::vector<Markoff::BlockId> blocks = m_markoffDocument->iterateBlocks();
        static constexpr int kSepLen = 2;  // "\n\n"

        quint32 bytePos = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const Markoff::BlockId id = blocks[i];
            const QByteArray text = m_markoffDocument->blockText(id);
            const quint32 blockStart = bytePos;
            const quint32 blockEnd   = bytePos + static_cast<quint32>(text.size());

            const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
            const int startQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockStart);
            const int endQt = Markoff::SourceTextDocumentBinding
                ::byteOffsetToQtPos(flatBytes, blockEnd);

            cursor.setPosition(startQt);
            QTextBlock qblk = cursor.block();
            while (qblk.isValid() && qblk.position() <= endQt) {
                QTextCursor blkCursor(qblk);
                if (kind == Markoff::BlockKind::Heading) {
                    int level = 0;
                    while (level < text.size() && text[level] == '#') ++level;
                    level = qBound(1, level, 6);
                    applyHeading(blkCursor, level, m_fontScale);
                } else if (kind == Markoff::BlockKind::Paragraph) {
                    applyParagraph(blkCursor, m_fontScale);
                } else if (kind == Markoff::BlockKind::CodeBlock) {
                    applyCodeBlock(blkCursor, m_fontScale);
                } else if (kind == Markoff::BlockKind::BlockQuote) {
                    int depth = 1;
                    const QByteArray text = m_markoffDocument->blockText(id);
                    if (!text.isEmpty()) {
                        depth = 0;
                        for (int i = 0; i < text.size() && text[i] == '>'; ++i) ++depth;
                        depth = qMax(1, depth);
                    }
                    applyBlockquote(blkCursor, depth, m_fontScale);
                } else if (kind == Markoff::BlockKind::ListItem) {
                    int depth = 0;
                    const QByteArray text = m_markoffDocument->blockText(id);
                    while (depth < text.size() && (text[depth] == ' ' || text[depth] == '\t')) ++depth;
                    depth /= 2;  // 2 spaces per indent level — close enough for v0.
                    applyListItem(blkCursor, depth, m_fontScale);
                } else if (kind == Markoff::BlockKind::HorizontalRule) {
                    applyHorizontalRule(blkCursor, m_fontScale);
                } else {
                    applyParagraph(blkCursor, m_fontScale);
                }
                qblk = qblk.next();
            }

            // Advance past block content + separator ("\n\n") between blocks.
            bytePos = blockEnd;
            if (i + 1 < blocks.size()) bytePos += kSepLen;
        }

        cursor.endEditBlock();
    }
    ++m_restyleCount;
    m_applyingFormats = false;
}

}  // namespace Markoff::Styled
