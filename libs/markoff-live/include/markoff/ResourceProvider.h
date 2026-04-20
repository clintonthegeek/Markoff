// libs/markoff/include/markoff/ResourceProvider.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_RESOURCEPROVIDER_H
#define MARKOFF_RESOURCEPROVIDER_H

#include <QUrl>
#include <QString>
#include <optional>

namespace Markoff {

class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;

    virtual QUrl resolveImage(const QString &name) const = 0;
    virtual std::optional<QString> resolveEmbed(const QString &name) const = 0;
    virtual QUrl resolveLink(const QString &target) const = 0;
    virtual bool linkExists(const QString &target) const = 0;
};

class FilesystemResourceProvider : public ResourceProvider {
public:
    explicit FilesystemResourceProvider(const QString &basePath);

    QUrl resolveImage(const QString &name) const override;
    std::optional<QString> resolveEmbed(const QString &name) const override;
    QUrl resolveLink(const QString &target) const override;
    bool linkExists(const QString &target) const override;

private:
    QString m_basePath;
};

} // namespace Markoff

#endif // MARKOFF_RESOURCEPROVIDER_H
