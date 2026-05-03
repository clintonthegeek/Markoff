// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 400
    height: 30
    property string allDeliveredText: ""
    property bool firstKeyHandled: false

    Loader {
        id: textEditLoader
        anchors.fill: parent
        sourceComponent: textEditComponent
    }

    Component {
        id: textEditComponent
        TextEdit {
            id: textEdit
            objectName: "innerTextEdit"
            focus: true
            Component.onCompleted: forceActiveFocus()

            // v0-mimic: on first keystroke, destroy + recreate across two
            // deferred turns so the "no TextEdit" window outlasts a 30 ms
            // inter-keystroke gap in LiveRealisticInputHarness.
            Keys.onPressed: function(event) {
                if (!root.firstKeyHandled && event.text.length > 0) {
                    root.firstKeyHandled = true;
                    root.allDeliveredText += event.text;
                    Qt.callLater(function() {
                        textEditLoader.active = false;
                        Qt.callLater(function() {
                            textEditLoader.active = true;
                        });
                    });
                    return;
                }
                root.allDeliveredText += event.text;
            }
        }
    }
}
