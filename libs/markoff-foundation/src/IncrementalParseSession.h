// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-parser/Document.h>
#include <markoff-parser/TreeSitterParser.h>

#include <QString>

#include <memory>

namespace Markoff::Parse::Detail {

/// Long-lived parsing context for the ParsePool worker. Owns one
/// TreeSitterParser across calls so consecutive parses can reuse the
/// prior tree via tree-sitter's incremental API.
///
/// Workflow:
///   - reset(raw)        — full parse, drop any prior state
///   - applyEdit(newRaw) — incremental reparse; derives a single body
///                         ByteEdit from prior body vs new body
///   - snapshot()        — bake an immutable Markoff::Document from
///                         current parser/extraction state
///
/// Lives on the ParsePool worker thread; ParsePool's queue ensures
/// serialized access. Not thread-safe on its own.
class IncrementalParseSession {
public:
    IncrementalParseSession();
    ~IncrementalParseSession();

    IncrementalParseSession(const IncrementalParseSession &) = delete;
    IncrementalParseSession &operator=(const IncrementalParseSession &) = delete;

    void reset(const QString &raw);
    void applyEdit(const QString &newRaw);

    std::unique_ptr<Markoff::Document> snapshot() const;

private:
    Markoff::TreeSitterParser    m_parser;
    Markoff::ExtractedSource     m_extracted;
    Markoff::DocumentQueryResult m_queries;
    QString                      m_source;
    bool                         m_havePriorParse = false;
};

}  // namespace Markoff::Parse::Detail
