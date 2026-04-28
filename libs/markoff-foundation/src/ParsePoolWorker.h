// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>

namespace Markoff { class Document; }  // markoff-parser

namespace Markoff::Parse::Detail {

/// Private worker object that lives on ParsePool's worker thread. Receives
/// parseSnapshot() invocations via QueuedConnection from ParsePool and emits
/// parsed() with the result. Never used outside of ParsePool.cpp.
class ParsePoolWorker : public QObject {
    Q_OBJECT
public:
    ParsePoolWorker() = default;

public Q_SLOTS:
    void parseSnapshot(QByteArray utf8, quint64 generation);

Q_SIGNALS:
    void parsed(Markoff::Document *result, quint64 generation);
};

}  // namespace Markoff::Parse::Detail
