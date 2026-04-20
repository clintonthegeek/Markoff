// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QSyntaxHighlighter>
#include <QTextDocument>

#include "language.h"
#include "text_block_user_data.h"

namespace Qutepart {

class Theme;

class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

  public:
    SyntaxHighlighter(QObject *parent, QSharedPointer<Language> language);
    SyntaxHighlighter(QTextDocument *parent, QSharedPointer<Language> language);

    inline QSharedPointer<Language> getLanguage() const { return language; }
    inline void setTheme(const Theme *t) {
        language->setTheme(t);
        rehighlight();
    }

  protected:
    void highlightBlock(const QString &text) override;
    QSharedPointer<Language> language;
};

} // namespace Qutepart
