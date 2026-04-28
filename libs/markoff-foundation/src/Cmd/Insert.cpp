// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/Cmd/Insert.h>

#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::Cmd {

namespace {
QList<MarkoffEdit> insertOne(const MarkoffDocument &doc,
                              const CollabText::Crdt::Anchor &a,
                              const QByteArray &text)
{
    MarkoffEdit r;
    r.oldStart = doc.resolveAnchor(a);
    r.oldEnd   = r.oldStart;
    r.newText  = text;
    return { r };
}
}

QList<MarkoffEdit> editsForInsertLink(const MarkoffDocument &d,
                                       const CollabText::Crdt::Anchor &a,
                                       const QString &t, const QString &u)
{
    return insertOne(d, a, QStringLiteral("[%1](%2)").arg(t, u).toUtf8());
}

QList<MarkoffEdit> editsForInsertImage(const MarkoffDocument &d,
                                        const CollabText::Crdt::Anchor &a,
                                        const QString &alt, const QString &u)
{
    return insertOne(d, a, QStringLiteral("![%1](%2)").arg(alt, u).toUtf8());
}

QList<MarkoffEdit> editsForInsertHorizontalRule(const MarkoffDocument &d,
                                                 const CollabText::Crdt::Anchor &a)
{
    return insertOne(d, a, QByteArray("\n---\n"));
}

QList<MarkoffEdit> editsForInsertTable(const MarkoffDocument &d,
                                        const CollabText::Crdt::Anchor &a,
                                        int rows, int cols, bool hasHeader)
{
    if (rows < 1 || cols < 1) return {};
    QByteArray out;
    auto emitRow = [&](){
        out += '|';
        for (int c = 0; c < cols; ++c) out += "  |";
        out += '\n';
    };
    auto emitSep = [&](){
        out += '|';
        for (int c = 0; c < cols; ++c) out += "---|";
        out += '\n';
    };
    emitRow();
    if (hasHeader) emitSep();
    for (int r = 1; r < rows; ++r) emitRow();
    return insertOne(d, a, out);
}

CollabText::Crdt::Operation insertLink(MarkoffDocument &d,
                                        const CollabText::Crdt::Anchor &a,
                                        const QString &t, const QString &u)
{ return d.applyLocalEdit(editsForInsertLink(d, a, t, u)); }
CollabText::Crdt::Operation insertImage(MarkoffDocument &d,
                                         const CollabText::Crdt::Anchor &a,
                                         const QString &alt, const QString &u)
{ return d.applyLocalEdit(editsForInsertImage(d, a, alt, u)); }
CollabText::Crdt::Operation insertHorizontalRule(MarkoffDocument &d,
                                                  const CollabText::Crdt::Anchor &a)
{ return d.applyLocalEdit(editsForInsertHorizontalRule(d, a)); }
CollabText::Crdt::Operation insertTable(MarkoffDocument &d,
                                         const CollabText::Crdt::Anchor &a,
                                         int rows, int cols, bool hasHeader)
{ return d.applyLocalEdit(editsForInsertTable(d, a, rows, cols, hasHeader)); }

}  // namespace Markoff::Cmd
