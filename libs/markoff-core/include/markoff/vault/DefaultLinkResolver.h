// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/vault/LinkResolver.h"

namespace Markoff::Vault {

/// No-op link resolver. Always returns an empty string.
class DefaultLinkResolver : public LinkResolver
{
public:
    QString resolve(const QString & /*linkText*/,
                    const QString & /*fromPath*/) const override
    {
        return {};
    }
};

} // namespace Markoff::Vault
