// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Cmd/InlineFormat.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>

#include "Helpers.h"

namespace Markoff::Cmd {

namespace {

/// Common toggler. `delim` is the markdown delimiter (e.g. "**", "*", "~~", "`").
QList<MarkoffEdit> toggleDelim(const MarkoffDocument &doc, const Selection &sel,
                                const QByteArray &delim)
{
    if (sel.isEmpty()) return {};
    const auto [start, end] = Detail::selectionByteRange(doc, sel);
    if (start == end) return {};

    const QByteArray buf = doc.toMarkdownUtf8();
    const int dlen = delim.size();
    const bool wrapped =
        start >= static_cast<quint32>(dlen)
        && end + dlen <= static_cast<quint32>(buf.size())
        && buf.mid(static_cast<int>(start) - dlen, dlen) == delim
        && buf.mid(static_cast<int>(end), dlen) == delim;

    QList<MarkoffEdit> out;
    if (wrapped) {
        // Strip both delimiters.
        MarkoffEdit r1; r1.oldStart = start - dlen; r1.oldEnd = start;       r1.newText.clear();
        MarkoffEdit r2; r2.oldStart = end;          r2.oldEnd = end + dlen;  r2.newText.clear();
        out << r1 << r2;
    } else {
        // Wrap.
        MarkoffEdit r1; r1.oldStart = start; r1.oldEnd = start; r1.newText = delim;
        MarkoffEdit r2; r2.oldStart = end;   r2.oldEnd = end;   r2.newText = delim;
        out << r1 << r2;
    }
    return out;
}

}  // namespace

QList<MarkoffEdit> editsForToggleBold(const MarkoffDocument &d, const Selection &s)
{ return toggleDelim(d, s, "**"); }
QList<MarkoffEdit> editsForToggleItalic(const MarkoffDocument &d, const Selection &s)
{ return toggleDelim(d, s, "*"); }
QList<MarkoffEdit> editsForToggleStrikethrough(const MarkoffDocument &d, const Selection &s)
{ return toggleDelim(d, s, "~~"); }
QList<MarkoffEdit> editsForToggleInlineCode(const MarkoffDocument &d, const Selection &s)
{ return toggleDelim(d, s, "`"); }

CollabText::Crdt::Operation toggleBold(MarkoffDocument &d, const Selection &s)
{ return d.applyLocalEdit(editsForToggleBold(d, s)); }
CollabText::Crdt::Operation toggleItalic(MarkoffDocument &d, const Selection &s)
{ return d.applyLocalEdit(editsForToggleItalic(d, s)); }
CollabText::Crdt::Operation toggleStrikethrough(MarkoffDocument &d, const Selection &s)
{ return d.applyLocalEdit(editsForToggleStrikethrough(d, s)); }
CollabText::Crdt::Operation toggleInlineCode(MarkoffDocument &d, const Selection &s)
{ return d.applyLocalEdit(editsForToggleInlineCode(d, s)); }

}  // namespace Markoff::Cmd
