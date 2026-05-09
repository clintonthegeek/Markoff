// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

ApplicationWindow {
    id: window
    width: 900
    height: 700
    visible: true
    title: ctxMain.title

    LiveListModelBinding {
        id: modelBinding
        document: ctxDocument
        Component.onCompleted: {
            selectionView.setSession(ctxSession)
            if (actionController)
                actionController.saveRequested.connect(ctxMain.save)
        }
    }

    LiveView {
        anchors.fill: parent
        binding: modelBinding
        focus: true
    }
}
