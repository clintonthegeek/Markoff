// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/ReadingPipeline.h"

#include <markoff-parser/Document.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QRegularExpression>

namespace Corbomite::ReadingView {

namespace {

// Detect a fenced-code-block fence at the given position in `text`.
// Returns (fenceChar, fenceLen) if at the start of an opening/closing fence,
// else (0, 0). Matches leading up to 3 spaces and needs the fence char to be
// either ` or ~ repeated >= 3 times.
struct FenceInfo { QChar ch; int length = 0; };

FenceInfo detectFence(const QString &text, int lineStart, int lineEnd)
{
    int i = lineStart;
    int spaces = 0;
    while (i < lineEnd && text.at(i) == QLatin1Char(' ') && spaces < 4) {
        ++i;
        ++spaces;
    }
    if (spaces >= 4) return {};
    if (i >= lineEnd) return {};
    const QChar c = text.at(i);
    if (c != QLatin1Char('`') && c != QLatin1Char('~')) return {};
    int run = 0;
    while (i < lineEnd && text.at(i) == c) {
        ++i;
        ++run;
    }
    if (run < 3) return {};
    return { c, run };
}

} // namespace

ReadingPipeline::ReadingPipeline(QObject *parent)
    : QObject(parent)
{
}

namespace {

// Return the byte-slice of the frontmatter *contents* (between the two
// `---` fences, excluding them) if present, or a sentinel indicating
// absence via the `present` flag. Rules:
//   - the document must start with `---\n` (we do not accept a leading BOM
//     or blank lines, matching markoff-parser's frontmatterSpan).
//   - a closing line of exactly `---` (optionally with trailing whitespace)
//     terminates the block.
//   - if no closing fence is found, frontmatter is considered absent.
struct FrontmatterSlice {
    bool present = false;
    QStringView contents;
};

FrontmatterSlice extractFrontmatter(const QString &md)
{
    FrontmatterSlice out;
    if (!md.startsWith(QStringLiteral("---\n"))) return out;
    const int contentStart = 4; // past "---\n"
    const int n = md.size();
    int i = contentStart;
    while (i < n) {
        int lineStart = i;
        int lineEnd = md.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0) lineEnd = n;
        // Close fence: line is exactly "---" (trailing whitespace allowed).
        int p = lineStart;
        int dashes = 0;
        while (p < lineEnd && md.at(p) == QLatin1Char('-')) { ++p; ++dashes; }
        if (dashes == 3) {
            // allow trailing whitespace before newline
            bool rest_ws = true;
            for (int q = p; q < lineEnd; ++q) {
                if (!md.at(q).isSpace()) { rest_ws = false; break; }
            }
            if (rest_ws) {
                out.present = true;
                out.contents = QStringView(md).sliced(
                    contentStart, lineStart - contentStart);
                return out;
            }
        }
        i = (lineEnd < n) ? lineEnd + 1 : lineEnd;
    }
    return out; // unterminated → treat as absent
}

} // namespace

bool ReadingPipeline::detectFrontmatterChange(const QString &oldMarkdown,
                                              const QString &newMarkdown)
{
    const FrontmatterSlice a = extractFrontmatter(oldMarkdown);
    const FrontmatterSlice b = extractFrontmatter(newMarkdown);
    if (a.present != b.present) return true;
    if (!a.present && !b.present) return false;
    return a.contents != b.contents;
}

namespace {

// Return a cumulative-newline-count table: `lineOffsets[k]` is the number
// of '\n' chars in `markdown[0 .. k)`. Built in one linear pass so
// per-section `sourceLine` lookup is O(1). Used by Phase 6 so the
// post-split loop that stamps `ReadingSection::sourceLine` remains O(n)
// overall (a naive per-section newline count is O(n²)).
QVector<int> buildLineOffsetTable(const QString &markdown)
{
    const int n = markdown.size();
    QVector<int> table;
    table.resize(n + 1);
    int count = 0;
    for (int j = 0; j < n; ++j) {
        table[j] = count;
        if (markdown.at(j) == QLatin1Char('\n')) ++count;
    }
    table[n] = count;
    return table;
}

int lineOfOffset(const QVector<int> &table, int offset)
{
    if (offset <= 0) return 0;
    const int clamped = qMin(offset, static_cast<int>(table.size() - 1));
    return table.at(clamped);
}

} // namespace

QVector<std::shared_ptr<ReadingSection>>
ReadingPipeline::splitIntoSections(const QString &markdown)
{
    QVector<std::shared_ptr<ReadingSection>> sections;
    if (markdown.isEmpty())
        return sections;

    // Parse via markoff-parser so we stay faithful to its frontmatter-span
    // rules (it eats the full `---\n…\n---\n` including trailing newline).
    auto doc = Markoff::Document::fromMarkdown(markdown);

    int bodyStart = 0;
    if (auto fmSpan = doc->frontmatterSpan()) {
        // Frontmatter occupies [fmSpan->first, fmSpan->second) in char-space
        // since frontmatter is ASCII-delimited by '---' and YAML (byte =
        // char for the delimiters). The parser returns char offsets per
        // Document::fromMarkdown where the span is computed pre-UTF-8.
        auto fm = std::make_shared<ReadingSection>();
        fm->setSourceRange({ fmSpan->first, fmSpan->second });
        fm->setHeadingLevel(0);
        fm->setIsFrontMatterSection(true);
        fm->setUsesFrontMatter(false); // the source — not a consumer
        sections.push_back(fm);
        bodyStart = fmSpan->second;
    }

    // Find ATX-heading-line starts in the body. A heading is: optional up to
    // 3 leading spaces, 1..6 '#' chars, at least one space/tab, text. We
    // skip lines that fall inside fenced code blocks to avoid mis-treating
    // `# in a code sample` as a heading.
    struct Heading { int lineStart; int level; };
    QVector<Heading> headings;

    int i = bodyStart;
    const int n = markdown.size();
    bool inFence = false;
    QChar fenceChar;
    int fenceLen = 0;

    while (i < n) {
        int lineStart = i;
        int lineEnd = markdown.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0) lineEnd = n;

        if (inFence) {
            // Look for closing fence of same char and length >=
            FenceInfo f = detectFence(markdown, lineStart, lineEnd);
            if (f.length > 0 && f.ch == fenceChar && f.length >= fenceLen) {
                inFence = false;
                fenceLen = 0;
            }
        } else {
            FenceInfo f = detectFence(markdown, lineStart, lineEnd);
            if (f.length > 0) {
                inFence = true;
                fenceChar = f.ch;
                fenceLen = f.length;
            } else {
                // Check for ATX heading
                int p = lineStart;
                int spaces = 0;
                while (p < lineEnd && markdown.at(p) == QLatin1Char(' ')
                       && spaces < 4) {
                    ++p; ++spaces;
                }
                if (spaces < 4 && p < lineEnd
                    && markdown.at(p) == QLatin1Char('#')) {
                    int hashes = 0;
                    while (p < lineEnd && markdown.at(p) == QLatin1Char('#')
                           && hashes < 7) {
                        ++p; ++hashes;
                    }
                    if (hashes >= 1 && hashes <= 6
                        && (p >= lineEnd
                            || markdown.at(p) == QLatin1Char(' ')
                            || markdown.at(p) == QLatin1Char('\t'))) {
                        headings.push_back({ lineStart, hashes });
                    }
                }
            }
        }

        i = (lineEnd < n) ? lineEnd + 1 : lineEnd;
    }

    if (headings.isEmpty()) {
        // Single body section if there's any content.
        if (bodyStart < n) {
            auto s = std::make_shared<ReadingSection>();
            s->setSourceRange({ bodyStart, n });
            s->setHeadingLevel(0);
            sections.push_back(s);
        }
        return sections;
    }

    // Pre-heading body (content between frontmatter and first heading).
    if (bodyStart < headings.first().lineStart) {
        // Only emit if non-whitespace content exists
        bool hasContent = false;
        for (int j = bodyStart; j < headings.first().lineStart; ++j) {
            if (!markdown.at(j).isSpace()) { hasContent = true; break; }
        }
        if (hasContent) {
            auto s = std::make_shared<ReadingSection>();
            s->setSourceRange({ bodyStart, headings.first().lineStart });
            s->setHeadingLevel(0);
            sections.push_back(s);
        }
    }

    // One section per heading, extending to the next heading of equal or
    // shallower level. (Deeper headings live inside the outer section per
    // the plan: "A section extends until the next heading at the same or
    // shallower level".)
    for (int h = 0; h < headings.size(); ++h) {
        const Heading &cur = headings.at(h);
        int endOffset = n;
        for (int k = h + 1; k < headings.size(); ++k) {
            if (headings.at(k).level <= cur.level) {
                endOffset = headings.at(k).lineStart;
                break;
            }
        }
        auto s = std::make_shared<ReadingSection>();
        s->setSourceRange({ cur.lineStart, endOffset });
        s->setHeadingLevel(cur.level);
        sections.push_back(s);
    }

    // Pre-layout recycle key: SHA-256 of a *normalized* form of the
    // section's source bytes plus a discriminator for section type
    // (heading level / frontmatter). The normalization trims trailing
    // whitespace-only lines so a section keeps the same key when the
    // blank padding between it and the next heading varies (e.g. from
    // reordering headings). SectionLayout leaves this alone post-layout.
    for (auto &sec : sections) {
        const auto r = sec->sourceRange();
        QString slice = markdown.mid(r.from, r.to - r.from);
        // Trim trailing whitespace (including newlines) — the visual
        // shape of a section is independent of blank-line padding after
        // its last non-whitespace byte.
        int end = slice.size();
        while (end > 0 && slice.at(end - 1).isSpace()) --end;
        const QByteArray normalized = slice.left(end).toUtf8();
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(normalized);
        const char tag[2] = {
            static_cast<char>(sec->isFrontMatterSection() ? 'F' : 'S'),
            static_cast<char>('0' + (sec->headingLevel() & 0xF)) };
        h.addData(QByteArray(tag, 2));
        sec->setRenderedShape(h.result());
    }

    // Phase 6: populate `sourceLine` + `estimatedHeight`. Estimated
    // height is a crude line-count heuristic — `lines * 1.4 * lineSpacing`
    // with a nominal 20 px line spacing. Suffices for initial scene-rect
    // sizing; actual heights overwrite post-layout.
    //
    // Build a cumulative-newline lookup table once so per-section queries
    // are O(1) — with 1000+ sections a per-section linear scan is O(n²)
    // and dominates large-note open time.
    const QVector<int> lineTable = buildLineOffsetTable(markdown);
    for (auto &sec : sections) {
        const auto r = sec->sourceRange();
        sec->setSourceLine(lineOfOffset(lineTable, r.from));
        int lineCount = 0;
        if (r.to > r.from) {
            const int fromLine = lineOfOffset(lineTable, r.from);
            const int toLine = lineOfOffset(lineTable, r.to);
            lineCount = toLine - fromLine;
            if (lineCount == 0) lineCount = 1; // no newline: still one line
        }
        const qreal estimate = qMax<qreal>(lineCount * 1.4 * 20.0, 24.0);
        sec->setEstimatedHeight(estimate);
    }

    // usesFrontMatter detection — scan each non-frontmatter section for
    // Obsidian-style template tokens that read from frontmatter. Any of
    // `{{title}}`, `{{date}}`, or `{{property:...}}` triggers the flag.
    // Phase 4 uses this as the re-render trigger on frontmatter edits.
    static const QRegularExpression tokenRe(
        QStringLiteral(R"(\{\{(?:title|date|property:[^}]+)\}\})"));
    for (auto &sec : sections) {
        if (sec->isFrontMatterSection()) continue;
        const auto r = sec->sourceRange();
        if (r.to <= r.from) continue;
        const QString slice = markdown.mid(r.from, r.to - r.from);
        if (tokenRe.match(slice).hasMatch())
            sec->setUsesFrontMatter(true);
    }

    return sections;
}

} // namespace Corbomite::ReadingView
