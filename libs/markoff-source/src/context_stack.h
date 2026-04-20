// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QStringList>

namespace Qutepart {

class ContextSwitcher;
class Context;
struct ContextStackItem;
class ContextStack;

uint qHash(const ContextStackItem &key, uint seed = 0);
uint qHash(const ContextStack &key, uint seed = 0);

struct ContextStackItem {
    ContextStackItem();
    ContextStackItem(const Context *context, const QStringList &data = QStringList());

    bool operator==(const ContextStackItem &other) const;

    const Context *context;
    QStringList data;
};

class ContextStack {
  public:
    ContextStack(Context *context);

    bool operator==(const ContextStack &other) const;
    bool operator!=(const ContextStack &other) const;

  private:
    ContextStack(const QVector<ContextStackItem> &items);

  public:
    // Apply context switch operation and return new context
    ContextStack switchContext(const ContextSwitcher &operation,
                               const QStringList &data = QStringList()) const;

    // Get current context
    const Context *currentContext() const;

    // Get current data
    const QStringList &currentData() const;

  private:
    QVector<ContextStackItem> items;

    friend uint qHash(const ContextStack &key, uint seed);
};

} // namespace Qutepart
