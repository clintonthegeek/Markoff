// SPDX-License-Identifier: GPL-3.0-or-later
#include "MarkdownTextItem.h"
#include "TextControl.h"
#include "MathTextObject.h"
#include "CheckboxTextObject.h"

#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextCursor>
#include <QAbstractTextDocumentLayout>
#include <QStyleOptionGraphicsItem>
#include "MarkdownHighlighter.h"
#include <markoff-parser/SourceSpan.h>
#include <QGraphicsSceneMouseEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlockFormat>
#include "TableSerializer.h"
#include <QTextTable>
#include <QTextFrame>

namespace Markoff {

MarkdownTextItem::MarkdownTextItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_document(new QTextDocument(this))
    , m_control(new TextControl(this))
    , m_mathObject(new MathTextObject(this))
    , m_checkboxObject(new CheckboxTextObject(this))
{
    m_control->setDocument(m_document);
    m_control->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_document->setDocumentMargin(8);

    // Register inline object handlers with this document's layout.
    m_document->documentLayout()->registerHandler(MathTextObject::TypeId, m_mathObject);
    m_document->documentLayout()->registerHandler(CheckboxTextObject::TypeId, m_checkboxObject);

    setFlag(ItemIsFocusable);
    setFlag(ItemAcceptsInputMethod);
    setAcceptedMouseButtons(Qt::AllButtons);

    connect(m_document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, &MarkdownTextItem::updateGeometry);
    connect(m_control, &TextControl::updateRequest,
            this, [this]() { update(); });
    connect(m_control, &TextControl::textChanged,
            this, &MarkdownTextItem::textChanged);
    connect(m_control, &TextControl::cursorPositionChanged,
            this, &MarkdownTextItem::onCursorPositionChanged);
}

MarkdownTextItem::~MarkdownTextItem() = default;

void MarkdownTextItem::setPlainText(const QString &text)
{
    m_document->setPlainText(text);
    detectDecoratedRanges();
    applyBlockFormats();
}

void MarkdownTextItem::setTextWidth(qreal width)
{
    if (qFuzzyCompare(m_width, width))
        return;
    prepareGeometryChange();
    m_width = width;
    m_document->setTextWidth(width);
}

QTextDocument *MarkdownTextItem::document() const
{
    return m_document;
}

QRectF MarkdownTextItem::boundingRect() const
{
    return {0, 0, m_width, m_document->size().height()};
}

void MarkdownTextItem::paint(QPainter *painter,
                             const QStyleOptionGraphicsItem * /*option*/,
                             QWidget *widget)
{
    painter->save();
    paintDecoratedRanges(painter);
    m_control->drawContents(painter, boundingRect(), widget);
    painter->restore();
}

int MarkdownTextItem::hitTest(const QPointF &scenePos) const
{
    QPointF localPos = mapFromScene(scenePos);
    return m_document->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
}

void MarkdownTextItem::setSelection(int anchorPos, int cursorPos)
{
    QTextCursor cursor(m_document);
    cursor.setPosition(anchorPos);
    cursor.setPosition(cursorPos, QTextCursor::KeepAnchor);
    m_control->setTextCursor(cursor);
}

void MarkdownTextItem::clearSelection()
{
    QTextCursor cursor = m_control->textCursor();
    cursor.clearSelection();
    m_control->setTextCursor(cursor);
}

QString MarkdownTextItem::selectedMarkdown() const
{
    QTextCursor cursor = m_control->textCursor();
    if (!cursor.hasSelection())
        return {};

    const int selStart = cursor.selectionStart();
    const int selEnd = cursor.selectionEnd();

    // Collect table frames sorted by position for overlap detection.
    QList<QTextTable *> tables;
    for (auto *frame : m_document->rootFrame()->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(frame))
            tables.append(t);
    }
    std::sort(tables.begin(), tables.end(), [](QTextTable *a, QTextTable *b) {
        return a->firstPosition() < b->firstPosition();
    });

    // Walk the root frame in document order, emitting text blocks and
    // tables that overlap the selection.
    QString out;
    out.reserve(selEnd - selStart);

    QTextFrame *root = m_document->rootFrame();
    bool firstElement = true;

    for (auto it = root->begin(); it != root->end(); ++it) {
        if (auto *childFrame = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(childFrame)) {
                // If any part of the table overlaps the selection, emit the whole table
                if (table->lastPosition() >= selStart && table->firstPosition() <= selEnd) {
                    if (!firstElement)
                        out += QLatin1Char('\n');
                    out += TableSerializer::serialize(table);
                    firstElement = false;
                }
            }
        } else {
            QTextBlock block = it.currentBlock();
            if (!block.isValid())
                continue;

            const int blockStart = block.position();
            const int blockEnd = blockStart + block.length() - 1; // exclude paragraph sep
            if (blockEnd < selStart || blockStart >= selEnd)
                continue;

            const int sliceStart = qMax(selStart, blockStart);
            const int sliceEnd   = qMin(selEnd, blockEnd);

            if (!firstElement)
                out += QLatin1Char('\n');

            // Expand U+FFFC inline objects in the selected range
            for (auto fragIt = block.begin(); !fragIt.atEnd(); ++fragIt) {
                const QTextFragment frag = fragIt.fragment();
                if (!frag.isValid()) continue;
                const int fragStart = frag.position();
                const int fragEnd = fragStart + frag.length();
                if (fragEnd <= sliceStart) continue;
                if (fragStart >= sliceEnd) break;

                const QTextCharFormat fmt = frag.charFormat();
                const QString fragText = frag.text();
                const int localStart = qMax(0, sliceStart - fragStart);
                const int localEnd = qMin(fragText.size(), sliceEnd - fragStart);

                const QString raw = fmt.property(MathTextObject::RawProperty).toString();
                if (!raw.isEmpty()) {
                    for (int i = localStart; i < localEnd; ++i) {
                        if (fragText.at(i) == QChar::ObjectReplacementCharacter)
                            out += raw;
                        else
                            out += fragText.at(i);
                    }
                } else {
                    out += fragText.mid(localStart, localEnd - localStart);
                }
            }
            firstElement = false;
        }
    }
    return out;
}

QString MarkdownTextItem::allMarkdown() const
{
    // Walk the root frame using QTextFrame::iterator, which visits both
    // regular text blocks AND child frames (QTextTable) in document order.
    // The old doc->begin()/block.next() loop only visited top-level blocks,
    // silently skipping blocks inside QTextTable child frames.
    QString out;
    out.reserve(m_document->characterCount());

    QTextFrame *root = m_document->rootFrame();
    bool firstElement = true;

    for (auto it = root->begin(); it != root->end(); ++it) {
        if (auto *childFrame = it.currentFrame()) {
            // Child frame — serialize tables via TableSerializer
            if (auto *table = qobject_cast<QTextTable *>(childFrame)) {
                if (!firstElement)
                    out += QLatin1Char('\n');
                out += TableSerializer::serialize(table);
                firstElement = false;
            }
        } else {
            QTextBlock block = it.currentBlock();
            if (!block.isValid())
                continue;

            if (!firstElement)
                out += QLatin1Char('\n');

            // Expand U+FFFC inline objects back to their stored raw source
            const QString blockText = block.text();
            if (!blockText.contains(QChar::ObjectReplacementCharacter)) {
                out += blockText;
            } else {
                for (auto fragIt = block.begin(); !fragIt.atEnd(); ++fragIt) {
                    const QTextFragment frag = fragIt.fragment();
                    if (!frag.isValid()) continue;
                    const QTextCharFormat fmt = frag.charFormat();
                    const QString text = frag.text();
                    const QString raw = fmt.property(MathTextObject::RawProperty).toString();
                    if (!raw.isEmpty() && text.size() == 1
                        && text.at(0) == QChar::ObjectReplacementCharacter) {
                        out += raw;
                    } else {
                        for (QChar c : text) {
                            if (c != QChar::ObjectReplacementCharacter)
                                out += c;
                        }
                    }
                }
            }
            firstElement = false;
        }
    }
    return out;
}

int MarkdownTextItem::documentLength() const
{
    // characterCount() includes trailing paragraph separator; subtract 1
    // for the last valid cursor position.
    return qMax(0, m_document->characterCount() - 1);
}

QString MarkdownTextItem::toMarkdown() const
{
    return allMarkdown();
}

void MarkdownTextItem::setBlockFolded(int blockNumber, bool folded)
{
    QTextBlock block = m_document->findBlockByNumber(blockNumber);
    if (!block.isValid()) return;
    if (block.isVisible() == !folded) return;  // already in desired state

    // QTextBlock::setVisible is the proper Qt primitive: it tells the
    // document layout to skip the block entirely (no glyph painting, no
    // height contribution). Setting line-height to 0 only collapses
    // spacing — glyphs still draw, causing pile-up when sibling blocks
    // share the same Y coordinate. setVisible avoids that.
    prepareGeometryChange();
    block.setVisible(!folded);
    // Force the document layout to re-measure from this block onward so
    // subsequent block Y-positions reflect the visibility change.
    m_document->markContentsDirty(block.position(), block.length());
    update();
}

int MarkdownTextItem::stripInlineSubstitutions()
{
    if (m_inSubstitution) return 0;

    // Find every U+FFFC inline object (math, checkbox, etc.) that carries a
    // RawProperty, and replace it with the stored source. Mutate from the END
    // to keep earlier offsets stable.
    struct Hit { int pos; QString raw; };
    QList<Hit> hits;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            const QTextCharFormat fmt = frag.charFormat();
            const QString raw = fmt.property(MathTextObject::RawProperty).toString();
            if (raw.isEmpty()) continue;
            const QString text = frag.text();
            for (int i = 0; i < text.size(); ++i) {
                if (text.at(i) != QChar::ObjectReplacementCharacter) continue;
                hits.append({frag.position() + i, raw});
            }
        }
    }
    if (hits.isEmpty()) return 0;

    m_inSubstitution = true;
    const bool blocked = m_document->blockSignals(true);
    int delta = 0;
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (int i = hits.size() - 1; i >= 0; --i) {
        const Hit &h = hits[i];
        cursor.setPosition(h.pos);
        cursor.setPosition(h.pos + 1, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.insertText(h.raw);
        delta += h.raw.size() - 1;
    }
    cursor.endEditBlock();
    m_document->blockSignals(blocked);
    m_inSubstitution = false;
    return delta;
}

QString MarkdownTextItem::buildHighlightingSource() const
{
    // Build a string with the same character count as the document where
    // blocks inside QTextTable frames are replaced with spaces. This lets
    // tree-sitter produce spans in document-coordinate space without being
    // confused by the QTextTable frame structure.
    //
    // Precondition: document must be in source form (after
    // stripInlineSubstitutions) so that non-table text matches what
    // tree-sitter expects to parse.

    const int charCount = m_document->characterCount();
    QString src(charCount, QLatin1Char('\n'));  // fill with newlines (paragraph separators)

    // Collect table frame ranges for fast lookup
    struct Range { int first; int last; };
    QList<Range> tableRanges;
    for (auto *frame : m_document->rootFrame()->childFrames()) {
        if (qobject_cast<QTextTable *>(frame))
            tableRanges.append({frame->firstPosition(), frame->lastPosition()});
    }

    auto isInTable = [&](int pos) -> bool {
        for (const auto &r : tableRanges) {
            if (pos >= r.first && pos <= r.last)
                return true;
        }
        return false;
    };

    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const int pos = block.position();
        if (isInTable(pos)) {
            // Leave as spaces/newlines — tree-sitter will ignore them
            continue;
        }

        // Copy block text at the correct document position
        const QString text = block.text();
        for (int i = 0; i < text.length() && pos + i < charCount; ++i) {
            src[pos + i] = text[i];
        }
        // The paragraph separator at pos + text.length() is already '\n'
    }

    return src;
}

void MarkdownTextItem::applyInlineSubstitutions()
{
    if (m_inSubstitution) return;

    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (!hl) return;

    const QString docText = m_document->toPlainText();
    const int cursorPos = m_control->textCursor().position();

    // Unified substitution entry — math and checkboxes share one list so
    // we can apply all replacements from end-to-start in one pass.
    struct Entry {
        int start;
        int len;
        QTextCharFormat fmt;
        bool skipForReveal = false; // math cursor-reveal: keep as source
    };
    QList<Entry> entries;

    // --- Math entries ---
    // Group abutting math spans into runs, decode $/$$ form.
    {
        QList<SourceSpan> mathSpans;
        for (const SourceSpan &s : hl->spans()) {
            if (s.math || s.mathDisplay)
                mathSpans.append(s);
        }
        std::sort(mathSpans.begin(), mathSpans.end(),
                  [](const SourceSpan &a, const SourceSpan &b) {
                      return a.charOffset < b.charOffset;
                  });

        struct Run { int start; int end; };
        QList<Run> runs;
        for (const SourceSpan &s : mathSpans) {
            if (s.charLength <= 0) continue;
            const int rStart = s.charOffset;
            const int rEnd   = s.charOffset + s.charLength;
            if (!runs.isEmpty() && runs.last().end == rStart)
                runs.last().end = rEnd;
            else
                runs.append({rStart, rEnd});
        }

        for (const Run &run : runs) {
            if (run.start < 0 || run.end > docText.size() || run.end <= run.start)
                continue;
            const QString slice = docText.mid(run.start, run.end - run.start);

            bool display = false;
            QString latex;
            if (slice.size() >= 4 && slice.startsWith(QStringLiteral("$$"))
                                  && slice.endsWith(QStringLiteral("$$"))) {
                display = true;
                latex = slice.mid(2, slice.size() - 4);
            } else if (slice.size() >= 2 && slice.startsWith(QLatin1Char('$'))
                                         && slice.endsWith(QLatin1Char('$'))) {
                display = false;
                latex = slice.mid(1, slice.size() - 2);
            } else {
                continue;
            }
            if (latex.isEmpty()) continue;

            QTextCharFormat fmt;
            fmt.setObjectType(MathTextObject::TypeId);
            fmt.setProperty(MathTextObject::SourceProperty, latex);
            fmt.setProperty(MathTextObject::DisplayProperty, display);
            const QString delim = display ? QStringLiteral("$$") : QStringLiteral("$");
            fmt.setProperty(MathTextObject::RawProperty, delim + latex + delim);

            bool reveal = (cursorPos > run.start && cursorPos < run.end);
            entries.append({run.start, run.end - run.start, fmt, reveal});
        }
    }

    // --- Checkbox entries ---
    for (const SourceSpan &s : hl->spans()) {
        if (!s.isTaskMarker || s.charLength <= 0) continue;
        const int start = s.charOffset;
        const int len = s.charLength;
        if (start < 0 || start + len > docText.size()) continue;

        const QString raw = docText.mid(start, len);
        // [x] or [X] = checked; [ ] = unchecked
        bool checked = raw.contains(QLatin1Char('x'), Qt::CaseInsensitive);

        QTextCharFormat fmt;
        fmt.setObjectType(CheckboxTextObject::TypeId);
        fmt.setProperty(CheckboxTextObject::CheckedProperty, checked);
        fmt.setProperty(MathTextObject::RawProperty, raw); // shared for round-trip

        entries.append({start, len, fmt, false});
    }

    if (entries.isEmpty()) return;

    // Sort ascending by start position, then apply from end to start.
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.start < b.start; });

    m_inSubstitution = true;

    // Block document signals during the substitution pass. This prevents
    // adjustSpanOffsets from running on each individual remove+insert
    // (which causes cascading offset errors across multiple checkboxes)
    // and suppresses the automatic rehighlight that would use the now-stale
    // span map. The highlighting was already applied before substitution;
    // the glyphs render via QTextObjectInterface, not the highlighter.
    const bool blocked = m_document->blockSignals(true);

    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    int newRevealedStart = -1;
    int newRevealedEnd = -1;
    bool newRevealedIsDisplay = false;

    for (int i = entries.size() - 1; i >= 0; --i) {
        const Entry &e = entries[i];
        if (e.skipForReveal) {
            newRevealedStart = e.start;
            newRevealedEnd = e.start + e.len;
            newRevealedIsDisplay = e.fmt.property(MathTextObject::DisplayProperty).toBool();
            continue;
        }

        cursor.setPosition(e.start);
        cursor.setPosition(e.start + e.len, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), e.fmt);
    }
    cursor.endEditBlock();
    m_document->blockSignals(blocked);

    // Save span map at source positions before adjustment so
    // refreshInlineSubstitutions() can restore them after stripping.
    m_sourcePositionSpans = hl->spans();

    // Adjust span offsets to match the substituted document. Each non-skipped
    // entry replaced `len` chars with 1 char. Entries are sorted ascending by
    // start (source positions). Compute cumulative shift for each span.
    {
        // Build shift table: source positions where substitutions occurred
        struct Shift { int srcPos; int delta; }; // delta = len - 1
        QList<Shift> shifts;
        for (const Entry &e : entries) {
            if (e.skipForReveal) continue;
            shifts.append({e.start, e.len - 1});
        }

        for (SourceSpan &span : hl->mutableSpans()) {
            int cumShift = 0;
            for (const Shift &s : shifts) {
                if (s.srcPos >= span.charOffset) break;
                cumShift += s.delta;
            }
            span.charOffset -= cumShift;
            span.charLength = qMax(0, span.charLength);
            if (span.parentCharStart >= 0) {
                int parentShift = 0;
                for (const Shift &s : shifts) {
                    if (s.srcPos >= span.parentCharStart) break;
                    parentShift += s.delta;
                }
                span.parentCharStart -= parentShift;
                int parentEndShift = 0;
                for (const Shift &s : shifts) {
                    if (s.srcPos >= span.parentCharEnd) break;
                    parentEndShift += s.delta;
                }
                span.parentCharEnd -= parentEndShift;
            }
        }
        hl->rehighlight();
    }

    m_inSubstitution = false;

    m_revealedStart = newRevealedStart;
    m_revealedEnd = newRevealedEnd;
    m_revealedIsDisplay = newRevealedIsDisplay;
}

void MarkdownTextItem::refreshInlineSubstitutions()
{
    if (m_inSubstitution) return;

    stripInlineSubstitutions();

    // applyInlineSubstitutions() adjusts span offsets to match the
    // substituted document. After stripping, the document is back in
    // source form but the spans still reflect substituted positions.
    // Restore the saved source-position spans so apply can find the
    // math regions at their correct source offsets.
    if (!m_sourcePositionSpans.isEmpty()) {
        auto *hl = qobject_cast<MarkdownHighlighter *>(
            m_document->findChild<QSyntaxHighlighter *>());
        if (hl)
            hl->mutableSpans() = m_sourcePositionSpans;
    }

    applyInlineSubstitutions();
}

void MarkdownTextItem::refreshBlockFormatting()
{
    detectDecoratedRanges();
    applyBlockFormats();
    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (hl)
        hl->setDecoratedRanges(m_decoratedRanges);
}

void MarkdownTextItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // Check for checkbox toggle before other event handling.
    if (event->button() == Qt::LeftButton) {
        const int pos = m_document->documentLayout()->hitTest(event->pos(), Qt::FuzzyHit);
        auto checkboxAt = [&](int p) -> int {
            if (p < 0 || p >= m_document->characterCount() - 1) return -1;
            QTextCursor c(m_document);
            c.setPosition(p);
            c.setPosition(p + 1, QTextCursor::KeepAnchor);
            if (c.selectedText() != QString(QChar::ObjectReplacementCharacter)) return -1;
            if (c.charFormat().objectType() != CheckboxTextObject::TypeId) return -1;
            return p;
        };
        int cbPos = checkboxAt(pos);
        if (cbPos < 0) cbPos = checkboxAt(pos - 1);

        if (cbPos >= 0) {
            QTextCursor c(m_document);
            c.setPosition(cbPos);
            c.setPosition(cbPos + 1, QTextCursor::KeepAnchor);
            const QTextCharFormat oldFmt = c.charFormat();
            const bool checked = oldFmt.property(CheckboxTextObject::CheckedProperty).toBool();
            const QString newRaw = checked ? QStringLiteral("[ ]") : QStringLiteral("[x]");

            // Update the format in-place via setCharFormat (no remove+insert).
            // Block document signals so the change doesn't propagate through
            // textChanged → SceneCoordinator → Editor::ensureFocusedCursorVisible,
            // which would auto-scroll to the cursor position.
            QTextCharFormat newFmt;
            newFmt.setObjectType(CheckboxTextObject::TypeId);
            newFmt.setProperty(CheckboxTextObject::CheckedProperty, !checked);
            newFmt.setProperty(MathTextObject::RawProperty, newRaw);

            const bool blocked = m_document->blockSignals(true);
            c.setCharFormat(newFmt);
            m_document->blockSignals(blocked);
            update(); // repaint with updated glyph
            event->accept();
            return;
        }
    }

    // Flag for updateReveal: reveal only on mouse clicks, not arrow keys.
    m_mouseTriggered = true;
    m_control->processEvent(event);
    m_mouseTriggered = false;
    event->accept(); // Accept all buttons to hold grab for middle-click paste
}

void MarkdownTextItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    m_control->processEvent(event);
}

void MarkdownTextItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    m_control->processEvent(event);
}

void MarkdownTextItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    m_control->processEvent(event);
}

void MarkdownTextItem::keyPressEvent(QKeyEvent *event)
{
    // Check for cursor-at-boundary before forwarding
    QTextCursor cursor = m_control->textCursor();
    bool atStart = cursor.atStart();
    bool atEnd = cursor.atEnd();

    m_control->processEvent(event);

    // CJK full-width bracket autocorrect (Obsidian compat).
    // Longest match first: ！【【 before 【【.
    if (!event->text().isEmpty()) {
        QTextCursor c = m_control->textCursor();
        int pos = c.position();
        if (pos >= 3) {
            c.setPosition(pos - 3);
            c.setPosition(pos, QTextCursor::KeepAnchor);
            if (c.selectedText() == QStringLiteral("\uff01\u3010\u3010")) {
                c.beginEditBlock();
                c.removeSelectedText();
                c.insertText(QStringLiteral("![["));
                c.endEditBlock();
                goto cjk_done;
            }
        }
        if (pos >= 2) {
            c = m_control->textCursor();
            c.setPosition(pos - 2);
            c.setPosition(pos, QTextCursor::KeepAnchor);
            QString sel = c.selectedText();
            if (sel == QStringLiteral("\u3010\u3010")) {
                c.beginEditBlock();
                c.removeSelectedText();
                c.insertText(QStringLiteral("[["));
                c.endEditBlock();
            } else if (sel == QStringLiteral("\u3011\u3011")) {
                c.beginEditBlock();
                c.removeSelectedText();
                c.insertText(QStringLiteral("]]"));
                c.endEditBlock();
            }
        }
    }
    cjk_done:

    // If cursor didn't move for arrow keys, we're at a boundary
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Home) {
        if (atStart && m_control->textCursor().atStart())
            emit cursorAtBoundary(Qt::TopEdge);
    } else if (event->key() == Qt::Key_Down || event->key() == Qt::Key_End) {
        if (atEnd && m_control->textCursor().atEnd())
            emit cursorAtBoundary(Qt::BottomEdge);
    }
}

void MarkdownTextItem::inputMethodEvent(QInputMethodEvent *event)
{
    m_control->processEvent(event);
}

QVariant MarkdownTextItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return m_control->inputMethodQuery(query, QVariant());
}

void MarkdownTextItem::focusInEvent(QFocusEvent *event)
{
    m_control->setFocus(true, event->reason());
    QGraphicsObject::focusInEvent(event);
}

void MarkdownTextItem::focusOutEvent(QFocusEvent *event)
{
    m_control->setFocus(false, event->reason());
    QGraphicsObject::focusOutEvent(event);
}

void MarkdownTextItem::onCursorPositionChanged()
{
    if (m_snappingCursor)
        return;
    if (m_inSubstitution)
        return;
    if (m_inCursorUpdate)
        return;
    m_inCursorUpdate = true;

    // 1. Snap cursor past hidden delimiters
    snapCursorPastDelimiters();

    // 2. Math cursor reveal: expand the U+FFFC under the cursor to its raw
    //    source, or collapse a previously-revealed region when the cursor
    //    leaves it.
    updateReveal();

    // 3. Notify highlighter of cursor position (shows/hides delimiters)
    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (hl) {
        QTextCursor cursor = m_control->textCursor();
        hl->setCursorPosition(cursor.block().blockNumber(),
                              cursor.positionInBlock());
    }

    m_inCursorUpdate = false;
}

void MarkdownTextItem::updateReveal()
{
    if (m_inSubstitution) return;

    QTextCursor cursor = m_control->textCursor();
    const int pos = cursor.position();

    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (!hl) return;

    // Case 1: we have a revealed region and the cursor is still strictly
    // inside it. Leave it alone so the user can continue editing. Strict
    // bounds: position AT the boundary means the cursor has moved just
    // outside the `$...$` delimiters, so we collapse.
    if (m_revealedStart >= 0 && pos > m_revealedStart && pos < m_revealedEnd)
        return;

    // Case 2: we have a revealed region but the cursor has left it. Collapse
    // it back to a U+FFFC glyph. Do this before checking for a new reveal,
    // since expanding shifts subsequent offsets.
    if (m_revealedStart >= 0) {
        const int start = m_revealedStart;
        const int end = m_revealedEnd;
        m_revealedStart = -1;
        m_revealedEnd = -1;

        if (end > start && end <= m_document->characterCount()) {
            QTextCursor c(m_document);
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            const QString slice = c.selectedText();

            // Only re-substitute if the slice still looks like $...$ — the
            // user may have deleted the delimiters while editing, in which
            // case we leave it as plain text and let the next reparse sort
            // it out.
            bool display = false;
            QString latex;
            if (slice.size() >= 4 && slice.startsWith(QStringLiteral("$$"))
                                  && slice.endsWith(QStringLiteral("$$"))) {
                display = true;
                latex = slice.mid(2, slice.size() - 4);
            } else if (slice.size() >= 2 && slice.startsWith(QLatin1Char('$'))
                                         && slice.endsWith(QLatin1Char('$'))) {
                display = false;
                latex = slice.mid(1, slice.size() - 2);
            }

            if (!latex.isEmpty()) {
                QTextCharFormat fmt;
                fmt.setObjectType(MathTextObject::TypeId);
                fmt.setProperty(MathTextObject::SourceProperty, latex);
                fmt.setProperty(MathTextObject::DisplayProperty, display);
                const QString delim = display ? QStringLiteral("$$") : QStringLiteral("$");
                fmt.setProperty(MathTextObject::RawProperty, delim + latex + delim);

                m_inSubstitution = true;
                c.beginEditBlock();
                c.removeSelectedText();
                c.insertText(QString(QChar::ObjectReplacementCharacter), fmt);
                c.endEditBlock();
                m_inSubstitution = false;

                // Refresh cursor, since offsets shifted.
                cursor = m_control->textCursor();
            }
        }
    }

    // Case 3: expand a glyph — ONLY on mouse clicks. Arrow keys just
    // step past the 1-char U+FFFC naturally; the user clicks to reveal.
    // This avoids all the arrow-key edge cases (stale span offsets,
    // cursor position jumps, collapse→re-expand loops).
    if (!m_mouseTriggered)
        return;

    auto glyphAt = [&](int p) -> QTextCharFormat {
        if (p < 0 || p >= m_document->characterCount() - 1)
            return {};
        QTextCursor c(m_document);
        c.setPosition(p);
        c.setPosition(p + 1, QTextCursor::KeepAnchor);
        if (c.selectedText() != QString(QChar::ObjectReplacementCharacter))
            return {};
        const QTextCharFormat fmt = c.charFormat();
        if (fmt.objectType() != MathTextObject::TypeId)
            return {};
        return fmt;
    };

    int glyphPos = -1;
    QTextCharFormat glyphFmt;
    {
        QTextCharFormat f = glyphAt(pos);
        if (f.isValid() && f.objectType() == MathTextObject::TypeId) {
            glyphPos = pos;
            glyphFmt = f;
        } else {
            f = glyphAt(pos - 1);
            if (f.isValid() && f.objectType() == MathTextObject::TypeId) {
                glyphPos = pos - 1;
                glyphFmt = f;
            }
        }
    }
    if (glyphPos < 0) return;

    const QString raw = glyphFmt.property(MathTextObject::RawProperty).toString();
    if (raw.isEmpty()) return;

    // Replace the U+FFFC with the raw source.
    m_inSubstitution = true;
    QTextCursor c(m_document);
    c.setPosition(glyphPos);
    c.setPosition(glyphPos + 1, QTextCursor::KeepAnchor);
    QTextCharFormat plain;
    c.setCharFormat(plain);
    c.beginEditBlock();
    c.removeSelectedText();
    c.insertText(raw);
    c.endEditBlock();
    m_inSubstitution = false;

    m_revealedStart = glyphPos;
    m_revealedEnd = glyphPos + raw.size();
    m_revealedIsDisplay = raw.startsWith(QStringLiteral("$$"));

    // Position the cursor right after the opening delimiter so the user
    // lands inside the LaTeX content, ready to edit.
    const int delim = m_revealedIsDisplay ? 2 : 1;
    QTextCursor newCursor(m_document);
    newCursor.setPosition(glyphPos + delim);
    m_control->setTextCursor(newCursor);
}

void MarkdownTextItem::snapCursorPastDelimiters()
{
    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (!hl) return;

    // If the document has any inline math glyphs substituted in, the
    // highlighter's span offsets reference SOURCE-form positions and no
    // longer match the document's character positions. Snapping based on
    // those stale offsets would jump the cursor to arbitrary places.
    // Math reveal handles cursor positioning around glyphs anyway, so
    // disabling snap in this case is safe.
    if (m_document->toPlainText().contains(QChar::ObjectReplacementCharacter))
        return;

    QTextCursor cursor = m_control->textCursor();
    if (cursor.hasSelection())
        return; // don't snap during selection

    int pos = cursor.position();

    for (const SourceSpan &span : hl->spans()) {
        if (!span.isDelimiter)
            continue;
        int spanStart = span.charOffset;
        int spanEnd = span.charOffset + span.charLength;
        if (pos >= spanStart && pos < spanEnd) {
            // Cursor is inside a hidden delimiter — snap to end
            m_snappingCursor = true;
            cursor.setPosition(spanEnd);
            m_control->setTextCursor(cursor);
            m_snappingCursor = false;
            return;
        }
    }
}

void MarkdownTextItem::detectDecoratedRanges()
{
    m_decoratedRanges.clear();

    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (!hl || hl->spans().isEmpty())
        return;

    const int blockCount = m_document->blockCount();

    // Per-block flags derived from the span map (single source of truth)
    struct BlockInfo {
        bool isCodeFence = false;
        bool isCodeContent = false;
        bool isHR = false;
        bool isBlockquote = false;
        bool isHeading = false;
    };
    QList<BlockInfo> bi(blockCount);

    for (const SourceSpan &s : hl->spans()) {
        if (s.charLength <= 0) continue;
        QTextBlock b = m_document->findBlock(s.charOffset);
        while (b.isValid() && b.position() < s.charOffset + s.charLength) {
            const int bn = b.blockNumber();
            if (bn >= blockCount) break;
            if (s.isCodeBlockFence)   bi[bn].isCodeFence = true;
            if (s.isCodeBlockContent) bi[bn].isCodeContent = true;
            if (s.isHorizontalRule)   bi[bn].isHR = true;
            if (s.isBlockquote)       bi[bn].isBlockquote = true;
            if (s.isHeading)          bi[bn].isHeading = true;
            b = b.next();
        }
    }

    // 1. Fenced code blocks: opening fence → content → closing fence.
    //    Span-based detection handles ~~~ fences and edge cases the old
    //    regex missed. Interior empty lines are included by range.
    {
        int bn = 0;
        while (bn < blockCount) {
            if (bi[bn].isCodeFence) {
                int first = bn;
                ++bn;
                while (bn < blockCount && !bi[bn].isCodeFence) ++bn;
                int last = (bn < blockCount) ? bn : blockCount - 1;

                DecoratedRange dr;
                dr.type = DecoratedRange::CodeBlock;
                dr.firstBlock = first;
                dr.lastBlock = last;
                QTextBlock fb = m_document->findBlockByNumber(first);
                if (fb.isValid()) {
                    QString text = fb.text().trimmed();
                    // Extract language after ``` or ~~~
                    if (text.startsWith(QStringLiteral("```")))
                        dr.language = text.mid(3).trimmed();
                    else if (text.startsWith(QStringLiteral("~~~")))
                        dr.language = text.mid(3).trimmed();
                }
                m_decoratedRanges.append(dr);
                ++bn;
            } else {
                ++bn;
            }
        }
    }

    // 2. Callouts: blockquote lines where the first line matches [!type].
    //    Still regex-based — tree-sitter doesn't parse Obsidian callouts.
    {
        static const QRegularExpression calloutRe(
            QStringLiteral(R"(^>\s*\[!(\w+)\]([+-])?\s*(.*)?$)"));

        QTextBlock block = m_document->begin();
        while (block.isValid()) {
            const int bn = block.blockNumber();
            bool skip = false;
            for (const auto &dr : m_decoratedRanges) {
                if (bn >= dr.firstBlock && bn <= dr.lastBlock) { skip = true; break; }
            }
            if (skip || !bi[bn].isBlockquote) { block = block.next(); continue; }

            auto match = calloutRe.match(block.text());
            if (match.hasMatch()) {
                QString type = match.captured(1).toLower();
                QString title = match.captured(3).trimmed();

                QTextBlock bodyBlock = block.next();
                int lastBlockNum = bn;
                while (bodyBlock.isValid() && bodyBlock.text().startsWith(QLatin1Char('>'))) {
                    lastBlockNum = bodyBlock.blockNumber();
                    bodyBlock = bodyBlock.next();
                }

                DecoratedRange dr;
                dr.type = DecoratedRange::Callout;
                dr.firstBlock = bn;
                dr.lastBlock = lastBlockNum;
                dr.calloutType = type;
                dr.calloutTitle = title.isEmpty()
                    ? type.at(0).toUpper() + type.mid(1) : title;
                dr.calloutColor = DecoratedRange::colorForCalloutType(type);
                m_decoratedRanges.append(dr);

                block = bodyBlock;
                continue;
            }
            block = block.next();
        }
    }

    auto inExistingRange = [this](int bn) {
        for (const auto &dr : m_decoratedRanges) {
            if (bn >= dr.firstBlock && bn <= dr.lastBlock) return true;
        }
        return false;
    };

    // 3. Horizontal rules: span-flagged blocks not in other ranges
    for (int bn = 0; bn < blockCount; ++bn) {
        if (bi[bn].isHR && !inExistingRange(bn)) {
            DecoratedRange dr;
            dr.type = DecoratedRange::HorizontalRule;
            dr.firstBlock = bn;
            dr.lastBlock = bn;
            m_decoratedRanges.append(dr);
        }
    }

    // 4. Blockquotes: consecutive span-flagged blocks not claimed above
    {
        int bn = 0;
        while (bn < blockCount) {
            if (bi[bn].isBlockquote && !inExistingRange(bn)) {
                int first = bn;
                while (bn < blockCount && bi[bn].isBlockquote && !inExistingRange(bn))
                    ++bn;
                DecoratedRange dr;
                dr.type = DecoratedRange::Blockquote;
                dr.firstBlock = first;
                dr.lastBlock = bn - 1;
                m_decoratedRanges.append(dr);
            } else {
                ++bn;
            }
        }
    }

}

void MarkdownTextItem::applyBlockFormats()
{
    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());
    if (!hl) return;

    const int blockCount = m_document->blockCount();

    // Per-block info from span map
    struct BlockInfo { bool hasListMarker = false; bool isHeading = false; int bqDepth = 0; };
    QList<BlockInfo> bi(blockCount);
    for (const SourceSpan &s : hl->spans()) {
        if (s.charLength <= 0) continue;
        const int bn = m_document->findBlock(s.charOffset).blockNumber();
        if (bn < 0 || bn >= blockCount) continue;
        if (s.isListMarker) bi[bn].hasListMarker = true;
        if (s.isHeading)    bi[bn].isHeading = true;
        if (s.blockquoteDepth > bi[bn].bqDepth) bi[bn].bqDepth = s.blockquoteDepth;
    }

    const qreal indentStep = 20.0;  // px per nesting level

    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        const int bn = block.blockNumber();
        const QString text = block.text();

        // Count leading whitespace to determine nesting level
        int spaces = 0;
        for (int i = 0; i < text.size(); ++i) {
            if (text[i] == QLatin1Char(' ')) ++spaces;
            else if (text[i] == QLatin1Char('\t')) spaces += 4;
            else break;
        }

        qreal leftMargin = 0;

        // List indentation: each 2 spaces of leading whitespace = 1 indent level
        if (bi[bn].hasListMarker && spaces > 0)
            leftMargin = (spaces / 2) * indentStep;

        // Blockquote indentation: add margin per depth level (additive with
        // list indent, so nested lists inside blockquotes work)
        if (bi[bn].bqDepth > 0)
            leftMargin += bi[bn].bqDepth * indentStep;

        QTextBlockFormat fmt = block.blockFormat();
        if (!qFuzzyCompare(fmt.leftMargin(), leftMargin)) {
            fmt.setLeftMargin(leftMargin);
            cursor.setPosition(block.position());
            cursor.setBlockFormat(fmt);
        }
    }
    cursor.endEditBlock();
}

void MarkdownTextItem::paintDecoratedRanges(QPainter *painter)
{
    if (m_decoratedRanges.isEmpty())
        return;

    QAbstractTextDocumentLayout *layout = m_document->documentLayout();
    qreal margin = m_document->documentMargin();

    // Get theme colors from the highlighter
    auto *hl = qobject_cast<MarkdownHighlighter *>(
        m_document->findChild<QSyntaxHighlighter *>());

    for (const DecoratedRange &dr : m_decoratedRanges) {
        QTextBlock firstBlock = m_document->findBlockByNumber(dr.firstBlock);
        if (!firstBlock.isValid()) continue;

        QRectF firstBR = layout->blockBoundingRect(firstBlock);
        qreal rangeTop = firstBR.top();
        qreal rangeHeight = 0;
        QTextBlock b = firstBlock;
        for (int i = dr.firstBlock; i <= dr.lastBlock && b.isValid(); ++i, b = b.next())
            rangeHeight += layout->blockBoundingRect(b).height();

        QRectF bgRect(margin - 4, rangeTop, m_width - margin * 2 + 8, rangeHeight);

        if (dr.type == DecoratedRange::CodeBlock) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0xf5, 0xf5, 0xf5));
            painter->setRenderHint(QPainter::Antialiasing);
            painter->drawRoundedRect(bgRect, 4, 4);
            painter->setPen(QPen(QColor(0xe0, 0xe0, 0xe0), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(bgRect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            if (!dr.language.isEmpty()) {
                QFont labelFont = painter->font();
                labelFont.setPointSize(qMax(8, labelFont.pointSize() - 2));
                painter->setFont(labelFont);
                painter->setPen(QColor(0x9e, 0x9e, 0x9e));
                QRectF labelRect(bgRect.right() - 80, bgRect.top() + 2, 72, 16);
                painter->drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter,
                                  dr.language);
            }
        } else if (dr.type == DecoratedRange::Callout) {
            QColor bg = dr.calloutColor;
            bg.setAlpha(20);
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->drawRoundedRect(bgRect, 4, 4);
            painter->setBrush(dr.calloutColor);
            painter->drawRoundedRect(
                QRectF(bgRect.left(), bgRect.top(), 4, bgRect.height()), 2, 2);

        } else if (dr.type == DecoratedRange::HorizontalRule) {
            QColor lineColor = hl ? hl->horizontalRuleColor() : QColor(0xb4, 0xb4, 0xb4);
            qreal centerY = rangeTop + rangeHeight / 2;
            painter->setPen(QPen(lineColor, 1.5));
            painter->drawLine(QPointF(margin, centerY),
                              QPointF(m_width - margin, centerY));

        } else if (dr.type == DecoratedRange::Blockquote) {
            QColor accentColor = hl ? hl->blockquoteColor() : QColor(111, 159, 0);
            painter->setPen(Qt::NoPen);
            painter->setRenderHint(QPainter::Antialiasing);
            // Per-block depth-aware accent bars
            b = firstBlock;
            for (int i = dr.firstBlock; i <= dr.lastBlock && b.isValid(); ++i, b = b.next()) {
                QRectF blockBR = layout->blockBoundingRect(b);
                // Count > markers to determine depth
                const QString text = b.text();
                int depth = 0;
                for (int j = 0; j < text.size(); ++j) {
                    if (text[j] == QLatin1Char('>')) ++depth;
                    else if (text[j] != QLatin1Char(' ')) break;
                }
                for (int d = 0; d < depth; ++d) {
                    qreal barX = margin - 4 + d * 16;
                    painter->setBrush(accentColor);
                    painter->drawRoundedRect(
                        QRectF(barX, blockBR.top(), 4, blockBR.height()), 2, 2);
                }
            }

        }
    }
}

void MarkdownTextItem::updateGeometry()
{
    prepareGeometryChange();
}

} // namespace Markoff
