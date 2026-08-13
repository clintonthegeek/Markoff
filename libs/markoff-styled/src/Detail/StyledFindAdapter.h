// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTextEdit>

#include <markoff/core/FindController.h>

namespace Markoff::Styled {

class Editor;

namespace Detail {

/// Internal. Subscribes to a Markoff::FindController and translates
/// (BlockAnchor, byteOffset, byteLength) matches into
/// QTextEdit::ExtraSelections rendered on the styled QTextEdit, plus a
/// non-focus-stealing setTextCursor on navigationRequested.
///
/// Position mapping is FRAME-AWARE: it runs Styled::walkBlocks (the same
/// lockstep walk FormatPass consumes) instead of flat-byte arithmetic —
/// a rendered QTextTable frame occupies far fewer document positions than
/// its pipe source has bytes, so byte arithmetic overruns everything
/// after a table (the 2026-05-31 SIGSEGV class). Matches INSIDE a table
/// frame are a documented degradation: the match's byte offsets index the
/// raw pipe source, which has no positional counterpart in the compact
/// frame, so they are counted by the controller but get no highlight;
/// navigation scrolls to the frame (caret parked at its first position).
///
/// Stale-handle safety: WalkEntries / QTextBlocks are never cached as
/// members — every render/navigation re-walks the live document (a
/// d2DocumentChanged re-render may rebuild frames and blocks wholesale).
class StyledFindAdapter : public QObject {
    Q_OBJECT
public:
    /// Visible-document (start, length) in QChar positions for a match,
    /// or {-1, 0} when unmappable (table frame, desync, unknown block).
    struct MappedSpan {
        int start  = -1;
        int length = 0;
    };

    explicit StyledFindAdapter(Editor *editor, QObject *parent = nullptr);
    ~StyledFindAdapter() override;

    void attach(Markoff::FindController *fc);
    void detach();

private slots:
    void onMatchesChanged();
    void onNavigationRequested(Markoff::FindController::Match);

private:
    void renderHighlights();

    Editor                            *m_editor;
    QPointer<Markoff::FindController>  m_controller;
    QMetaObject::Connection            m_docChangedCon;
    QMetaObject::Connection            m_themeChangedCon;
    QList<QTextEdit::ExtraSelection>   m_highlights;
};

}  // namespace Detail
}  // namespace Markoff::Styled
