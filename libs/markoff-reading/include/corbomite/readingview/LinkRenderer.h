// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_LINKRENDERER_H
#define CORBOMITE_READINGVIEW_LINKRENDERER_H

#include <QObject>
#include <QPoint>
#include <QString>
#include <QUrl>

namespace Corbomite::Core {
class VaultResourceProvider;
}

namespace Corbomite::ReadingView {

/// Typed inline-link-emission surface for ReadingView's section-mount
/// pipeline. Sibling to Markoff::LinkRenderer — same contract shape,
/// readingview namespace. Emitters pass an honest `sourceId`
/// (e.g. "markoff:reading") so downstream HoverLinkSource consumers
/// can route by source without string-guessing.
///
/// No "bases" hardcode — compat aliasing is a Cluster N shim concern.
class LinkRenderer : public QObject
{
    Q_OBJECT

public:
    /// Shared request shape for wikilink / internal-link emissions.
    struct FileLinkRequest
    {
        QString linkText;  ///< wikilink "Note#heading|Alias" or plain target
        QString fromPath;  ///< note containing the link
        QString sourceId;  ///< honest caller identifier, e.g. "markoff:reading"
        QPoint anchorHint; ///< optional: pixel anchor for hover emission
    };

    explicit LinkRenderer(Corbomite::Core::VaultResourceProvider *resources,
                          QObject *parent = nullptr);

    /// Wikilink / internal-link emission. Resolves `linkText` via the
    /// VaultResourceProvider when one was supplied; falls back to the
    /// raw text otherwise. Emits `linkHovered`.
    void emitFileLink(const FileLinkRequest &req);

    /// Tag-link emission (`#tag`). Emits `tagHovered`.
    void emitTagLink(const QString &tag, const QString &sourceId);

    /// External-link emission (`http://...`). Emits `externalLinkActivated`.
    void emitExternalLink(const QUrl &url, const QString &sourceId);

    /// Click-path companion to `emitFileLink`. Emits `linkClicked`.
    void emitFileLinkClicked(const QString &target, const QString &sourceId);

Q_SIGNALS:
    void linkHovered(const QString &target,
                     const QString &sourceId,
                     const QPoint &anchorHint);
    void linkClicked(const QString &target, const QString &sourceId);
    void tagHovered(const QString &tag, const QString &sourceId);
    void externalLinkActivated(const QUrl &url, const QString &sourceId);

private:
    Corbomite::Core::VaultResourceProvider *m_resources;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_LINKRENDERER_H
