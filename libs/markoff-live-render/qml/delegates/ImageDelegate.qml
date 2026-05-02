// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only image delegate. In R2, topLevelBlocks() does not extract
/// imageSrc from image syntax, so the source markdown text is shown as a
/// styled placeholder. Image URL extraction and rendering land in R6+.
TextEdit {
    width: ListView.view ? ListView.view.width : 600
    readOnly: true
    textFormat: TextEdit.PlainText
    text: model.text
    wrapMode: TextEdit.Wrap
    leftPadding: 8; rightPadding: 8
    topPadding: 4; bottomPadding: 4
    font.pixelSize: 13
    color: palette.placeholderText
}
