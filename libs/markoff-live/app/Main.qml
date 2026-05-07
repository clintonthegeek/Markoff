// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

ApplicationWindow {
    id: window
    width: 900
    height: 700
    visible: true
    title: ctxTitle + " — markoff-live (R5.5)"

    LiveListModelBinding {
        id: modelBinding
        document: ctxDocument
    }

    LiveView {
        anchors.fill: parent
        binding: modelBinding
        focus: true
    }
}
