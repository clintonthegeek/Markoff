// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QSharedPointer>
#include <QTextCharFormat>

namespace Qutepart {

class Theme;

class Style {
  public:
    Style();
    Style(const QString &defStyleName, QSharedPointer<QTextCharFormat> format);

    /* Called by some clients.
       If the style knows attribute it can better detect textType
     */
    void updateTextType(const QString &attribute);

    inline char textType() const { return _textType; }
    inline const QStringView getDefStyle() const { return defStyleName; }
    inline const QSharedPointer<QTextCharFormat> format() const { return displayFormat; }

    void setTheme(const Theme *newTheme);
    inline const Theme *getTheme() const { return theme; }

  private:
    QSharedPointer<QTextCharFormat> savedFormat;
    QSharedPointer<QTextCharFormat> displayFormat;
    char _textType;

    QString defStyleName;
    const Theme *theme = nullptr;
};

Style makeStyle(const QString &defStyleName, const QString &color, const QString & /*selColor*/,
                const QHash<QString, bool> &flags, QString &error);

} // namespace Qutepart
