// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/parser/ParserSession.h>
#include <markoff/parser/TreeSitterParser.h>
#include "SnapshotBuilder.h"

#include <QByteArray>
#include <QtGlobal>

#include <optional>

namespace Markoff {

struct ParserSession::Impl
{
    TreeSitterParser parser;
    QString lastSource;
    bool haveLast = false;
    /// Valid whenever lastSource's body is the body the parser's live tree
    /// describes; lets an unchanged-body parse skip the query walk
    /// entirely (the walk is the dominant cost, not the parse).
    DocumentQueryResult lastQueries;
};

ParserSession::ParserSession()
    : d(new Impl)
{
}

ParserSession::~ParserSession() = default;
ParserSession::ParserSession(ParserSession &&) noexcept = default;
ParserSession &ParserSession::operator=(ParserSession &&) noexcept = default;

namespace {
/// The minimal contiguous UTF-8 replacement edit turning `oldU` into
/// `newU`: [start, oldEnd) of `oldU` becomes [start, newEnd) of `newU`.
/// Boundaries are backed/forwarded onto UTF-8 scalar starts — tree-sitter's
/// TSInputEdit requires points that match the real text, and splitting a
/// multi-byte sequence would desync row/column accounting. Byte-equal
/// sources produce std::nullopt.
struct ByteEdit { quint32 start; quint32 oldEnd; quint32 newEnd; };

std::optional<ByteEdit> minimalUtf8Edit(const QByteArray &oldU,
                                        const QByteArray &newU)
{
    qsizetype p = 0;
    const qsizetype maxP = qMin(oldU.size(), newU.size());
    while (p < maxP && oldU[int(p)] == newU[int(p)])
        ++p;
    // Back to a scalar start: byte equality across a well-formed sequence
    // is all-or-nothing per byte, but the divergence point can land inside
    // one — align to its lead byte.
    while (p > 0 && (quint8(oldU[int(p)]) & 0xC0) == 0x80)
        --p;

    qsizetype o = oldU.size();
    qsizetype n = newU.size();
    while (o > p && n > p && oldU[int(o - 1)] == newU[int(n - 1)]) {
        --o;
        --n;
    }
    // Forward to scalar starts in both texts.
    while (o < oldU.size() && (quint8(oldU[int(o)]) & 0xC0) == 0x80)
        ++o;
    while (n < newU.size() && (quint8(newU[int(n)]) & 0xC0) == 0x80)
        ++n;

    if (o == oldU.size() && n == newU.size())
        return std::nullopt;  // byte-identical: no edit
    return ByteEdit{quint32(p), quint32(o), quint32(n)};
}
} // namespace

const QString &ParserSession::lastSource() const
{
    return d->lastSource;
}

std::unique_ptr<Document> ParserSession::parse(const QString &source)
{
    ExtractedSource extracted = Document::extract(source);
    bool bodyUnchanged = false;
    if (d->haveLast && d->parser.hasTree()) {
        // Diff BODY vs BODY: the edit offsets the parser's reparse()
        // expects are in the text it actually parsed (the body), so
        // frontmatter changes that leave the body untouched never reach
        // the parser at all, and frontmatter-length changes cannot shift
        // body-relative offsets.
        const QString lastBody = Document::extract(d->lastSource).body;
        if (lastBody != extracted.body) {
            const auto edit = minimalUtf8Edit(lastBody.toUtf8(),
                                              extracted.body.toUtf8());
            bool ok = false;
            if (edit) {
                // A failed re-parse must not publish an empty snapshot:
                // reparse() invalidates the tree wholesale on failure, so
                // re-parse fresh rather than return the invalid state.
                ok = d->parser.reparse(extracted.body, edit->start,
                                       edit->oldEnd, edit->newEnd);
            }
            if (!ok)
                d->parser.parse(extracted.body);  // fresh parse
        } else {
            bodyUnchanged = true;  // frontmatter-only edit or same source
        }
    } else {
        d->parser.parse(extracted.body);
    }

    DocumentQueryResult q;
    if (bodyUnchanged) {
        // The tree already describes this body and lastQueries is the walk
        // of exactly that tree: reuse it instead of re-walking (ASK-M31's
        // vault/filter case — the re-parse is now a snapshot copy).
        q = d->lastQueries;
    } else {
        q = d->parser.buildDocumentQueries();
        d->lastQueries = q;
    }
    d->lastSource = source;
    d->haveLast = true;
    return Detail::buildDocumentSnapshot(source, std::move(extracted),
                                         std::move(q));
}

} // namespace Markoff
