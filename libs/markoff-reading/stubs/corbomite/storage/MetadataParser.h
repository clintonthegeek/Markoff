// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See core/VaultResourceProvider.h for context.
#pragma once

#include "corbomite/storage/CachedMetadata.h"
#include "corbomite/storage/LinkResolver.h"

#include <QByteArray>
#include <QString>

namespace Corbomite {

struct MetadataParseResult
{
    CachedMetadata cache;
};

class MetadataParser
{
public:
    static MetadataParseResult parse(const QByteArray & /*content*/,
                                     const QString & /*path*/,
                                     const LinkResolver & /*resolver*/)
    {
        // Stub: returns empty metadata. Phase-A callers treat this as
        // "no headings / no blocks" — they fall through to the
        // "subpath marker" branch and render a placeholder.
        return {};
    }
};

} // namespace Corbomite
