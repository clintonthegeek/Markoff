// SPDX-License-Identifier: GPL-3.0-or-later
// Lifecycle: deleted in R5.5 Task 3; this stub exists solely to gate
// the LiveRealisticInputHarness against a v0-style reify-on-first-
// keystroke race. Once tst_live_render_holes_gate proves the harness
// catches the scramble, the stub has no further purpose.
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
                    // Double-deferred per plan Step 6: a single Qt.callLater closes the
                    // no-TextEdit window before a 30 ms inter-keystroke gap can land in
                    // it. The nested chain widens the dead window across two event-loop
                    // turns so the harness reliably observes dropped characters.
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
