// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See core/VaultResourceProvider.h for context.
#pragma once

#include <QString>

namespace Corbomite {

class LinkResolver
{
public:
    LinkResolver() = default;
    virtual ~LinkResolver() = default;

    virtual QString resolve(const QString & /*linkText*/,
                            const QString & /*fromPath*/) const
    {
        return {};
    }
};

} // namespace Corbomite
