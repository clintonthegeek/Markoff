// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QString>
#include <QTextBlock>

#include "text_block_utils.h"

#include "indenter.h"

namespace Qutepart {

class IndentAlgCstyle : public IndentAlgImpl {
  public:
    const QString &triggerCharacters() const override;
    QString indentLine(QTextBlock block, int cursorPos) const override;
    QString computeSmartIndent(QTextBlock block, int cursorPos) const override;

  private:
    QString findLeftBrace(const QTextBlock &block, int column) const;
    TextPosition tryParenthesisBeforeBrace(const TextPosition &pos) const;
    QString trySwitchStatement(const QTextBlock &block) const;
    QString tryAccessModifiers(const QTextBlock &block) const;
    QString tryCComment(const QTextBlock &block) const;
    QString tryCppComment(const QTextBlock &block) const;
    QString tryBrace(const QTextBlock &block) const;
    QString tryCKeywords(const QTextBlock &block, bool isBrace) const;
    QString tryCondition(const QTextBlock &block) const;
    QString tryStatement(const QTextBlock &block) const;
    QString tryMatchedAnchor(const QTextBlock &block, bool autoIndent) const;
    QString indentLine(const QTextBlock &block, bool autoIndent) const;
    QString processChar(const QTextBlock &block, QChar c, int cursorPos) const;
};

} // namespace Qutepart
