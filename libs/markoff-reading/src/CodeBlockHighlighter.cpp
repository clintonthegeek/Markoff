// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transplanted from Penelope's CodeBlockHighlighter at:
//   ~/dev/Penelope/src/markdown/codeblockhighlighter.{h,cpp}
// Penelope HEAD at transplant time: 6b9c32344032c9eb54c041970a5a3e2feff7caff
// Penelope is GPL-3.0 (see ~/dev/Penelope/COPYING).
// Adapted for Corbomite's libs/readingview/ — theme source changed from
// Penelope's ThemeManager to a local Theme enum; namespace rebadged.
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "corbomite/readingview/CodeBlockHighlighter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/State>

#include <QFont>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>

namespace Corbomite::ReadingView {

CodeBlockHighlighter::CodeBlockHighlighter(Theme theme)
{
    const auto ksTheme = (theme == Theme::Dark)
        ? m_repository.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme)
        : m_repository.defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
    AbstractHighlighter::setTheme(ksTheme);
}

void CodeBlockHighlighter::highlight(QTextDocument *document)
{
    QTextBlock block = document->begin();
    KSyntaxHighlighting::State state;
    QString currentLang;

    while (block.isValid()) {
        QTextBlockFormat bf = block.blockFormat();
        QString lang = bf.property(QTextFormat::BlockCodeLanguage).toString();

        if (!lang.isEmpty()) {
            if (lang != currentLang) {
                currentLang = lang;
                auto def = m_repository.definitionForName(lang);
                if (!def.isValid())
                    def = m_repository.definitionForFileName(
                        QStringLiteral("file.") + lang);
                setDefinition(def);
                state = KSyntaxHighlighting::State();
            }

            if (definition().isValid()) {
                m_currentBlock = block;
                state = highlightLine(block.text(), state);
            }
        } else {
            if (!currentLang.isEmpty()) {
                currentLang.clear();
                state = KSyntaxHighlighting::State();
            }
        }

        block = block.next();
    }
}

void CodeBlockHighlighter::applyFormat(
    int offset, int length,
    const KSyntaxHighlighting::Format &format)
{
    if (!m_currentBlock.isValid())
        return;
    if (format.isDefaultTextStyle(theme()))
        return;

    QTextCursor cursor(m_currentBlock);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, offset);
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, length);

    QTextCharFormat cf;
    if (format.hasTextColor(theme()))
        cf.setForeground(format.textColor(theme()));
    if (format.hasBackgroundColor(theme()))
        cf.setBackground(format.backgroundColor(theme()));
    if (format.isBold(theme()))
        cf.setFontWeight(QFont::Bold);
    if (format.isItalic(theme()))
        cf.setFontItalic(true);
    if (format.isUnderline(theme()))
        cf.setFontUnderline(true);
    if (format.isStrikeThrough(theme()))
        cf.setFontStrikeOut(true);

    cursor.mergeCharFormat(cf);
}

} // namespace Corbomite::ReadingView
