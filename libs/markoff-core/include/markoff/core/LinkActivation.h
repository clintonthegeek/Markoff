// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUrl>
#include <QtCore/qnamespace.h>

#include <markoff/core/LinkKind.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

struct MARKOFF_CORE_EXPORT LinkActivation {
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;
    QString  fromContext;

    // E3a additions: structured wikilink fields + click modifiers.
    QString  page;
    QString  section;
    QString  blockRef;
    QString  alias;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    // [cluster-k] P2 addition: middle-click-on-a-link "open in a new tab"
    // intent. `modifiers` is Qt::KeyboardModifiers — it can carry Ctrl/
    // Shift/Alt but has no way to represent WHICH MOUSE BUTTON triggered
    // the activation, so a middle-click can't be told apart from a plain
    // click by inspecting `modifiers` alone. Default false so every
    // existing producer/consumer of LinkActivation (aggregate-initialized
    // or field-by-field) is unaffected; a producer that knows the click
    // was a middle-click sets this explicitly (see
    // markoff-canvas/src/View.cpp's `mousePressEvent` MiddleButton branch).
    // Consumers that don't care about new-tab semantics yet (Corbomite's
    // NoteEditorWidget::onLinkActivated does not, as of this addition) are
    // free to ignore the field entirely — it is additive, not a behavior
    // change for anyone not reading it.
    bool openInNewTab = false;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::LinkActivation)
