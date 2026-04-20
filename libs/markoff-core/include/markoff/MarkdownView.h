// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QUrl>
#include <QVector>
#include <QWidget>

#include <markoff/CursorPos.h>
#include <markoff/FoldSpec.h>

namespace Markoff {

class MarkoffDocument;
class SearchAdapter;
class Theme;
class ResourceProvider;
class LinkResolver;

/// Abstract polymorphic base for the three Markoff view widgets
/// (Live Preview, Source, Reading). Host applications hold a
/// MarkdownView* and dispatch through the contract, which keeps
/// mode-swap logic (QStackedWidget + flush/restore) at the host
/// layer rather than inside this library.
///
/// See docs/specs/2026-04-20-tri-view-unified-api-design.md.
class MarkdownView : public QWidget {
    Q_OBJECT
public:
    explicit MarkdownView(QWidget *parent = nullptr) : QWidget(parent) {}

    // Content lives on MarkoffDocument; views attach to it.
    virtual void setDocument(MarkoffDocument *doc) = 0;
    virtual MarkoffDocument *document() const = 0;

    // Appearance / resources (types currently stubbed; real definitions
    // arrive in Phase C when the three Theme/ResourceProvider/LinkResolver
    // implementations consolidate). Named setViewTheme/Resource/Link
    // rather than setTheme/… so they don't collide with leaf widgets'
    // existing same-named setters that take differently-typed concrete
    // arguments.
    virtual void setViewTheme(const Theme &theme) = 0;
    virtual void setViewResourceProvider(ResourceProvider *rp) = 0;
    virtual void setViewLinkResolver(LinkResolver *lr) = 0;

    // Scroll — visual-line float, every mode.
    virtual float scrollPosition() const = 0;
    virtual void setScrollPosition(float visualLine) = 0;

    // Zoom — every mode.
    virtual void zoomIn() = 0;
    virtual void zoomOut() = 0;
    virtual void resetZoom() = 0;

    // Ephemeral state — opaque per-view JSON blob.
    virtual QJsonObject ephemeralState() const = 0;
    virtual void setEphemeralState(const QJsonObject &) = 0;

    // Search — every view supplies one so a SearchController can
    // drive it without knowing the view internals.
    virtual SearchAdapter *searchAdapter() = 0;

    // Capability probes — callers dispatch through these instead of
    // RTTI. Defaults return false; views that support the capability
    // override.
    virtual bool hasCursor() const { return false; }
    virtual bool hasEditing() const { return false; }
    virtual bool hasFold() const { return false; }

    // Optional — default no-ops / capability-denied returns.
    virtual CursorPos cursorPosition() const { return {}; }
    virtual bool setCursorPosition(CursorPos) { return false; }
    virtual bool setReadOnly(bool) { return false; }
    virtual bool isReadOnly() const { return !hasEditing(); }
    virtual QVector<FoldSpec> foldedHeadings() const { return {}; }
    virtual void setFoldedHeadings(const QVector<FoldSpec> &) {}

Q_SIGNALS:
    void scrollPositionChanged(float visualLine);
    void cursorPositionChanged(CursorPos pos);  // emitted only when hasCursor()
    void linkActivated(const QUrl &url);
};

// Theme / ResourceProvider / LinkResolver are forward-declared only.
// Real definitions live in markoff-live today (Theme, ResourceProvider,
// LinkRenderer). Consumers who call setViewTheme() etc. include the
// real headers from wherever they currently live; Phase C consolidates
// them into markoff-core. MarkdownView only needs reference types.

}  // namespace Markoff
