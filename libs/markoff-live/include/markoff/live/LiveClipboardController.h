// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QObject>
#include <functional>

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

    /// Read-only gate source (contract-v2 spec §4.2). The provider reads
    /// LiveListModelBinding's `readOnly` flag live (single authority —
    /// this header is included by the binding's, so no back-include).
    /// While it returns true, cut/paste/pastePrimary/pasteText early-
    /// return; copy() stays available.
    void setReadOnlyProvider(std::function<bool()> provider);

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();

    /// X11/Wayland PRIMARY-selection paste (middle-click). Safe no-op
    /// on platforms where the clipboard does not support a separate
    /// selection buffer. See audit L2 spec.
    Q_INVOKABLE void pastePrimary();

    /// Insert the given text at the current cursor position (or replace
    /// the current selection's range with it). Used by the drag-drop
    /// path (audit L3) where the text comes from a QDropEvent's
    /// QMimeData rather than the system clipboard.
    Q_INVOKABLE void pasteText(const QString &text);

    static constexpr const char *kBlocksMime = "application/x-markoff-blocks";

private:
    void pasteFrom(int clipboardMode);  // QClipboard::Mode as int to
                                        // avoid leaking the include
                                        // into the public header.
    bool resolveSelectionByteRange(uint32_t &startByte, uint32_t &endByte) const;
    bool resolveSelectionRange(uint32_t &startByte, uint32_t &endByte,
                              int &firstRow, int &firstQtPos, int &lastRow) const;

    /// Advance the caret to just past a single-line, same-block paste's
    /// inserted text (queue #10 item 3: no code path previously advanced
    /// the caret after a flat-text paste at all — see docs/queue.md).
    /// Scope: `firstRow == lastRow` (collapsed cursor or a same-block
    /// selection) and `insertedText` contains no '\n' (so the block count
    /// is unchanged and row indices stay valid after the model rebuild).
    /// Cross-block or structured (multi-block) pastes are unaffected —
    /// known remaining gap, see queue.md.
    void advanceCaretPastPaste(int firstRow, int lastRow, int firstQtPos,
                               const QString &insertedText);

    /// Insert `insertedText` at [startByte, endByte), routing a collapsed
    /// single-block cursor through the unambiguous block+offset primitive
    /// instead of applyFlatEdit's boundary-ambiguous global byte range
    /// (see the .cpp for why). Clears the selection either way.
    void insertAtOrReplace(uint32_t startByte, uint32_t endByte,
                           int firstRow, int lastRow, int firstQtPos,
                           const QString &insertedText);

    bool isReadOnly() const {
        return m_readOnlyProvider && m_readOnlyProvider();
    }

    Markoff::MarkoffDocument *m_document  = nullptr;
    LiveCursorState          *m_selection = nullptr;
    const LiveBlockModel     *m_model     = nullptr;
    std::function<bool()>     m_readOnlyProvider;
};

}  // namespace Markoff::Live
