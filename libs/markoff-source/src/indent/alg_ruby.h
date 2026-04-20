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

class RubyStatement {
  public:
    RubyStatement(QTextBlock startBlock, QTextBlock endBlock);

    // Convert to string for debugging
    QString toString() const;

    TextPosition offsetToTextPos(int offset) const;

    // Return document.isCode at the given offset in a statement
    bool isPosCode(int offset) const;

    // Return document.isComment at the given offset in a statement
    bool isPosComment(int offset) const;

    // Return the indent at the beginning of the statement
    QString indent() const;
    // Return the content of the statement from the document
    QString content() const;

    QTextBlock startBlock;
    QTextBlock endBlock;

  private:
    mutable QString contentCache_;
};

class IndentAlgRuby : public IndentAlgImpl {
  public:
    virtual const QString &triggerCharacters() const override;
    virtual bool shouldTrimPrevEmptyLine() const override { return true; }
    virtual QString computeSmartIndent(QTextBlock block, int cursorPos) const override;


  private:
    bool isCommentBlock(QTextBlock block) const;
    QTextBlock prevNonCommentBlock(QTextBlock block) const;
    bool isLastCodeColumn(QTextBlock block, int column) const;
    bool isStmtContinuing(QTextBlock block) const;
    QTextBlock findStmtStart(QTextBlock block) const;
    bool isValidTrigger(QTextBlock block) const;

    RubyStatement findPrevStmt(QTextBlock block) const;
    bool isBlockStart(const RubyStatement &stmt) const;
    bool isBlockEnd(const RubyStatement &stmt) const;

    RubyStatement findBlockStart(QTextBlock block) const;
};

} // namespace Qutepart
