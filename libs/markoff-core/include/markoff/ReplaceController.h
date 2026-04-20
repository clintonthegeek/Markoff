// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/SearchController.h>

namespace Markoff {

class ReplaceController : public SearchController {
    Q_OBJECT
public:
    ReplaceController(MarkoffDocument *doc, SearchAdapter *adapter,
                      QObject *parent = nullptr);

    /// Replace the current match. No-op if adapter->supportsReplace()
    /// is false or if there is no current match. Logs a diagnostic
    /// when refused.
    void replaceCurrent(const QString &with);

    /// Replace every match in one atomic undo step. Returns the
    /// number replaced. Zero if the adapter rejects replacement.
    int replaceAll(const QString &with);
};

}  // namespace Markoff
