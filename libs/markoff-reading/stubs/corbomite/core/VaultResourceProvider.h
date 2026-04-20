// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim for Corbomite::Core::VaultResourceProvider. The real
// type lives in the Corbomite tree; this file exists only so markoff-reading
// compiles and links without pulling in Corbomite::Core. Runtime wiring is
// broken until Phase B absorbs the real headers.
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

namespace Corbomite::Core {

class VaultResourceProvider
{
public:
    virtual ~VaultResourceProvider() = default;

    virtual QUrl resolveWikiLink(const QString & /*linkText*/) const
    {
        return {};
    }
    virtual std::optional<QString>
    resolveEmbed(const QString & /*targetPath*/) const
    {
        return std::nullopt;
    }
    virtual QUrl resolveImage(const QString & /*name*/) const { return {}; }
    virtual QByteArray loadImageBytes(const QString & /*name*/) const
    {
        return {};
    }
    virtual bool wikiLinkExists(const QString & /*target*/) const
    {
        return false;
    }
};

} // namespace Corbomite::Core
