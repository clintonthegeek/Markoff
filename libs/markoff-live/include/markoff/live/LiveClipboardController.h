// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QObject>

namespace Markoff {
class MarkoffDocument;
}

namespace Markoff::Live {

class LiveCursorState;
class LiveBlockModel;

/// Cut/Copy/Paste controller for the live-preview editor.
///
/// Holds weak references to a MarkoffDocument, LiveCursorState, and
/// LiveBlockModel. All three must be set before calling copy/cut/paste.
///
/// Copy writes two MIME types:
///   - text/plain                       — plain-text fallback
///   - application/x-markoff-blocks     — structured JSON payload
///
/// Cut = Copy + deleteSelection + recordRecentCut.
///
/// Paste routes through the structured path when the clipboard carries
/// application/x-markoff-blocks with version==1; falls back to
/// applyFlatEdit(text/plain) otherwise.
class MARKOFF_LIVE_EXPORT LiveClipboardController : public QObject {
    Q_OBJECT
public:
    explicit LiveClipboardController(QObject *parent = nullptr);

    void setDocument(Markoff::MarkoffDocument *doc);
    void setSelectionView(LiveCursorState *sv);
    void setModel(const LiveBlockModel *model);

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();

    /// X11/Wayland PRIMARY-selection paste (middle-click). Safe no-op
    /// on platforms where the clipboard does not support a separate
    /// selection buffer. See audit L2 spec.
    Q_INVOKABLE void pastePrimary();

    static constexpr const char *kBlocksMime = "application/x-markoff-blocks";

private:
    void pasteFrom(int clipboardMode);  // QClipboard::Mode as int to
                                        // avoid leaking the include
                                        // into the public header.

    Markoff::MarkoffDocument *m_document  = nullptr;
    LiveCursorState          *m_selection = nullptr;
    const LiveBlockModel     *m_model     = nullptr;
};

}  // namespace Markoff::Live
