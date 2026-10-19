// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/EmbedRender.h>

#include <QCryptographicHash>

#include <markoff/core/Theme.h>

namespace Markoff {

QString embedArtifactKey(const RenderRequest &request)
{
    const QByteArray sourceHash =
        QCryptographicHash::hash(request.source.toUtf8(),
                                 QCryptographicHash::Sha1).toHex();
    const int widthBucket = request.availableWidth.width() < 0
        ? -1
        : qRound(request.availableWidth.width());
    const int heightBucket = request.availableWidth.height() < 0
        ? -1
        : qRound(request.availableWidth.height());
    const int dprBucket = qRound(request.devicePixelRatio * 100.0);
    const int scaleBucket = qRound(request.fontScale * 100.0);

    return QStringLiteral("%1|%2|%3|%4x%5|dpr%6|fs%7")
        .arg(request.language.toLower(), QString::fromLatin1(sourceHash),
             request.themeKey)
        .arg(widthBucket)
        .arg(heightBucket)
        .arg(dprBucket)
        .arg(scaleBucket);
}

QString embedThemeKey(const Theme &theme)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const Theme::Slot themeSlots[] = {
        Theme::Slot::TextDefault, Theme::Slot::CodeBlock,
        Theme::Slot::CodeBlockBackground, Theme::Slot::Quote,
        Theme::Slot::Link, Theme::Slot::EditorBackground,
        Theme::Slot::Highlight,
    };
    for (const Theme::Slot slot : themeSlots)
        hash.addData(theme.color(slot).name(QColor::HexArgb).toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

EmbedRenderService::EmbedRenderService(QObject *parent) : QObject(parent) {}
EmbedRenderService::~EmbedRenderService() = default;

}  // namespace Markoff
