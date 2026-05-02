// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.kde.syntaxhighlighting

/// Read-only code block. KSyntaxHighlighting colors the content;
/// language is driven by the fence info-string (`codeLanguage` role).
Rectangle {
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight + 16
    color: Qt.rgba(0, 0, 0, 0.05)
    radius: 4

    TextEdit {
        id: edit
        anchors {
            left: parent.left; right: parent.right
            top: parent.top; bottom: parent.bottom
            margins: 8
        }
        readOnly: true
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: palette.text

        // Only attach when a language is declared; with no language the
        // default text color renders correctly against the tinted background.
        SyntaxHighlighter {
            textEdit: model.codeLanguage.length > 0 ? edit : null
            definition: model.codeLanguage
        }
    }
}
