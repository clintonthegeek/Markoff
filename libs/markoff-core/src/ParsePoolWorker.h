// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace Markoff {

class MarkoffDocument;
class Document;

/// Private worker object that lives on ParsePool's worker thread.
/// Receives parseSnapshot() invocations via QueuedConnection from ParsePool
/// and emits parsed() with the result. Never used outside of ParsePool.cpp.
class ParsePoolWorker : public QObject {
    Q_OBJECT
public:
    ParsePoolWorker() = default;

public slots:
    void parseSnapshot(Markoff::MarkoffDocument *sender,
                       QString snapshot,
                       quint64 generation);

Q_SIGNALS:
    void parsed(Markoff::MarkoffDocument *sender,
                Markoff::Document *result,
                quint64 generation);
};

} // namespace Markoff
