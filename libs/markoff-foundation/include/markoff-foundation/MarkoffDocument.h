// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <memory>
#include <optional>
#include <vector>

#include <crdt/Anchor.h>
#include <crdt/Clock.h>
#include <crdt/Operations.h>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/RenderPhases.h>
#include <markoff-foundation/SessionParams.h>

namespace Markoff {

class Document;       // markoff-parser
class Session;        // forward; defined in Session.h after this task

/// Canonical text + AST + sessions. Owns a CollabText::Crdt::Buffer
/// internally; views are subscribers to this object's signals.
class MARKOFF_FOUNDATION_EXPORT MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    /// Construct an empty document with the given replica ID. ReplicaId is
    /// the CRDT identity for this MarkoffDocument instance; for single-user
    /// use, a random quint16 is fine.
    explicit MarkoffDocument(quint16 replicaId, QObject *parent = nullptr);
    ~MarkoffDocument() override;

    // ===== Reads =====
    QByteArray toMarkdownUtf8() const;        ///< Buffer::text() as QByteArray
    QString    toMarkdown() const;            ///< UTF-8 -> QString convenience
    quint32    visibleLength() const;         ///< UTF-8 byte length

    /// Returns the most-recently parsed Document (from markoff-parser),
    /// or nullptr if no parse has completed yet.
    const Markoff::Document *parsedDocument() const;

    /// True when a parse is currently scheduled or running.
    bool parseIsPending() const;

    // ===== CRDT identity =====
    quint16 replicaId() const;
    CollabText::Crdt::Global version() const;

    // ===== Sequence accessors (CRDT-free, public-boundary friendly) =====
    /// Locally-monotonic edit-sequence number that increments on every
    /// state-change operation (applyLocalEdit, undo, redo, applyRemoteOps,
    /// resetContent). Used for dirty-tracking ("has the doc changed since
    /// the last save?") without holding a Crdt::Global. See spec §10
    /// decision 8.
    quint64 editSequence() const noexcept;

    // ===== Local writes =====
    /// Apply a list of local edits as a single batched local edit. Edits are
    /// in OLD-text byte coordinates; ranges must be non-overlapping; if
    /// multiple edits, ordering must be ascending by oldStart. Returns the
    /// resulting Operation for broadcast (CRDT future). Emits contentsChanged.
    CollabText::Crdt::Operation
        applyLocalEdit(const QList<MarkoffEdit> &edits);

    // ===== Undo / redo =====
    std::optional<CollabText::Crdt::Operation> undo();
    std::optional<CollabText::Crdt::Operation> redo();
    int  undoDepth() const;
    bool coalesceLastUndo();

    // ===== Remote ops =====
    void applyRemoteOps(const std::vector<CollabText::Crdt::Operation> &ops);

    // ===== Wholesale reload =====
    void resetContent(const QByteArray &newContent, Origin origin);

    // ===== Anchors =====
    CollabText::Crdt::Anchor
        anchorAt(quint32 byteOffset, CollabText::Crdt::Bias bias) const;
    quint32 resolveAnchor(const CollabText::Crdt::Anchor &) const;

    // ===== Sessions (filled in Task 23) =====
    Session *createSession(const SessionParams &params = {});
    void     destroySession(Session *);
    QList<Session *> sessions() const;
    Session *sessionForParticipant(const QString &participantId) const;

    // ===== Garbage collection =====
    qsizetype collectGarbage();
    qsizetype compact(const CollabText::Crdt::Global &watermark);

    // ===== Bench-only opt-in instrumentation =====
    /// Wire an external render-tier timestamp tap for benchmarking. The
    /// document writes worker-thread and main-thread timestamps into the
    /// passed `RenderPhaseTaps` for every parse iteration that completes
    /// while the pointer is installed. Pass nullptr (the default) to disable.
    /// Caller owns the taps and resets them between iterations.
    /// Production callers leave this null and pay zero overhead.
    void setRenderPhaseTaps(Markoff::Render::RenderPhaseTaps *taps) noexcept;

Q_SIGNALS:
    void contentsChanged(QList<Markoff::MarkoffEdit> edits);
    void parseUpdated(const Markoff::Document *parsed, CollabText::Crdt::Global atVersion);
    void documentReloaded();
    void sessionCreated(Markoff::Session *);
    void sessionDestroyed(Markoff::Session *);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
