// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

namespace Markoff::Canvas::Detail {

/// One `key: value` row recovered from a raw frontmatter YAML blob for the
/// collapsed "properties" render (P5.5). Deliberately narrow: only
/// flat-scalar top-level `key: value` lines are recognized (the common
/// case — title/date/tags-as-scalar/etc.); nested maps, block scalars
/// (`|`/`>`) and list items (`- foo`) are skipped rather than
/// mis-rendered. A skipped/unparsed line is simply absent from the
/// properties list — the raw-YAML reveal (View::paintFrontmatter's
/// expanded state) is always available as the ground truth, same "reduced
/// scope, log it" shape as prior P5 findings (P4.5 column overflow, P5.3
/// inline math). No document/core dependency — pure string parsing over
/// the blob `MarkoffDocument::frontmatterValue("raw")` already returns.
struct FrontmatterProperty {
    QString key;
    QString value;
};

/// Parses `rawYaml` (the frontmatter map's "raw" value, UTF-8 decoded) into
/// an ordered list of recognized `key: value` rows. Returns an empty list
/// for YAML this parser doesn't model (nested structures, pure lists) —
/// callers fall back to showing a generic "Properties" label with no rows,
/// or the caller's own choice of placeholder.
QList<FrontmatterProperty> parseFrontmatterProperties(const QString &rawYaml);

}  // namespace Markoff::Canvas::Detail
