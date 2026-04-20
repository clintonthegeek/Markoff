// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QKeyEvent>
#include <QObject>
#include <QString>
#include <QTextBlock>

#include "alg_impl.h"
#include "qutepart.h"

namespace Qutepart {

class Indenter : public QObject {
  public:
    Indenter(QObject *parent);
    ~Indenter();

    void setAlgorithm(IndentAlg alg);

    QString indentText() const;

    int width() const;
    void setWidth(int);

    bool useTabs() const;
    void setUseTabs(bool);

    void setLanguage(const QString &language);

    bool shouldAutoIndentOnEvent(QKeyEvent *event) const;
    bool shouldUnindentWithBackspace(const QTextCursor &cursor) const;
#if 0
    void autoIndentBlock(QTextBlock block, QChar typedKey) const;
#endif
    void indentBlock(QTextBlock block, int cursorPos, int typedKey) const;
  public slots:
    void onShortcutIndentAfterCursor(QTextCursor cursor) const;
    void onShortcutUnindentWithBackspace(QTextCursor &cursor) const;

  private:
    IndentAlgImpl *alg_;
    bool useTabs_;
    int width_;
    QString language_;
};

} // namespace Qutepart
