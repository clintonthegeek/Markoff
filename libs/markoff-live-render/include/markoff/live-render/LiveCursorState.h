// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/Cursor.h>

#include <QObject>
#include <QString>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

class BlockKindRegistry;
class LiveBlockModel;

/// Owns the single canonical cursor value for the live view. Validates
/// `request()` calls against the target block's `BlockKindDescriptor`
/// (so BlockSelected is refused on a paragraph, etc.). Emits
/// `cursorChanged()` only when the cursor actually changes. Spec §5.3.
///
/// `cursorKind` Q_PROPERTY exposes the active variant as a string for QML
/// bindings that need to react to focus type (e.g. focus ring vs. caret).
/// Values: "none", "TextCaret", "BlockSelected", "BlockInternalEdit".
class MARKOFF_LIVE_RENDER_EXPORT LiveCursorState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveCursorState is provided by LiveListModelBinding")

    Q_PROPERTY(QString cursorKind READ cursorKind NOTIFY cursorChanged)

public:
    explicit LiveCursorState(const BlockKindRegistry *registry,
                             const LiveBlockModel    *model,
                             QObject                 *parent = nullptr);

    Cursor cursor() const { return m_cursor; }
    QString cursorKind() const;

    void request(const Cursor &newCursor);
    void clear();

    int rowForBlock(const Markoff::BlockAnchor &block) const;

Q_SIGNALS:
    void cursorChanged();

private:
    bool validateVariant(const Cursor &c) const;

    Cursor                   m_cursor;
    const BlockKindRegistry *m_registry;
    const LiveBlockModel    *m_model;
};

}  // namespace Markoff::LiveRender
