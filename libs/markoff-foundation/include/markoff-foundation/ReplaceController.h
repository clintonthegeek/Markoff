// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

#include <crdt/Operations.h>

#include <markoff-foundation/BlockId.h>
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
class Session;

class MARKOFF_FOUNDATION_EXPORT ReplaceController : public QObject {
    Q_OBJECT
public:
    explicit ReplaceController(QObject *parent = nullptr);
    ~ReplaceController() override;

    std::optional<CollabText::Crdt::Operation>
        replaceCurrent(MarkoffDocument *, Session *, const QString &replacement);

    struct ReplaceAllResult {
        int count = 0;
        std::optional<CollabText::Crdt::Operation> op;
    };
    ReplaceAllResult
        replaceAll(MarkoffDocument *, Session *, const QString &replacement);

    /// D2: replace bytes [byteStart, byteStart+byteLen) in block `blockId`
    /// with `replacement`. Wrapped in a UndoLog::Transaction. Returns true
    /// on success, false if `blockId` is null or doc is null.
    static bool replaceInBlock(MarkoffDocument &doc,
                               BlockId blockId,
                               uint32_t byteStart,
                               uint32_t byteLen,
                               const QByteArray &replacement);
};

}  // namespace Markoff
