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

// toggleCheckbox / blockQuote — stubs filled in Task 35.
QList<MarkoffEdit> editsForToggleCheckbox(const MarkoffDocument &,
                                           const CollabText::Crdt::Anchor &)
{ return {}; }
CollabText::Crdt::Operation toggleCheckbox(MarkoffDocument &d,
                                            const CollabText::Crdt::Anchor &a)
{ return d.applyLocalEdit(editsForToggleCheckbox(d, a)); }
QList<MarkoffEdit> editsForBlockQuote(const MarkoffDocument &, const Selection &)
{ return {}; }
CollabText::Crdt::Operation blockQuote(MarkoffDocument &d, const Selection &s)
{ return d.applyLocalEdit(editsForBlockQuote(d, s)); }

}  // namespace Markoff::Cmd
