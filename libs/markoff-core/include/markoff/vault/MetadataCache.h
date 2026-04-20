// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/vault/CachedMetadata.h"

#include <QString>

namespace Markoff::Vault {

class MetadataCache
{
public:
    virtual ~MetadataCache() = default;

    /// Returns the cached metadata for `path`, or nullptr if not cached.
    /// Callers typically fall back to synchronous MetadataParser::parse()
    /// when the cache misses.
    virtual const CachedMetadata *getFileCache(const QString &path) const = 0;
};

} // namespace Markoff::Vault
