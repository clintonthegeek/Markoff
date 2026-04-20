// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/vault/MetadataCache.h"

namespace Markoff::Vault {

/// No-op metadata cache. Always reports a miss (nullptr).
class DefaultMetadataCache : public MetadataCache
{
public:
    const CachedMetadata *
    getFileCache(const QString & /*path*/) const override
    {
        return nullptr;
    }
};

} // namespace Markoff::Vault
