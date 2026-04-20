// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transplanted from Penelope's CodeBlockHighlighter at:
//   ~/dev/Penelope/src/markdown/codeblockhighlighter.{h,cpp}
// Penelope HEAD at transplant time: 6b9c32344032c9eb54c041970a5a3e2feff7caff
// Penelope is GPL-3.0 (see ~/dev/Penelope/COPYING).
// Adapted for Corbomite's libs/readingview/ — theme source changed from
// Penelope's ThemeManager to a local Theme enum; namespace rebadged.
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_CODEBLOCKHIGHLIGHTER_H
#define CORBOMITE_READINGVIEW_CODEBLOCKHIGHLIGHTER_H

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QTextBlock>
#include <QTextDocument>

namespace Corbomite::ReadingView {

/// Theme selector for Reading-mode code-block highlighting.
///
/// Phase 0b placeholder: a minimal enum that maps to KSyntaxHighlighting's
/// built-in default Light/Dark themes. Later phases will replace this with
/// Corbomite's unified theme pipeline.
enum class Theme {
    Light,
    Dark,
};

/// Applies KSyntaxHighlighting to fenced code blocks within a QTextDocument.
///
/// Each QTextBlock whose QTextBlockFormat carries QTextFormat::BlockCodeLanguage
/// is highlighted against the Definition for that language; adjacent blocks of
/// the same language share the same tokenizer state.
class CodeBlockHighlighter : public KSyntaxHighlighting::AbstractHighlighter
{
public:
    explicit CodeBlockHighlighter(Theme theme = Theme::Light);

    void highlight(QTextDocument *document);

protected:
    void applyFormat(int offset, int length,
                     const KSyntaxHighlighting::Format &format) override;

private:
    KSyntaxHighlighting::Repository m_repository;
    QTextBlock m_currentBlock;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_CODEBLOCKHIGHLIGHTER_H
