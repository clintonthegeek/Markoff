// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QString>

namespace Markoff::Vault {

class LinkResolver
{
public:
    virtual ~LinkResolver() = default;

    virtual QString resolve(const QString &linkText,
                            const QString &fromPath) const = 0;
};

} // namespace Markoff::Vault
