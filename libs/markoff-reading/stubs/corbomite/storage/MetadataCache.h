// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See core/VaultResourceProvider.h for context.
#pragma once

#include "corbomite/storage/CachedMetadata.h"

#include <QString>

namespace Corbomite {

class MetadataCache
{
public:
    virtual ~MetadataCache() = default;

    // Returns nullptr in the stub: markoff-reading gracefully falls back to
    // a synchronous parse (via MetadataParser::parse) when the cache is
    // empty. That fallback is all the Phase-A build exercises.
    virtual const CachedMetadata *
    getFileCache(const QString & /*path*/) const
    {
        return nullptr;
    }
};

} // namespace Corbomite
