// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>

#include "IncrementalParseSession.h"

namespace Markoff { class Document; }  // markoff-parser

namespace Markoff::Parse::Detail {

/// Private worker object that lives on ParsePool's worker thread. Receives
/// parseSnapshot() / parseReset() invocations via QueuedConnection from
/// ParsePool and emits parsed() with the snapshotted Document.
///
/// Owns one IncrementalParseSession across calls, so each parse re-uses
/// the prior tree via tree-sitter's incremental API.
class ParsePoolWorker : public QObject {
    Q_OBJECT
public:
    ParsePoolWorker() = default;

public Q_SLOTS:
    /// Incremental reparse against `utf8` (the new full document body).
    void parseSnapshot(QByteArray utf8, quint64 generation);

    /// Drop session state and full-parse `utf8`. Used for resetContent
    /// (file load, external reload, revert-to-saved, test fixture).
    void parseReset(QByteArray utf8, quint64 generation);

Q_SIGNALS:
    void parsed(Markoff::Document *result, quint64 generation);

private:
    IncrementalParseSession m_session;
};

}  // namespace Markoff::Parse::Detail
