// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QSet>
#include <QString>
#include <QStringList>

namespace Markoff::LiveRender {

/// Static metadata for a block kind. Registered in BlockKindRegistry.
/// Fields default to empty/false in R2; populated as later phases add
/// cursor (R3), structural keys (R5), and context menu (R9) machinery.
///
/// `delegateUrl` is the QRC URL of the QML delegate file. Populated for
/// built-in kinds but not consulted by LiveView.qml in R2 (DelegateChooser
/// hardcodes the five built-in choices). Plugin-registered kinds use this
/// URL for dynamic Loader dispatch in R3+.
///
/// Spec §5.1.
struct MARKOFF_LIVE_RENDER_EXPORT BlockKindDescriptor {
    QString     id;          ///< "paragraph", "heading", etc.
    QString     delegateUrl; ///< qrc: URL of the QML delegate (R3+ dispatch).

    /// Which Cursor variants this kind admits: "TextCaret", "BlockSelected",
    /// "BlockInternalEdit". Validated by LiveCursorState in R3.
    QSet<QString> supportedCursorVariants;

    /// Internal-edit mode tokens (e.g. {"editing-latex"} for math). R8.
    QStringList internalEditModes;

    /// True for text-bearing kinds (paragraph, heading, code-block).
    /// False for hr, image.
    bool acceptsTextRoleUpdates = false;

    /// Context-menu actions registered for this kind. R9.
    QStringList contextMenuActions;

    /// Structural keys this kind consumes (Qt::Key_* values). R5 dispatch.
    QSet<int> consumedStructuralKeys;
};

}  // namespace Markoff::LiveRender
