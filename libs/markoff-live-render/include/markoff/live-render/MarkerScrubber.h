// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/Marker.h>

#include <QObject>
#include <QPointer>
#include <QString>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::LiveRender {

class LiveBlockModel;

/// Marker-paragraph design (`docs/specs/2026-05-03-marker-paragraph-design.md` §6).
/// Removes leaked U+200B markers from source at three deterministic events:
/// focus-out from a marker-only paragraph, pre-save flush, post-load cleanup.
class MARKOFF_LIVE_RENDER_EXPORT MarkerScrubber : public QObject {
    Q_OBJECT
public:
    explicit MarkerScrubber(Markoff::MarkoffDocument *doc,
                            LiveBlockModel           *model,
                            QObject                  *parent = nullptr);

    /// True iff `text` consists exclusively of marker characters and
    /// soft-break newlines (and is non-empty). Spec §6.2 + §17 open
    /// question 1.
    static bool isMarkerOnly(const QString &text);

    /// Called by LiveEditBinding when focus leaves a paragraph whose
    /// content currently matches `isMarkerOnly`. Emits one
    /// `applyLocalEdit` removing the marker paragraph + its leading
    /// `\n\n` separator. No-op if `blockIndex` is out of range or the
    /// block is no longer marker-only.
    void scrubOnFocusOut(int blockIndex);

    /// Called by the host's save handler before serializing bytes.
    /// Walks all paragraph rows; collects every marker-only paragraph
    /// (or run of them); applies one batched `applyLocalEdit` removing
    /// them. Returns the number of marker bytes removed.
    int scrubBeforeSave();

    /// Called from `MarkoffDocument::documentReloaded`. Same logic as
    /// `scrubBeforeSave` but for the just-loaded document. Defends
    /// against marker-bearing files written by other tools.
    int scrubAfterLoad();

private:
    QPointer<Markoff::MarkoffDocument> m_doc;
    QPointer<LiveBlockModel>           m_model;
};

}  // namespace Markoff::LiveRender
