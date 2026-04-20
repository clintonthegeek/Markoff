// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

namespace Markoff::Vault {

/// Abstract host-side vault surface consumed by Markoff rendering. Hosts
/// (CorbomiteApp) implement to resolve wiki-links / embeds / images against
/// their vault. Standalone builds use Markoff::Vault::DefaultResourceProvider.
class ResourceProvider
{
public:
    virtual ~ResourceProvider() = default;

    virtual QUrl resolveWikiLink(const QString &linkText) const = 0;
    virtual std::optional<QString>
    resolveEmbed(const QString &targetPath) const = 0;
    virtual QUrl resolveImage(const QString &name) const = 0;
    virtual QByteArray loadImageBytes(const QString &name) const = 0;
    virtual bool wikiLinkExists(const QString &target) const = 0;
};

} // namespace Markoff::Vault
