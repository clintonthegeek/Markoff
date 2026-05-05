// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QHash>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <crdt/Anchor.h>
#include <crdt/Clock.h>
#include <crdt/Operations.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/CrdtProxies.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/StructuralOp.h>
#include <markoff-foundation/TextAnchor.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/RenderPhases.h>
#include <markoff-foundation/SessionParams.h>
#include <markoff-foundation/UndoLog.h>

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

    /// Locally-monotonic parse-sequence number for the most recent parse
    /// delivered via parseUpdated. View-layer code uses this for parse-
    /// ordering ("is this a newer parse than what I rendered?") without
    /// holding a Crdt::Global. Decoupled from the CRDT version vector.
    /// See spec §10 decision 3.
    quint64 parseSequence() const noexcept;

    // ===== Local writes =====
    /// Apply a list of local edits as a single batched local edit. Edits are
    /// in OLD-text byte coordinates; ranges must be non-overlapping; if
    /// multiple edits, ordering must be ascending by oldStart. Returns the
    /// resulting Operation for broadcast (CRDT future). Emits contentsChanged.
    [[deprecated("D2: use applyBlockEdit; will be removed in Phase 14")]]
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

    /// TextAnchor-typed companion to anchorAt(quint32, Crdt::Bias). Same
    /// semantics; view-layer-friendly because it doesn't require including
    /// <crdt/Anchor.h>.
    TextAnchor textAnchorAt(quint32 byteOffset, bool rightBias) const;

    /// Resolve a TextAnchor to its current byte offset. Companion to
    /// resolveAnchor(const Crdt::Anchor &).
    quint32 resolveTextAnchor(const TextAnchor &) const;

    // ===== Block-aware queries =====
    /// Returns the BlockAnchor for the top-level block at index `i` in
    /// the most-recent parse. Out-of-range returns std::nullopt.
    std::optional<BlockAnchor> blockAnchorAt(int blockIndex) const;

    /// Returns the byte range [start, end) of the BlockAnchor's block in
    /// the current parse. End is exclusive of any structural separator
    /// to the next block (parser/scanner-reported AST node range).
    std::optional<std::pair<quint32, quint32>>
        blockByteRange(const BlockAnchor &) const;

    /// Returns the BlockAnchor for the top-level block containing the
    /// given TextAnchor's resolved byte position. Returns std::nullopt
    /// if the resolved byte falls outside any top-level block (e.g.
    /// inside an inter-block separator, before the first block, or
    /// past the last block).
    std::optional<BlockAnchor> blockAt(const TextAnchor &) const;

    /// Returns the offset (in UTF-8 bytes) of the given TextAnchor's
    /// resolved byte relative to the BlockAnchor's first byte. Clamps:
    /// resolved-byte below block-start returns 0; resolved-byte past
    /// block-end returns block byte length.
    int offsetInBlock(const BlockAnchor &, const TextAnchor &) const;

    /// Returns a TextAnchor at `offset` UTF-8 bytes from the
    /// BlockAnchor's first-byte position. Block-local companion to
    /// textAnchorAt(quint32, bool).
    TextAnchor textAnchorAt(const BlockAnchor &, int offset, bool rightBias) const;

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

    // ===== D2 per-block edit API =====
    /// Apply a local edit to a single block's CRDT buffer. The edit is
    /// recorded in the UndoLog as a single undoable action.
    void applyBlockEdit(const Markoff::BlockEdit &edit);

    /// Apply a structural operation (insert/remove/change-kind) to the
    /// IdList and related CRDTs. Recorded in the UndoLog.
    void applyStructural(const Markoff::StructuralOp &op);

    // ===== D2 undo/redo =====
    /// Undo the last D2 action recorded in the UndoLog. Does NOT affect
    /// the legacy single-buffer undo stack.
    void undoD2();

    /// Redo the last undone D2 action. Does NOT affect the legacy stack.
    void redoD2();

    /// Undo the most recent D2 action that touched the given block.
    void undoForBlock(Markoff::BlockId block);

    // ===== D2 block accessors =====
    /// Returns the current ordered list of block IDs from the IdList CRDT.
    std::vector<Markoff::BlockId> iterateBlocks() const;

    /// Returns the BlockKind for the given block (defaults to Paragraph if
    /// not set). Reads from the kindTagMap CRDT.
    Markoff::BlockKind blockKind(Markoff::BlockId id) const;

    /// Returns the current UTF-8 text of the given block's buffer.
    /// Returns empty if the block is not found.
    QByteArray blockText(Markoff::BlockId id) const;

    /// Returns an edit-sequence counter for a specific block's buffer.
    /// Increments each time applyBlockEdit touches that block.
    quint64 blockEditSequence(Markoff::BlockId id) const;

    /// D2: sum of all per-block edit sequences (rough document-level dirty
    /// tracker for D2 internals).
    quint64 d2EditSequence() const noexcept;

    /// Test helper: insert a block with the given kind and content directly
    /// into D2 internals. Callable from test code; do not call in production.
    Markoff::BlockId testInsertBlock(Markoff::BlockKind kind, const QByteArray &content);

    // ===== D2 internal helpers (Cmd layer; do not call from view code) =====

    /// Returns the D2 UndoLog for transaction grouping and coalescing.
    Markoff::UndoLog &d2UndoLog() noexcept;

    /// Apply a buffer edit within an existing transaction.
    /// Same semantics as applyBlockEdit() but uses caller's transaction.
    void d2ApplyBufferEdit(BlockId block, uint32_t offset, uint32_t removedBytes,
                           const QByteArray &insert, UndoLog::Transaction &t);

    /// Insert a new block after `afterBlock` (null BlockId = insert at start).
    /// Creates a Buffer for the new block; registers IdList + KindTag ops with t.
    /// Returns the new BlockId.
    BlockId d2InsertBlock(BlockId afterBlock, BlockKind kind, UndoLog::Transaction &t);

    /// Remove `block` from the IdList and KindTagMap. Buffer retained for GC (Phase 9).
    /// Registers IdList + KindTag ops with t.
    void d2RemoveBlock(BlockId block, UndoLog::Transaction &t);

    /// Update the kind of `block`. Registers KindTag op with t.
    void d2SetBlockKind(BlockId block, BlockKind newKind, UndoLog::Transaction &t);

    /// Set a block attribute. Registers BlockAttrs op with t.
    void d2SetBlockAttr(BlockId block, const QByteArray &attrName,
                        const AttrValue &value, UndoLog::Transaction &t);

    // ===== D2 CRDT proxies (fine-grained change notifications) =====
    /// Returns the BufferProxy for the given block, or nullptr if not found.
    /// Views connect to its signals to detect per-block text changes.
    Markoff::BufferProxy   *bufferProxy(Markoff::BlockId id) const;

    /// Returns the IdList proxy. Views connect to structureChanged() to detect
    /// block insertion/removal.
    Markoff::IdListProxy   *idListProxy() const;

    /// Returns the KindTagMap proxy. Views connect to mapChanged() to detect
    /// block kind changes.
    Markoff::SiblingMapProxy *kindTagMapProxy() const;

    // ===== D2 load =====
    /// Parse `src` as UTF-8 Markdown, populate frontmatter, footnote defs,
    /// and all D2 CRDT internals (IdList, per-block Buffers, KindTagMap,
    /// BlockAttrsMap). Emits documentLoaded() on success.
    void loadFromMarkdown(const QByteArray &src);

    /// Returns the raw bytes stored for `id` at load time (from
    /// loadFromMarkdown). Returns an empty QByteArray if `id` was not
    /// materialised from a load or has no content. Used by Phase 8
    /// touch-test to detect whether a block has been edited since load.
    QByteArray blockLoadTimeBytes(BlockId id) const;

    /// Returns the value stored for `key` in the frontmatter map, or
    /// std::nullopt if `key` is absent. In Phase 7 the map contains a
    /// single "raw" entry whose value is the raw frontmatter string.
    std::optional<QByteArray> frontmatterValue(const QByteArray &key) const;

    // ===== D2 save (Phase 8) =====

    /// Returns all non-tombstoned attrs for the given block as a QHash.
    QHash<Markoff::AttrName, Markoff::AttrValue> blockAttrs(Markoff::BlockId id) const;

    /// Returns true if the block has been modified since the last loadFromMarkdown:
    ///   - born after load (no load-time bytes)
    ///   - content edited (blockEditSequence > 0)
    ///   - kind or attrs changed (d2SetBlockKind / d2SetBlockAttr called)
    bool isBlockTouched(Markoff::BlockId id) const;

    /// Serialise the document to UTF-8 Markdown bytes ready for writing to disk.
    /// For untouched blocks uses the original load-time bytes; for touched blocks
    /// calls the registered BlockSerializer for that block's kind.
    QByteArray serializeForSave() const;

    /// Atomically write serializeForSave() to `path` via QSaveFile.
    /// Returns true on success, false on any I/O error.
    bool save(const QString &path);

    // ===== Garbage collection (Phase 9) =====
    /// Trigger GC manually (e.g. after save). Returns false if a transaction is open.
    bool triggerGc();

Q_SIGNALS:
    void contentsChanged(QList<Markoff::MarkoffEdit> edits);
    /// Emitted on the main thread each time a parse completes.
    ///
    /// `parseInputEditSequence` is the value of `editSequence()` at the
    /// moment this parse's input bytes were captured (the applyLocalEdit /
    /// resetContent call that triggered the parse). View consumers compare
    /// this against per-row last-edit sequence to decide whether the parse
    /// output for a given row is stale relative to user intent.
    /// See restoration spec §4.
    // D2: deprecated, migrating to d2DocumentChanged
    void parseUpdated(const Markoff::Document *parsed,
                      quint64 parseSequence,
                      QList<Markoff::BlockAnchor> blockAnchors,
                      quint64 parseInputEditSequence);
    void documentReloaded();
    void sessionCreated(Markoff::Session *);
    void sessionDestroyed(Markoff::Session *);

    /// Emitted (debounced: once per event-loop spin) whenever any D2 CRDT
    /// changes (applyBlockEdit or applyStructural).
    void d2DocumentChanged();

    /// Emitted when loadFromMarkdown() completes successfully.
    void documentLoaded();

private:
    friend class WatermarkCoordinator;

    void scheduleD2Changed();
    void materializeBlocksFromParsedDoc(const Markoff::Document &parsed,
                                        const QString &body);
    BlockId allocateD2BlockId() noexcept;

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
