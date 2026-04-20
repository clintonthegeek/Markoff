// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QSyntaxHighlighter>

namespace Qutepart {

class Theme;

/**
 * Choose and load a highlighter.
 *
 * Set as much parameters at posiible to detect language correctly
 *
 * See QSyntaxHighlighter::QSyntaxHighlighter(..) documentation.
 */
QSyntaxHighlighter *makeHighlighter(QObject *parent, const QString &languageId);

QSyntaxHighlighter *makeHighlighter(QTextDocument *parent, const QString &langugeId);

} // namespace Qutepart
