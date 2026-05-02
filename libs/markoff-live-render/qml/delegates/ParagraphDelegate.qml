// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only paragraph. Uses PlainText so the delegate structure is
/// correct for R4 when LiveEditBinding + InlineFormatHighlighter land.
/// Inline formatting (bold/italic etc.) is not rendered in R2 — that
/// requires InlineFormatHighlighter (R6).
TextEdit {
    width: ListView.view ? ListView.view.width : 600
    readOnly: true
    textFormat: TextEdit.PlainText
    text: model.text
    wrapMode: TextEdit.Wrap
    leftPadding: 8; rightPadding: 8
    topPadding: 4; bottomPadding: 4
    font.pixelSize: 14
    color: palette.text
}
