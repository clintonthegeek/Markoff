// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QString>

namespace Qutepart {

/* Peace of text, which shall be matched.
 * Contains pre-calculated and pre-checked data for performance optimization
 */
class TextToMatch {
  public:
    TextToMatch(const QString &text, const QStringList &contextData);

    void shiftOnce();
    void shift(int count);

    bool isEmpty() const;

    QString word(const QString &deliminators) const;

    int currentColumnIndex;
    QString wholeLineText;
    QStringView text;
    int textLength;
    bool firstNonSpace;
    bool isWordStart;
    const QStringList *contextData;
};

} // namespace Qutepart
