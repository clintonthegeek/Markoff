// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LINKRENDERER_H
#define MARKOFF_LINKRENDERER_H

#include <QObject>
#include <QPoint>
#include <QString>
#include <QUrl>

namespace Markoff {

/// Typed emission surface for inline link/tag/external-link activations
/// and hovers originating in the Markoff editor pipeline.
///
/// Every caller passes an honest `sourceId` string identifying the emitter
/// ("markoff:editor", "markoff:livepreview", "markoff:reading"). The
/// Corbomite-audit constraint is: no hover-link source string is ever
/// fabricated as `"bases"` (that value is reserved for the Bases plugin
/// surface in Cluster N). A compile-time + test-time gate enforces this.
///
/// LinkRenderer is intentionally resolver-free. It forwards raw targets
/// through signals so downstream consumers (the host app) decide how to
/// navigate / preview / resolve. Resolution is a concern for the app
/// layer, not the Markoff library.
class LinkRenderer : public QObject
{
    Q_OBJECT
public:
    explicit LinkRenderer(QObject *parent = nullptr);

    struct FileLinkRequest {
        QString linkText;           ///< wikilink "Note#heading|Alias" or plain text
        QString fromPath;           ///< path of the note containing the link
        QString sourceId;           ///< honest caller identifier
        QPoint  anchorHint;         ///< optional pixel anchor for hover popovers
    };

    /// Emit a wiki-link / note-internal link click.
    void emitFileLinkActivated(const FileLinkRequest &req);

    /// Emit a wiki-link / note-internal link hover. Pass an empty linkText
    /// to signal "hover left" — subscribers receive an empty `target`.
    void emitFileLinkHovered(const FileLinkRequest &req);

    /// Emit a tag hover (e.g. mouseover on `#tag`).
    void emitTagHovered(const QString &tag, const QString &sourceId);

    /// Emit an external link click (standard markdown `[text](url)`).
    void emitExternalLinkActivated(const QUrl &url, const QString &sourceId);

    /// Emit an external link hover.
    void emitExternalLinkHovered(const QUrl &url, const QString &sourceId);

Q_SIGNALS:
    void fileLinkActivated(const QString &target, const QString &sourceId,
                           const QString &fromPath);
    void fileLinkHovered(const QString &target, const QString &sourceId,
                         const QPoint &anchorHint);
    void tagHovered(const QString &tag, const QString &sourceId);
    void externalLinkActivated(const QUrl &url, const QString &sourceId);
    void externalLinkHovered(const QUrl &url, const QString &sourceId);
};

} // namespace Markoff

#endif // MARKOFF_LINKRENDERER_H
