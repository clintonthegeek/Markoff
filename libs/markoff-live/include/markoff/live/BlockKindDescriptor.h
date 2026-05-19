// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/core/BlockAttrsMap.h>

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

namespace Markoff::Live {

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
struct MARKOFF_LIVE_EXPORT BlockKindDescriptor {
    QString     id;          ///< "paragraph", "heading", etc.
    /// qrc: URL of the QML delegate. Populated for built-in kinds as
    /// a queryable annotation (debuggers, DevTools, future plugin-kind
    /// machinery); NOT consumed by LiveView.qml's DelegateChooser, which
    /// dispatches on `delegateClass`. Plugin-kind dynamic dispatch is a
    /// future spec that would wire this through a Loader-based selector.
    QString delegateUrl;

    /// Which Cursor variants this kind admits: "TextCaret", "BlockSelected",
    /// "BlockInternalEdit". Validated by LiveCursorState in R3.
    QSet<QString> supportedCursorVariants;

    /// Internal-edit mode tokens (e.g. {"editing-latex"} for math). R8.
    QStringList internalEditModes;

    /// True for text-bearing kinds (paragraph, heading, code-block).
    /// False for hr, image.
    bool acceptsTextRoleUpdates = false;

    /// Whether this kind cannot host a text caret. Derived from
    /// `supportedCursorVariants` in practice — except for transitional
    /// kinds (currently Math) where the explicit flag retains current
    /// behaviour pending the kind's text-bearing redesign (E5 for Math).
    bool isBlockOnly = false;

    /// Context-menu actions registered for this kind. R9.
    QStringList contextMenuActions;

    /// Structural keys this kind consumes (Qt::Key_* values). R5 dispatch.
    QSet<int> consumedStructuralKeys;

    /// Serializer: (text, attrs) → markdown bytes.
    /// Null = passthrough (returns text unchanged).
    std::function<QByteArray(const QByteArray &text,
                              const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs)>
        serializer;
};

}  // namespace Markoff::Live
