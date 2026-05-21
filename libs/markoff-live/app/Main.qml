// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.markoff.live 1.0

ApplicationWindow {
    id: window
    width: 900
    height: 700
    visible: true
    color: (modelBinding.theme)
           ? modelBinding.themeColorFor(Theme.EditorBackground)
           : "#ffffff"
    title: ctxMain.title

    LiveListModelBinding {
        id: modelBinding
        document: ctxDocument
        Component.onCompleted: {
            modelBinding.setSession(ctxSession)
            modelBinding.setLinkService(ctxMain.linkService)
            modelBinding.setFromContext(ctxMain.filePath)
            if (actionController) {
                actionController.saveRequested.connect(ctxMain.save)
                actionController.themeToggleRequested.connect(modelBinding.applyDefaultTheme)
            }
        }
    }

    // ---- Standalone-app editor shortcuts ----
    //
    // These bind the LiveActionController QActions to keyboard chords at
    // window scope, so the standalone markoff-live-app responds to
    // Bold/Italic/Heading/etc. without the host needing to wire them.
    // Consumer hosts (e.g. Corbomite) register their own KActions with
    // these chords and consume the events at the application level,
    // which is why these Shortcuts live in the app (not in LiveView.qml
    // inside the library) — embedding the library inside a consumer host
    // would otherwise create chord ambiguity.

    function _ac() { return modelBinding ? modelBinding.actionController : null }

    Shortcut {
        sequence: StandardKey.Bold
        enabled: !!_ac()
        onActivated: _ac().boldAction.trigger()
    }
    Shortcut {
        sequence: StandardKey.Italic
        enabled: !!_ac()
        onActivated: _ac().italicAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+Shift+X"
        enabled: !!_ac()
        onActivated: _ac().strikeAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: !!_ac()
        onActivated: _ac().inlineCodeAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+K"
        enabled: !!_ac()
        onActivated: _ac().linkAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: !!_ac()
        onActivated: _ac().heading1Action.trigger()
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: !!_ac()
        onActivated: _ac().heading2Action.trigger()
    }
    Shortcut {
        sequence: "Ctrl+3"
        enabled: !!_ac()
        onActivated: _ac().heading3Action.trigger()
    }
    Shortcut {
        sequence: "Ctrl+4"
        enabled: !!_ac()
        onActivated: _ac().heading4Action.trigger()
    }
    Shortcut {
        sequence: "Ctrl+5"
        enabled: !!_ac()
        onActivated: _ac().heading5Action.trigger()
    }
    Shortcut {
        sequence: "Ctrl+6"
        enabled: !!_ac()
        onActivated: _ac().heading6Action.trigger()
    }
    Shortcut {
        sequence: StandardKey.Save
        enabled: !!_ac()
        onActivated: _ac().saveAction.trigger()
    }

    // Update fromContext whenever the controller loads a new document.
    Connections {
        target: ctxMain
        function onFromContextChanged(path) {
            modelBinding.setFromContext(path)
        }
    }

    // Wire openRequested to in-place document load.
    Connections {
        target: ctxMain.linkService
        function onOpenRequested(path, section, blockRef) {
            ctxMain.loadDocumentFromPath(path)
            if (section !== "")
                console.log("[link] navigate to section:", section)
            if (blockRef !== "")
                console.log("[link] navigate to block:", blockRef)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        LiveView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            binding: modelBinding
            focus: true
        }

        // Status bar: shows hover/link status messages from the link service.
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            height: ctxMain.statusMessage !== "" ? statusLabel.implicitHeight + 6 : 0
            visible: ctxMain.statusMessage !== ""
            color: (modelBinding.theme)
                   ? modelBinding.themeColorFor(Theme.CodeBlockBackground)
                   : "#f0f0f0"

            Text {
                id: statusLabel
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                anchors.leftMargin: 8
                text: ctxMain.statusMessage
                color: (modelBinding.theme)
                       ? modelBinding.themeColorFor(Theme.TextDefault)
                       : "#222222"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
    }
}
