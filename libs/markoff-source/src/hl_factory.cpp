// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "hl/loader.h"
#include "hl/syntax_highlighter.h"
#include "qutepart.h"

#include "hl_factory.h"

namespace Qutepart {

class Theme;

QSyntaxHighlighter *makeHighlighter(QObject *parent, const QString &languageId) {
    QSharedPointer<Language> language = loadLanguage(languageId);
    if (!language.isNull()) {
        return new SyntaxHighlighter(parent, language);
    }

    return nullptr;
}

QSyntaxHighlighter *makeHighlighter(QTextDocument *parent, const QString &languageId) {
    QSharedPointer<Language> language = loadLanguage(languageId);
    if (!language.isNull()) {
        return new SyntaxHighlighter(parent, language);
    }

    return nullptr;
}

} // namespace Qutepart
