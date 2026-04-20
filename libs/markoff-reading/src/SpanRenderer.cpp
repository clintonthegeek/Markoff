// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.
//
// Pattern cross-reference: libs/markoff/src/MarkdownTextItem.cpp's use of
// `cursor.insertText(text, charFormat)` with QTextCharFormat-bearing
// formats is what this file mirrors — with CharacterStyle lookup sitting
// between the span detection and the format construction.

#include "SpanRenderer.h"

#include "corbomite/readingview/styling/CharacterStyle.h"
#include "corbomite/readingview/styling/StyleManager.h"

#include <QChar>
#include <QTextCharFormat>
#include <QTextCursor>

namespace Corbomite::ReadingView {

namespace {

/// Apply a resolved CharacterStyle (StyleManager::resolvedCharacterStyle)
/// onto a base QTextCharFormat. Unlike CharacterStyle::applyFormat which
/// merges by setProperty, we want each span's base format to inherit the
/// paragraph's font family/size + foreground unless the character style
/// overrides.
QTextCharFormat mergeCharStyle(const QTextCharFormat &base,
                               const CharacterStyle &cs)
{
    QTextCharFormat out = base;
    cs.applyFormat(out);
    return out;
}

/// Look up a character style by name. Returns a fully-resolved CharacterStyle
/// (parent chain walked). Falls back to an empty style if absent.
CharacterStyle resolveChar(StyleManager &styles, const QString &name)
{
    auto *cs = styles.characterStyle(name);
    if (!cs) return CharacterStyle(name);
    return styles.resolvedCharacterStyle(name);
}

} // namespace

bool SpanRenderer::renderInline(QTextCursor &cursor,
                                const QString &text,
                                const QTextCharFormat &baseFormat,
                                StyleManager &styles) const
{
    const int n = text.size();
    int i = 0;
    bool producedObjectRuns = false;

    // Cache resolved styles once.
    const CharacterStyle emph       = resolveChar(styles, QStringLiteral("Emphasis"));
    const CharacterStyle strong     = resolveChar(styles, QStringLiteral("Strong"));
    const CharacterStyle code       = resolveChar(styles, QStringLiteral("InlineCode"));
    const CharacterStyle link       = resolveChar(styles, QStringLiteral("Link"));
    const CharacterStyle wikiLink   = resolveChar(styles, QStringLiteral("WikiLink"));
    const CharacterStyle strike     = resolveChar(styles, QStringLiteral("Strikethrough"));
    const CharacterStyle highlight  = resolveChar(styles, QStringLiteral("Highlight"));

    auto emitPlain = [&](const QString &s) {
        if (s.isEmpty()) return;
        cursor.insertText(s, baseFormat);
    };

    while (i < n) {
        const QChar c = text.at(i);

        // Inline code: `...`
        if (c == QLatin1Char('`')) {
            int closeAt = text.indexOf(QLatin1Char('`'), i + 1);
            if (closeAt > i) {
                const QString inner = text.mid(i + 1, closeAt - i - 1);
                cursor.insertText(inner, mergeCharStyle(baseFormat, code));
                i = closeAt + 1;
                continue;
            }
        }

        // Display math: $$...$$
        if (c == QLatin1Char('$') && i + 1 < n
            && text.at(i + 1) == QLatin1Char('$')) {
            int closeAt = text.indexOf(QStringLiteral("$$"), i + 2);
            if (closeAt > i) {
                const QString latex = text.mid(i + 2, closeAt - i - 2);
                // Emit a replacement char carrying the math source. Display
                // math usually stands alone as its own block but we honour
                // the span-level form here too. SectionLayout decides the
                // presentation.
                QTextCharFormat mf = baseFormat;
                mf.setObjectType(MathObjectMarker);
                mf.setProperty(InlineMathSourceProperty, latex);
                mf.setProperty(SpanRenderer::WikiLinkTargetProperty, QVariant()); // clear
                mf.setProperty(QTextFormat::UserProperty, true); // display mode
                cursor.insertText(QString(QChar::ObjectReplacementCharacter), mf);
                producedObjectRuns = true;
                i = closeAt + 2;
                continue;
            }
        }

        // Inline math: $...$ (single-dollar, no embedded $)
        if (c == QLatin1Char('$')) {
            // Find closing $ on the same run, rejecting empty.
            int j = i + 1;
            while (j < n && text.at(j) != QLatin1Char('$')
                         && text.at(j) != QLatin1Char('\n')) {
                ++j;
            }
            if (j < n && text.at(j) == QLatin1Char('$') && j > i + 1) {
                const QString latex = text.mid(i + 1, j - i - 1);
                QTextCharFormat mf = baseFormat;
                mf.setObjectType(MathObjectMarker);
                mf.setProperty(InlineMathSourceProperty, latex);
                mf.setProperty(QTextFormat::UserProperty, false); // inline
                cursor.insertText(QString(QChar::ObjectReplacementCharacter), mf);
                producedObjectRuns = true;
                i = j + 1;
                continue;
            }
        }

        // Image embed: ![alt](path)
        if (c == QLatin1Char('!') && i + 1 < n
            && text.at(i + 1) == QLatin1Char('[')) {
            int closeBr = text.indexOf(QLatin1Char(']'), i + 2);
            if (closeBr > i && closeBr + 1 < n
                && text.at(closeBr + 1) == QLatin1Char('(')) {
                int closeParen = text.indexOf(QLatin1Char(')'), closeBr + 2);
                if (closeParen > closeBr) {
                    const QString alt  = text.mid(i + 2, closeBr - i - 2);
                    const QString path = text.mid(closeBr + 2,
                                                  closeParen - closeBr - 2);
                    QTextCharFormat imf = baseFormat;
                    imf.setObjectType(ImageObjectMarker);
                    imf.setProperty(InlineImagePathProperty, path);
                    imf.setProperty(InlineImageAltProperty, alt);
                    cursor.insertText(QString(QChar::ObjectReplacementCharacter),
                                       imf);
                    producedObjectRuns = true;
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        // Wiki-link: [[target|display]] or [[target]]
        if (c == QLatin1Char('[') && i + 1 < n
            && text.at(i + 1) == QLatin1Char('[')) {
            int closeAt = text.indexOf(QStringLiteral("]]"), i + 2);
            if (closeAt > i) {
                const QString inner = text.mid(i + 2, closeAt - i - 2);
                const int pipe = inner.indexOf(QLatin1Char('|'));
                QString target = inner;
                QString display = inner;
                if (pipe >= 0) {
                    target = inner.left(pipe);
                    display = inner.mid(pipe + 1);
                }
                QTextCharFormat cf = mergeCharStyle(baseFormat, wikiLink);
                cf.setProperty(WikiLinkTargetProperty, target);
                cf.setAnchor(true);
                cf.setAnchorHref(QStringLiteral("wiki:") + target);
                cursor.insertText(display, cf);
                i = closeAt + 2;
                continue;
            }
        }

        // Standard link: [text](url)
        if (c == QLatin1Char('[')) {
            int closeBr = text.indexOf(QLatin1Char(']'), i + 1);
            if (closeBr > i && closeBr + 1 < n
                && text.at(closeBr + 1) == QLatin1Char('(')) {
                int closeParen = text.indexOf(QLatin1Char(')'), closeBr + 2);
                if (closeParen > closeBr) {
                    const QString linkText =
                        text.mid(i + 1, closeBr - i - 1);
                    const QString url =
                        text.mid(closeBr + 2, closeParen - closeBr - 2);
                    QTextCharFormat cf = mergeCharStyle(baseFormat, link);
                    cf.setAnchor(true);
                    cf.setAnchorHref(url);
                    cursor.insertText(linkText, cf);
                    i = closeParen + 1;
                    continue;
                }
            }
        }

        // Strong: ** or __
        auto tryPaired = [&](const QString &delim,
                             const CharacterStyle &cs) -> bool {
            if (!text.mid(i, delim.size()).startsWith(delim))
                return false;
            int closeAt = text.indexOf(delim, i + delim.size());
            if (closeAt <= i) return false;
            const QString inner =
                text.mid(i + delim.size(), closeAt - i - delim.size());
            // Recurse so nested ** *italic* bold ** works.
            QTextCursor inner_cursor = cursor;
            SpanRenderer sub;
            // Build a merged base for the inner walk.
            QTextCharFormat merged = mergeCharStyle(baseFormat, cs);
            sub.renderInline(cursor, inner, merged, styles);
            i = closeAt + delim.size();
            return true;
        };

        if (tryPaired(QStringLiteral("**"), strong)) continue;
        if (tryPaired(QStringLiteral("__"), strong)) continue;

        // Strikethrough: ~~text~~
        if (c == QLatin1Char('~') && i + 1 < n
            && text.at(i + 1) == QLatin1Char('~')) {
            int closeAt = text.indexOf(QStringLiteral("~~"), i + 2);
            if (closeAt > i) {
                const QString inner = text.mid(i + 2, closeAt - i - 2);
                QTextCharFormat merged = mergeCharStyle(baseFormat, strike);
                SpanRenderer sub;
                sub.renderInline(cursor, inner, merged, styles);
                i = closeAt + 2;
                continue;
            }
        }

        // Highlight: ==text==
        if (c == QLatin1Char('=') && i + 1 < n
            && text.at(i + 1) == QLatin1Char('=')) {
            int closeAt = text.indexOf(QStringLiteral("=="), i + 2);
            if (closeAt > i) {
                const QString inner = text.mid(i + 2, closeAt - i - 2);
                QTextCharFormat merged = mergeCharStyle(baseFormat, highlight);
                SpanRenderer sub;
                sub.renderInline(cursor, inner, merged, styles);
                i = closeAt + 2;
                continue;
            }
        }

        // Emphasis: single * or _
        if (c == QLatin1Char('*') || c == QLatin1Char('_')) {
            const QChar dch = c;
            // Avoid stealing ** / __ starts.
            if (!(i + 1 < n && text.at(i + 1) == dch)) {
                int closeAt = text.indexOf(dch, i + 1);
                // Block multi-line and ** closings.
                if (closeAt > i) {
                    const QString inner = text.mid(i + 1, closeAt - i - 1);
                    QTextCharFormat merged = mergeCharStyle(baseFormat, emph);
                    SpanRenderer sub;
                    sub.renderInline(cursor, inner, merged, styles);
                    i = closeAt + 1;
                    continue;
                }
            }
        }

        // Default: one literal char.
        cursor.insertText(QString(c), baseFormat);
        ++i;
    }

    return producedObjectRuns;
}

} // namespace Corbomite::ReadingView
