// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Qt.labs.qmlmodels 1.0

import "delegates"

/// Read-only live render. Displays markdown as a scrollable list of block
/// delegates dispatched by kind. No cursor, selection, or key handling in
/// R2 — those land in R3–R5.
///
/// Usage:
///   LiveListModelBinding { id: binding; document: ctxDocument }
///   LiveView { anchors.fill: parent; binding: binding }
ListView {
    id: root

    required property var binding   // LiveListModelBinding *

    model: binding ? binding.model : null
    clip: true
    spacing: 2

    delegate: DelegateChooser {
        role: "kind"

        DelegateChoice {
            roleValue: "paragraph"
            delegate: ParagraphDelegate {}
        }
        DelegateChoice {
            roleValue: "heading"
            delegate: HeadingDelegate {}
        }
        DelegateChoice {
            roleValue: "code-block"
            delegate: CodeBlockDelegate {}
        }
        DelegateChoice {
            roleValue: "hr"
            delegate: HorizontalRuleDelegate {}
        }
        DelegateChoice {
            roleValue: "image"
            delegate: ImageDelegate {}
        }
    }
}
