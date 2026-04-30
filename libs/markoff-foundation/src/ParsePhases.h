// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <array>
#include <chrono>

namespace Markoff::Parse::Detail {

/// Phase taxonomy used by IncrementalParseSession::applyEdit (and reset /
/// snapshot). Foundation-internal — exposed to the in-tree benchmark via
/// PRIVATE include access. Production callers do not see this header.
enum class ParsePhase : int {
    Extract     = 0,  ///< Document::extract: frontmatter + footnote refs.
    Diff        = 1,  ///< prefix/suffix scan to derive a single ByteEdit.
    ParseBlock  = 2,  ///< tree-sitter block-tree edit + reparse.
    ParseInline = 3,  ///< inline-tree reuse + per-region fresh parses.
    Queries     = 4,  ///< buildDocumentQueries: full-tree walk.
    Snapshot    = 5,  ///< Document::fromComponents in snapshot().
    Count       = 6,  ///< sentinel — keep last.
};

constexpr int kParsePhaseCount = static_cast<int>(ParsePhase::Count);

/// Per-iteration phase totals in nanoseconds, indexed by ParsePhase. The
/// caller owns the table; the session accumulates into it. Callers reset
/// the table to {} between iterations if they want non-cumulative samples.
using ParsePhaseTable = std::array<quint64, kParsePhaseCount>;

/// RAII guard that adds the elapsed wall time (steady_clock nanoseconds)
/// of its lifetime to `(*table)[phase]`. If `table` is nullptr the guard
/// is a no-op (still cheap — one steady_clock::now() call); production
/// callers leave the table null and pay nothing measurable.
class ParsePhaseGuard {
public:
    ParsePhaseGuard(ParsePhaseTable *table, ParsePhase phase) noexcept
        : m_table(table), m_phase(phase),
          m_start(std::chrono::steady_clock::now()) {}

    ~ParsePhaseGuard() {
        if (!m_table) return;
        const auto end = std::chrono::steady_clock::now();
        const auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            end - m_start).count();
        (*m_table)[static_cast<int>(m_phase)] += static_cast<quint64>(ns);
    }

    ParsePhaseGuard(const ParsePhaseGuard &) = delete;
    ParsePhaseGuard &operator=(const ParsePhaseGuard &) = delete;

private:
    ParsePhaseTable *m_table;
    ParsePhase       m_phase;
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace Markoff::Parse::Detail
