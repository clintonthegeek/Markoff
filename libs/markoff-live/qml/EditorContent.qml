// SPDX-License-Identifier: GPL-3.0-or-later
// EditorContent — minimal QML root for Markoff::Live::EditorWidget hosting.
// Loaded into a QQuickWidget by the C++-side EditorWidget; expects the
// host to push `modelBinding` (a `Markoff::Live::LiveListModelBinding *`)
// into the QML context as a context property.
import QtQuick
import org.markoff.live 1.0

LiveView {
    binding: modelBinding
    focus: true
}
