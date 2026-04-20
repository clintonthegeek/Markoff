// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QHash>
#include <QSharedPointer>
#include <QString>

namespace Qutepart {

class Context;
typedef QSharedPointer<Context> ContextPtr;

class ContextSwitcher {
  public:
    ContextSwitcher();
    ContextSwitcher(int popsCount, const QString &contextName, const QString &contextOperation);

    QString toString() const;
    bool isNull() const;

    void resolveContextReferences(const QHash<QString, ContextPtr> &contexts, QString &error);

    int popsCount() const { return _popsCount; }
    ContextPtr context() const { return _context; }

  protected:
    int _popsCount;
    QString contextName;
    ContextPtr _context;
    QString contextOperation;
};

} // namespace Qutepart
