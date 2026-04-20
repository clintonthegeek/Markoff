// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#include "loader.h"

#include "context_switcher.h"

namespace Qutepart {

ContextSwitcher::ContextSwitcher() : _popsCount(0) {}

ContextSwitcher::ContextSwitcher(int popsCount, const QString &contextName,
                                 const QString &contextOperation)
    : _popsCount(popsCount), contextName(contextName), contextOperation(contextOperation) {}

QString ContextSwitcher::toString() const { return contextOperation; }

bool ContextSwitcher::isNull() const { return contextOperation.isEmpty(); }

void ContextSwitcher::resolveContextReferences(const QHash<QString, ContextPtr> &contexts,
                                               QString &error) {
    if (contextName.isEmpty()) {
        return;
    }

    if (contextName.contains('#')) {
        _context = loadExternalContext(contextName);
        return;
    }

    if (!contexts.contains(contextName)) {
        error = QString("Failed to get context '%1'").arg(contextName);
        return;
    }

    _context = contexts[contextName];
}

} // namespace Qutepart
