// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/vault/MetadataParser.h"

namespace Markoff::Vault {

/// No-op metadata parser. Returns an empty MetadataParseResult; callers
/// typically treat this as "no headings / no blocks" and render a placeholder.
class DefaultMetadataParser : public MetadataParser
{
public:
    MetadataParseResult parse(const QByteArray & /*content*/,
                              const QString & /*path*/,
                              const LinkResolver & /*resolver*/) const override
    {
        return {};
    }
};

} // namespace Markoff::Vault
