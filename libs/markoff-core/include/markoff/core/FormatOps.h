// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QByteArray>
#include <QString>

#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {
class MarkoffDocument;

/// Widget-free markdown format operations over the single-`\n`
/// widgetFlatView coordinate space (spec
/// docs/specs/2026-06-09-markdownview-contract-v2-design.md §5).
/// Lifted from markoff-source's Editor (queue #8.6-hardened,
/// block-aware via Detail::findBlockAtSepByte). Each op mutates the
/// model through d2 primitives and returns the UTF-16 caret/selection
/// the caller should re-apply after its binding's reverse sync.
///
/// `flatText` is the widget's toPlainText() — which IS
/// widgetFlatView() decoded — at the time of the call; it must be
/// re-fetched between consecutive ops (each op changes the view).
/// A `std::nullopt` return means no edit was performed and the caller
/// must leave its cursor untouched (matching the donor's early-return
/// paths, which never called setTextCursor — e.g. a setHeadingLevel
/// no-op must not collapse an existing selection).
namespace FormatOps {

/// UTF-16 positions over widgetFlatView. start == end is a caret.
struct QtRange { int start = 0; int end = 0; };

MARKOFF_CORE_EXPORT std::optional<QtRange> wrapToggle(
        MarkoffDocument *doc,
        const QString &flatText,
        QtRange sel,
        const QByteArray &delim);   // ** _ ~~ `
MARKOFF_CORE_EXPORT std::optional<QtRange> insertLink(
        MarkoffDocument *doc,
        const QString &flatText,
        QtRange sel);
MARKOFF_CORE_EXPORT std::optional<QtRange> setHeadingLevel(
        MarkoffDocument *doc,
        const QString &flatText,
        int caretQtPos, int level);

}  // namespace FormatOps
}  // namespace Markoff
