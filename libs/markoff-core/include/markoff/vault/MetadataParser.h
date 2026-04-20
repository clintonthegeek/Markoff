// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/vault/CachedMetadata.h"
#include "markoff/vault/LinkResolver.h"

#include <QByteArray>
#include <QString>

namespace Markoff::Vault {

struct MetadataParseResult
{
    CachedMetadata cache;
};

/// Abstract metadata parser. The host wires a concrete implementation
/// (Corbomite's MetadataParserImpl wraps its existing static parser);
/// standalone builds use Markoff::Vault::DefaultMetadataParser.
class MetadataParser
{
public:
    virtual ~MetadataParser() = default;

    virtual MetadataParseResult parse(const QByteArray &content,
                                      const QString &path,
                                      const LinkResolver &resolver) const = 0;
};

} // namespace Markoff::Vault
