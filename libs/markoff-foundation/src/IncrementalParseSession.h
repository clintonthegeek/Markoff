// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ParsePhases.h"

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

    /// Read-only access to the underlying TreeSitterParser. Used by the
    /// in-tree benchmark to read inline-tree-reuse and block-changed-bytes
    /// counters. Not part of the public foundation API.
    const Markoff::TreeSitterParser &parser() const { return m_parser; }

    /// Wire an external phase-accumulator table. The session adds elapsed
    /// nanoseconds for each instrumented phase into `(*table)[phase]` on
    /// every call to applyEdit / reset / snapshot. Pass nullptr (the
    /// default) to disable accumulation. Caller owns the table; the session
    /// never resets it.
    void setPhaseTable(ParsePhaseTable *table) noexcept { m_phaseTable = table; }
    ParsePhaseTable *phaseTable() const noexcept { return m_phaseTable; }

private:
    Markoff::TreeSitterParser    m_parser;
    Markoff::ExtractedSource     m_extracted;
    Markoff::DocumentQueryResult m_queries;
    QString                      m_source;
    bool                         m_havePriorParse = false;
    ParsePhaseTable             *m_phaseTable     = nullptr;
};

}  // namespace Markoff::Parse::Detail
