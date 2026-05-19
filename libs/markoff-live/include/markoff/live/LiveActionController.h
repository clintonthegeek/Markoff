// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <QObject>
#include <QAction>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Live {

class LiveCursorState;
class LiveClipboardController;
class LiveFormatController;
class LiveListModelBinding;

class MARKOFF_LIVE_EXPORT LiveActionController : public QObject {
    Q_OBJECT
    // Q_PROPERTY for each action (CONSTANT since pointers don't change)
    Q_PROPERTY(QAction *cutAction       READ cutAction       CONSTANT)
    Q_PROPERTY(QAction *copyAction      READ copyAction      CONSTANT)
    Q_PROPERTY(QAction *pasteAction     READ pasteAction     CONSTANT)
    Q_PROPERTY(QAction *selectAllAction READ selectAllAction CONSTANT)
    Q_PROPERTY(QAction *deleteAction    READ deleteAction    CONSTANT)
    Q_PROPERTY(QAction *undoAction      READ undoAction      CONSTANT)
    Q_PROPERTY(QAction *redoAction      READ redoAction      CONSTANT)
    Q_PROPERTY(QAction *boldAction      READ boldAction      CONSTANT)
    Q_PROPERTY(QAction *italicAction    READ italicAction    CONSTANT)
    Q_PROPERTY(QAction *linkAction      READ linkAction      CONSTANT)
    Q_PROPERTY(QAction *saveAction      READ saveAction      CONSTANT)
    Q_PROPERTY(QAction *zoomInAction    READ zoomInAction    CONSTANT)
    Q_PROPERTY(QAction *zoomOutAction   READ zoomOutAction   CONSTANT)
    Q_PROPERTY(QAction *zoomResetAction READ zoomResetAction CONSTANT)
    Q_PROPERTY(QAction *toggleDarkAction READ toggleDarkAction CONSTANT)

public:
    explicit LiveActionController(QObject *parent = nullptr);

    void setDocument(Markoff::MarkoffDocument *doc);
    void setSelectionView(LiveCursorState *sv);
    void setClipboardController(LiveClipboardController *cc);
    void setFormatController(LiveFormatController *fc);
    void setBinding(LiveListModelBinding *b);

    QAction *cutAction()       const { return m_cut; }
    QAction *copyAction()      const { return m_copy; }
    QAction *pasteAction()     const { return m_paste; }
    QAction *selectAllAction() const { return m_selectAll; }
    QAction *deleteAction()    const { return m_delete; }
    QAction *undoAction()      const { return m_undo; }
    QAction *redoAction()      const { return m_redo; }
    QAction *boldAction()      const { return m_bold; }
    QAction *italicAction()    const { return m_italic; }
    QAction *linkAction()      const { return m_link; }
    QAction *saveAction()      const { return m_save; }
    QAction *zoomInAction()    const { return m_zoomIn; }
    QAction *zoomOutAction()   const { return m_zoomOut; }
    QAction *zoomResetAction() const { return m_zoomReset; }
    QAction *toggleDarkAction() const { return m_toggleDark; }

Q_SIGNALS:
    void saveRequested();
    void themeToggleRequested(bool dark);

public Q_SLOTS:
    void updateEnabledStates();

private Q_SLOTS:
    void onClipboardChanged();

private:
    void setupActions();

    Markoff::MarkoffDocument  *m_document   = nullptr;
    LiveCursorState           *m_selection  = nullptr;
    LiveClipboardController   *m_clipboard  = nullptr;
    LiveFormatController      *m_format     = nullptr;
    LiveListModelBinding      *m_binding    = nullptr;

    QAction *m_cut       = nullptr;
    QAction *m_copy      = nullptr;
    QAction *m_paste     = nullptr;
    QAction *m_selectAll = nullptr;
    QAction *m_delete    = nullptr;
    QAction *m_undo      = nullptr;
    QAction *m_redo      = nullptr;
    QAction *m_bold      = nullptr;
    QAction *m_italic    = nullptr;
    QAction *m_link      = nullptr;
    QAction *m_save      = nullptr;
    QAction *m_zoomIn    = nullptr;
    QAction *m_zoomOut   = nullptr;
    QAction *m_zoomReset = nullptr;
    QAction *m_toggleDark = nullptr;
    bool     m_isDark     = false;
};

}  // namespace Markoff::Live
