// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSyntaxHighlighter>

class QTextDocument;

namespace Markoff::Styled {

/// Whole-document QSyntaxHighlighter for cursor-derived format overlays
/// (delimiter visibility, find-span highlights). v0 stub — no-op in
/// highlightBlock. v0.1 promotes to cursor-aware delimiter hide.
class DocHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit DocHighlighter(QTextDocument *parent);
    ~DocHighlighter() override;

protected:
    void highlightBlock(const QString &text) override;
};

}  // namespace Markoff::Styled
