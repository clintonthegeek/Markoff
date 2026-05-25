// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>

#include <QObject>
#include <QtTypes>

namespace Markoff {

/// Lightweight QObject proxy wrapping a single block's CollabText::Crdt::Buffer.
/// Connect to its signals to track per-block text changes without including CRDT headers.
/// Created and owned exclusively by MarkoffDocument — do not construct directly.
class MARKOFF_CORE_EXPORT BufferProxy : public QObject {
    Q_OBJECT
public:
    /// The blockId this proxy represents.
    BlockId blockId() const { return m_blockId; }
    /// Monotonic counter — increments each time the buffer content changes.
    quint64 editSequence() const { return m_editSequence; }

    /// @private — for MarkoffDocument use only.
    explicit BufferProxy(BlockId blockId, QObject *parent = nullptr);
    /// @private — for MarkoffDocument use only.
    void notifyChanged();

Q_SIGNALS:
    /// Emitted (on the thread that called applyBlockEdit) when this block's text changes.
    void inlineSpansChanged();

private:
    BlockId m_blockId;
    quint64 m_editSequence = 0;
};

/// QObject proxy for the IdList (structural order CRDT).
/// Created and owned exclusively by MarkoffDocument — do not construct directly.
class MARKOFF_CORE_EXPORT IdListProxy : public QObject {
    Q_OBJECT
public:
    quint64 editSequence() const { return m_editSequence; }

    /// @private — for MarkoffDocument use only.
    explicit IdListProxy(QObject *parent = nullptr);
    /// @private — for MarkoffDocument use only.
    void notifyChanged();

Q_SIGNALS:
    /// Emitted when blocks are inserted or removed (structural change).
    void structureChanged();

private:
    quint64 m_editSequence = 0;
};

/// QObject proxy for any sibling CausalLwwMap (KindTagMap, BlockAttrsMap, etc.).
/// Template-erased: callers just need to know "something in this map changed."
/// Created and owned exclusively by MarkoffDocument — do not construct directly.
class MARKOFF_CORE_EXPORT SiblingMapProxy : public QObject {
    Q_OBJECT
public:
    quint64 editSequence() const { return m_editSequence; }

    /// @private — for MarkoffDocument use only.
    explicit SiblingMapProxy(QObject *parent = nullptr);
    /// @private — for MarkoffDocument use only.
    void notifyChanged();

Q_SIGNALS:
    /// Emitted when any entry in the map changes.
    void mapChanged();

private:
    quint64 m_editSequence = 0;
};

}  // namespace Markoff
