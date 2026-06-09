// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

/// Shared by the styled-table test binaries: first QTextTable frame under
/// the document's root frame, or nullptr if none has been materialized.
inline QTextTable *firstTable(QTextDocument *doc) {
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f)) return t;
    return nullptr;
}
