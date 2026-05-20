// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>

#include <markoff/core/FindController.h>

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;

namespace Detail {

/// Internal. Subscribes to a Markoff::FindController and responds to
/// matches/navigation in the live leaf's idiom.
///
/// - matchesChanged: currently a no-op; highlight rendering across
///   delegates is a follow-up — adapter holds the match list for that
///   future renderer to consume.
/// - navigationRequested: resolves match.block -> ListView row via
///   LiveBlockModel, places the caret via
///   LiveCursorState::setCaretWithoutFocus (no focus stealing).
class LiveFindAdapter : public QObject {
    Q_OBJECT
public:
    explicit LiveFindAdapter(LiveBlockModel *model,
                             LiveCursorState *cursorState,
                             QObject *parent = nullptr);
    ~LiveFindAdapter() override;

    void attach(Markoff::FindController *fc);
    void detach();

    Markoff::FindController *controller() const { return m_controller; }

private slots:
    void onNavigationRequested(Markoff::FindController::Match);

private:
    int  resolveByteToQtPos(const Markoff::BlockAnchor &block, quint32 byteOffset) const;

    LiveBlockModel                       *m_model       = nullptr;
    LiveCursorState                      *m_cursorState = nullptr;
    QPointer<Markoff::FindController>     m_controller;
};

}  // namespace Detail
}  // namespace Markoff::Live
