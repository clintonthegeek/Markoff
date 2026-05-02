// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only heading. Font size driven by headingLevel (1–6).
TextEdit {
    width: ListView.view ? ListView.view.width : 600
    readOnly: true
    textFormat: TextEdit.PlainText
    text: model.text
    wrapMode: TextEdit.Wrap
    leftPadding: 8; rightPadding: 8
    topPadding: 6; bottomPadding: 2
    font.pixelSize: {
        switch (model.headingLevel) {
            case 1: return 28
            case 2: return 24
            case 3: return 20
            case 4: return 18
            case 5: return 16
            default: return 14
        }
    }
    font.bold: model.headingLevel <= 3
    color: palette.text
}
