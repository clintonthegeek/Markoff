// libs/markoff/src/ResourceProvider.cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "markoff/ResourceProvider.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace Markoff {

FilesystemResourceProvider::FilesystemResourceProvider(const QString &basePath)
    : m_basePath(basePath)
{
}

QUrl FilesystemResourceProvider::resolveImage(const QString &name) const
{
    QFileInfo fi(QDir(m_basePath), name);
    if (fi.exists())
        return QUrl::fromLocalFile(fi.absoluteFilePath());
    return {};
}

std::optional<QString> FilesystemResourceProvider::resolveEmbed(const QString &name) const
{
    QString fileName = name;
    if (!fileName.endsWith(QStringLiteral(".md")))
        fileName += QStringLiteral(".md");

    QFile file(QDir(m_basePath).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;

    QTextStream stream(&file);
    return stream.readAll();
}

QUrl FilesystemResourceProvider::resolveLink(const QString &target) const
{
    // Strip heading fragment for file resolution
    QString filePart = target;
    int hashPos = target.indexOf(QLatin1Char('#'));
    if (hashPos >= 0)
        filePart = target.left(hashPos);

    if (filePart.isEmpty())
        return {};

    QString fileName = filePart;
    if (!fileName.endsWith(QStringLiteral(".md")))
        fileName += QStringLiteral(".md");

    QFileInfo fi(QDir(m_basePath), fileName);
    if (fi.exists())
        return QUrl::fromLocalFile(fi.absoluteFilePath());
    return {};
}

bool FilesystemResourceProvider::linkExists(const QString &target) const
{
    QString filePart = target;
    int hashPos = target.indexOf(QLatin1Char('#'));
    if (hashPos >= 0)
        filePart = target.left(hashPos);

    if (filePart.isEmpty())
        return false;

    QString fileName = filePart;
    if (!fileName.endsWith(QStringLiteral(".md")))
        fileName += QStringLiteral(".md");

    return QFileInfo::exists(QDir(m_basePath).filePath(fileName));
}

} // namespace Markoff
