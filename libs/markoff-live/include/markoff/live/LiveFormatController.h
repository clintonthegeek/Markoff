// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <QObject>

namespace Markoff { class MarkoffDocument; }

namespace Markoff::Live {

class LiveCursorState;
class LiveBlockModel;

/// Per-block inline format controller: bold/italic/link toggles.
///
/// `wrapPerBlock` applies symmetric open/close delimiters to each selected
/// block's selected sub-range independently, processing blocks in REVERSE
/// order so byte offsets of earlier edits are not disturbed. If the selected
/// range is already bounded by the delimiters, they are removed (toggle/unwrap).
class MARKOFF_LIVE_EXPORT LiveFormatController : public QObject {
    Q_OBJECT
public:
    explicit LiveFormatController(QObject *parent = nullptr);

    void setDocument(Markoff::MarkoffDocument *doc);
    void setSelectionView(LiveCursorState *sv);
    void setModel(const LiveBlockModel *model);

    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void insertLink();

private:
    void wrapPerBlock(const QByteArray &openDelim, const QByteArray &closeDelim);

    Markoff::MarkoffDocument *m_document  = nullptr;
    LiveCursorState          *m_selection = nullptr;
    const LiveBlockModel     *m_model     = nullptr;
};

}  // namespace Markoff::Live
