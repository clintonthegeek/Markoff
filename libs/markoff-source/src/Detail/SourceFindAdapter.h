// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTextEdit>

#include <markoff/core/FindController.h>

namespace Markoff::Source {

class Editor;

namespace Detail {

/// Internal. Subscribes to a Markoff::FindController and translates
/// (BlockAnchor, byteOffset, byteLength) matches into flat
/// QTextEdit::ExtraSelections rendered on the underlying
/// QPlainTextEdit, plus a non-focus-stealing setTextCursor on
/// navigationRequested.
class SourceFindAdapter : public QObject {
    Q_OBJECT
public:
    explicit SourceFindAdapter(Editor *editor, QObject *parent = nullptr);
    ~SourceFindAdapter() override;

    void attach(Markoff::FindController *fc);
    void detach();

private slots:
    void onMatchesChanged();
    void onNavigationRequested(Markoff::FindController::Match);

private:
    int globalCharPosFor(Markoff::FindController::Match) const;
    void renderHighlights();

    Editor                                *m_editor;
    QPointer<Markoff::FindController>      m_controller;
    QList<QTextEdit::ExtraSelection>       m_highlights;
};

}  // namespace Detail
}  // namespace Markoff::Source
