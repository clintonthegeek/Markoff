// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.markoff.live.render

Item {
    width: 800
    height: 600

    LiveListModelBinding {
        id: binding
        document: ctxDocument   // injected as context property
    }

    LiveView {
        anchors.fill: parent
        binding: binding
        focus: true
    }
}
