// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Cmd/Block.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>

#include "Helpers.h"

namespace Markoff::Cmd {

namespace {
/// Returns the byte offset of the start of the line containing `byte`.
quint32 lineStart(const QByteArray &buf, quint32 byte) {
    if (byte > static_cast<quint32>(buf.size())) byte = buf.size();
    int b = static_cast<int>(byte);
    while (b > 0 && buf.at(b - 1) != '\n') --b;
    return static_cast<quint32>(b);
}
/// Returns the count of leading '#' chars (capped at 6) at offset `start`.
int existingHashes(const QByteArray &buf, quint32 start) {
    int n = 0;
    int b = static_cast<int>(start);
    while (n < 6 && b < buf.size() && buf.at(b) == '#') { ++n; ++b; }
    // A heading marker is `# ` not `#tag`. Require trailing space (or EOL).
    if (n > 0 && b < buf.size() && buf.at(b) != ' ' && buf.at(b) != '\n')
        return 0;
    return n;
}
}

QList<MarkoffEdit> editsForSetHeading(const MarkoffDocument &doc,
                                       const Selection &sel, int level)
{
    const auto [start, end] = Detail::selectionByteRange(doc, sel);
    const QByteArray buf = doc.toMarkdownUtf8();
    const quint32 ls = lineStart(buf, start);
    const int existing = existingHashes(buf, ls);

    // Compute current prefix length (existing hashes + trailing space if any).
    int curPrefixLen = existing;
    if (existing > 0 && ls + existing < static_cast<quint32>(buf.size())
        && buf.at(static_cast<int>(ls + existing)) == ' ')
        ++curPrefixLen;

    QByteArray newPrefix;
    if (level >= 1 && level <= 6) {
        newPrefix = QByteArray(level, '#') + " ";
    }

    if (newPrefix.isEmpty() && curPrefixLen == 0) return {};
    if (newPrefix == buf.mid(static_cast<int>(ls), curPrefixLen)) return {};

    MarkoffEdit r;
    r.oldStart = ls;
    r.oldEnd   = ls + static_cast<quint32>(curPrefixLen);
    r.newText  = newPrefix;
    return { r };
}

CollabText::Crdt::Operation setHeading(MarkoffDocument &d, const Selection &s, int level)
{ return d.applyLocalEdit(editsForSetHeading(d, s, level)); }

QList<MarkoffEdit> editsForToggleCheckbox(const MarkoffDocument &doc,
                                           const CollabText::Crdt::Anchor &a)
{
    const QByteArray buf = doc.toMarkdownUtf8();
    const quint32 byte = doc.resolveAnchor(a);
    const quint32 ls = lineStart(buf, byte);

    // Look for "- [ ] " or "- [x] " or "- " prefix.
    int b = static_cast<int>(ls);
    if (b + 1 >= buf.size()) return {};
    // Step over leading "- " or "* " or "+ " bullet.
    if (buf.at(b) != '-' && buf.at(b) != '*' && buf.at(b) != '+') return {};
    if (b + 1 >= buf.size() || buf.at(b + 1) != ' ') return {};
    int after = b + 2;

    if (after + 3 < buf.size() && buf.at(after) == '[' && buf.at(after + 2) == ']'
        && buf.at(after + 3) == ' ')
    {
        const char inside = buf.at(after + 1);
        MarkoffEdit r; r.oldStart = static_cast<quint32>(after);
        if (inside == ' ') {
            // [ ] -> [x]
            r.oldEnd = static_cast<quint32>(after + 4);
            r.newText = "[x] ";
        } else {
            // [x] -> none (strip "[x] ")
            r.oldEnd = static_cast<quint32>(after + 4);
            r.newText.clear();
        }
        return { r };
    }
    // No checkbox -> add "[ ] "
    MarkoffEdit r;
    r.oldStart = static_cast<quint32>(after);
    r.oldEnd   = static_cast<quint32>(after);
    r.newText  = "[ ] ";
    return { r };
}

CollabText::Crdt::Operation toggleCheckbox(MarkoffDocument &d,
                                            const CollabText::Crdt::Anchor &a)
{ return d.applyLocalEdit(editsForToggleCheckbox(d, a)); }

QList<MarkoffEdit> editsForBlockQuote(const MarkoffDocument &doc, const Selection &sel)
{
    const auto [start, end] = Detail::selectionByteRange(doc, sel);
    const QByteArray buf = doc.toMarkdownUtf8();

    // Collect line starts in [start, end].
    QList<quint32> lineStarts;
    quint32 ls = lineStart(buf, start);
    while (ls < end || (ls == end && lineStarts.isEmpty())) {
        lineStarts << ls;
        int next = static_cast<int>(ls);
        while (next < buf.size() && buf.at(next) != '\n') ++next;
        if (next >= buf.size()) break;
        ls = static_cast<quint32>(next + 1);
    }

    bool allQuoted = !lineStarts.isEmpty();
    for (quint32 l : lineStarts) {
        if (l + 1 >= static_cast<quint32>(buf.size())
            || buf.at(static_cast<int>(l)) != '>'
            || buf.at(static_cast<int>(l) + 1) != ' ')
        { allQuoted = false; break; }
    }

    QList<MarkoffEdit> out;
    out.reserve(lineStarts.size());
    if (allQuoted) {
        for (quint32 l : lineStarts) {
            MarkoffEdit r;
            r.oldStart = l; r.oldEnd = l + 2; r.newText.clear();
            out << r;
        }
    } else {
        for (quint32 l : lineStarts) {
            MarkoffEdit r;
            r.oldStart = l; r.oldEnd = l; r.newText = "> ";
            out << r;
        }
    }
    return out;
}

CollabText::Crdt::Operation blockQuote(MarkoffDocument &d, const Selection &s)
{ return d.applyLocalEdit(editsForBlockQuote(d, s)); }

}  // namespace Markoff::Cmd
