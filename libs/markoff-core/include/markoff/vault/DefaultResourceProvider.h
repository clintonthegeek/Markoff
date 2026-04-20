// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/vault/ResourceProvider.h"

namespace Markoff::Vault {

/// No-op resource provider. Every method returns the empty value. Used by
/// ReadingView as a lazy default when the host has not injected a real
/// implementation.
class DefaultResourceProvider : public ResourceProvider
{
public:
    QUrl resolveWikiLink(const QString & /*linkText*/) const override
    {
        return {};
    }
    std::optional<QString>
    resolveEmbed(const QString & /*targetPath*/) const override
    {
        return std::nullopt;
    }
    QUrl resolveImage(const QString & /*name*/) const override { return {}; }
    QByteArray loadImageBytes(const QString & /*name*/) const override
    {
        return {};
    }
    bool wikiLinkExists(const QString & /*target*/) const override
    {
        return false;
    }
};

} // namespace Markoff::Vault
