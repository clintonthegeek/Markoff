// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QIcon>
#include <QStack>
#include <QTextBlockUserData>

#include "context_stack.h"

namespace Qutepart {

class Language;

class TextBlockUserData : public QTextBlockUserData {
  public:
    TextBlockUserData(const QString &textTypeMap, const ContextStack &contexts);
    QString textTypeMap;
    QVector<QSharedPointer<Language>> languageMap;
    ContextStack contexts;
    int state = 0;

    struct {
        int level = 0;
        bool folded = false;
    } folding;
    QStack<QString> regions;

    struct {
        QString message;
    } metaData;
};

} // namespace Qutepart
