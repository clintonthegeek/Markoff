// SPDX-License-Identifier: GPL-3.0-or-later
#include "IncrementalParseSession.h"

#include <algorithm>

namespace Markoff::Parse::Detail {

namespace {

/// Compute a single ByteEdit covering the diff window between two UTF-8
/// buffers via longest-common-prefix / longest-common-suffix scan. The
/// scan is O(n) and breaks early on the first byte mismatch from each
/// end. For typical typing edits the diff window is tiny so the scan
/// terminates after a handful of byte comparisons.
///
/// Returned edit: replace `prior[oldStart..oldEnd)` with `nw[oldStart..
/// oldStart+newLength)`.
Markoff::ByteEdit diffBodyBytes(const QByteArray &prior, const QByteArray &nw)
{
    const int priorSz = prior.size();
    const int newSz   = nw.size();
    const int minLen  = std::min(priorSz, newSz);

    int p = 0;
    while (p < minLen && prior.at(p) == nw.at(p))
        ++p;

    // Suffix length cannot extend into the prefix region of either side.
    int s = 0;
    const int maxSuffix = minLen - p;
    while (s < maxSuffix
           && prior.at(priorSz - 1 - s) == nw.at(newSz - 1 - s))
        ++s;

    Markoff::ByteEdit e;
    e.oldStart  = static_cast<quint32>(p);
    e.oldEnd    = static_cast<quint32>(priorSz - s);
    e.newLength = static_cast<quint32>(newSz - s - p);
    return e;
}

}  // namespace

IncrementalParseSession::IncrementalParseSession() = default;
IncrementalParseSession::~IncrementalParseSession() = default;

void IncrementalParseSession::reset(const QString &raw)
{
    m_source     = raw;
    m_extracted  = Markoff::Document::extract(raw);
    m_parser.parse(m_extracted.body);
    m_queries        = m_parser.buildDocumentQueries();
    m_havePriorParse = true;
}

void IncrementalParseSession::applyEdit(const QString &newRaw)
{
    if (!m_havePriorParse) {
        reset(newRaw);
        return;
    }

    Markoff::ExtractedSource newExtracted = Markoff::Document::extract(newRaw);
    const QByteArray priorBodyUtf8 = m_extracted.body.toUtf8();
    const QByteArray newBodyUtf8   = newExtracted.body.toUtf8();

    if (priorBodyUtf8 == newBodyUtf8) {
        // Body unchanged (edit was entirely in frontmatter, or was a
        // no-op after pre-processing). Refresh source/extracted bookkeeping
        // but skip the parser invocation entirely.
        m_source    = newRaw;
        m_extracted = std::move(newExtracted);
        return;
    }

    const Markoff::ByteEdit edit = diffBodyBytes(priorBodyUtf8, newBodyUtf8);
    m_parser.parseIncremental({edit}, newBodyUtf8);

    m_source    = newRaw;
    m_extracted = std::move(newExtracted);
    m_queries   = m_parser.buildDocumentQueries();
}

std::unique_ptr<Markoff::Document>
IncrementalParseSession::snapshot() const
{
    return Markoff::Document::fromComponents(m_source, m_extracted, m_queries);
}

}  // namespace Markoff::Parse::Detail
