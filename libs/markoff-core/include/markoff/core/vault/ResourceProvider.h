// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

namespace Markoff::Vault {

/// Abstract host-side vault surface consumed by Markoff rendering (embeds,
/// images, wikilink resolution). Hosts (CorbomiteApp) implement to resolve
/// against their vault. Restored 2026-05-20 driven by Corbomite port pull on
/// EmbedRegistry — the abstract was retired with the old leaves but is
/// reintroduced here as a minimal pure-virtual interface (matching the
/// master-side shape for migration ease). No `DefaultResourceProvider` ships
/// in markoff-core; standalone builds construct their own no-op concrete if
/// one is needed.
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
