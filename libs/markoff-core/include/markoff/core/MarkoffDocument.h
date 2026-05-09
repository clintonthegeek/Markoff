// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonArray>
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

#include <markoff/core/BlockAnchor.h>
#include <markoff/core/BlockSerializerRegistry.h>
#include <markoff/core/Cursor.h>
#include <markoff/parser/SourceSpan.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/CrdtProxies.h>
#include <markoff/core/Origin.h>
#include <markoff/core/PasteMeta.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/core/TextAnchor.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/SessionParams.h>
#include <markoff/core/UndoLog.h>

namespace Markoff {

class Document;       // markoff-parser
class Session;        // forward; defined in Session.h after this task

/// Canonical text + AST + sessions. Owns a CollabText::Crdt::Buffer
/// internally; views are subscribers to this object's signals.
class MARKOFF_CORE_EXPORT MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    /// Single-user mode constructor. Uses replica ID sentinel 0x0001.
    /// isCollabConfigured() returns false.
    explicit MarkoffDocument(QObject *parent = nullptr);

    /// Construct an empty document with the given replica ID. ReplicaId is
    /// the CRDT identity for this MarkoffDocument instance; for single-user
    /// use, a random quint16 is fine. An optional BlockSerializerRegistry
    /// may be passed in for host-provided serializers (view layer injection);
    /// nullptr means "use BuiltinBlockSerializerRegistry only".
    /// isCollabConfigured() returns true.
    explicit MarkoffDocument(quint16 replicaId,
                             const Markoff::BlockSerializerRegistry *registry = nullptr,
                             QObject *parent = nullptr);
    ~MarkoffDocument() override;

    /// Returns the BlockSerializerRegistry passed at construction, or nullptr
    /// if none was provided.
    const Markoff::BlockSerializerRegistry *serializerRegistry() const;

    // ===== Reads =====
    QByteArray toMarkdownUtf8() const;        ///< Buffer::text() as QByteArray
    QString    toMarkdown() const;            ///< UTF-8 -> QString convenience
    quint32    visibleLength() const;         ///< UTF-8 byte length

    /// Returns the most-recently parsed Document (from markoff-parser),
    /// or nullptr if no parse has completed yet.
    const Markoff::Document *parsedDocument() const;

    // ===== CRDT identity =====
    quint16 replicaId() const noexcept;
    bool isCollabConfigured() const noexcept;
    CollabText::Crdt::Global version() const;

    // ===== Sequence accessors (CRDT-free, public-boundary friendly) =====
    /// Locally-monotonic edit-sequence number. Increments on every state-change
    /// op (undo, redo, applyRemoteOps, resetContent, applyFlatEdit, etc.).
    /// Used for dirty-tracking without holding a Crdt::Global.
    quint64 editSequence() const noexcept;

    // ===== Undo / redo =====
    std::optional<CollabText::Crdt::Operation> undo();
    std::optional<CollabText::Crdt::Operation> redo();
    int  undoDepth() const;
    bool coalesceLastUndo();

    // ===== Remote ops =====
    void applyRemoteOps(const std::vector<CollabText::Crdt::Operation> &ops);

    /// Apply remote ops in arrival order. Dispatches by CrdtTarget.
    /// See D5 spec §2.4.
    void applyRemoteOps(QList<Markoff::MarkoffOp> ops,
                        Markoff::MarkoffBundleMeta meta);

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

    // ===== D2 per-block edit API =====
    /// Apply a local edit to a single block's CRDT buffer. The edit is
    /// recorded in the UndoLog as a single undoable action.
    void applyBlockEdit(const Markoff::BlockEdit &edit);

    /// Apply a flat (source-widget) edit expressed as global UTF-8 byte
    /// offsets across the concatenated block buffers. Opens an UndoLog
    /// transaction and decomposes the edit into per-block d2ApplyBufferEdit
    /// calls + structural ops (d2InsertBlock, d2RemoveBlock). The entire
    /// edit lands in one transaction so a single undoD2() reverses it.
    void applyFlatEdit(uint32_t oldStart,
                       uint32_t oldEnd,
                       const QByteArray &newText,
                       Origin origin);

    /// Structure-aware paste. Serializes `blocks` (a QJsonArray of block
    /// objects with "kind", "text", and optional "attrs") to flat markdown
    /// and calls applyFlatEdit on the byte range [startByte, endByte).
    /// If meta.reuseBlockIds is true and meta.cutSeq matches a cached recent
    /// cut, the cache entry is consumed (BlockId reuse is a TODO for a future
    /// remap pass; correctness is unaffected). All work lands in one UndoLog
    /// transaction; a single undoD2() reverses it.
    void applyStructuredPaste(quint32 startByte, quint32 endByte,
                              const QJsonArray &blocks,
                              const Markoff::PasteMeta &meta);

    /// Serialise a clipboard JSON block payload (as produced by
    /// LiveClipboardController) back to flat markdown text. Reconstructs the
    /// kind-specific markdown prefix (heading `#`, list marker `-`/`1.`/etc.,
    /// blockquote `>`, code-fence) from the per-block "kind" + "attrs" fields,
    /// and joins blocks with `\n\n`. Used both by applyStructuredPaste (which
    /// then calls applyFlatEdit) and by LiveClipboardController's plain-text
    /// fallback so the markdown going to other apps round-trips correctly.
    static QByteArray reconstructFlatMarkdown(const QJsonArray &blocks);

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
    /// BlockAnchor is a type alias for BlockId; Q_INVOKABLE for QML/signal access.
    Q_INVOKABLE void undoForBlock(Markoff::BlockId block);

    /// Returns true if the UndoLog contains at least one entry that touched
    /// the given block, i.e. whether undoForBlock(blockAnchor) would do
    /// anything useful. View-layer code uses this to enable/disable undo UI.
    /// BlockAnchor is a type alias for BlockId.
    Q_INVOKABLE bool canUndoForBlock(Markoff::BlockAnchor blockAnchor) const;

    /// Toggle the Checked attr on a ListItem block. No-op if the block is not
    /// a ListItem or does not have a Checked attr. Q_INVOKABLE for QML use.
    Q_INVOKABLE void toggleListItemChecked(Markoff::BlockAnchor anchor);

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

    /// Save-watermark API. The view-layer / host calls markSaved with the
    /// d2EditSequence captured immediately before its file write started;
    /// dirty() returns whether subsequent edits have advanced the sequence.
    /// dirtyChanged fires only on transitions (false→true and true→false),
    /// not on every edit.
    quint64 savedSequence() const noexcept;
    void    markSaved(quint64 seq);
    bool    dirty() const noexcept;

    /// Record the BlockIds removed in a Cut so a subsequent local Paste
    /// can reuse them (preserving CRDT identity for cut→paste-back).
    /// `cutSeq` is the d2EditSequence captured immediately before the cut.
    /// FIFO-capped at 8 entries.
    void recordRecentCut(quint64 cutSeq, std::vector<BlockId> ids);

    /// Returns the cached BlockIds for `cutSeq` if still present, else empty.
    std::vector<BlockId> takeRecentCut(quint64 cutSeq);

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

    // ===== D2 inline cache (Phase 10) =====
    /// Returns inline formatting spans for block `id`, computed on first access
    /// and cached until the block's edit sequence increments.
    QList<Markoff::SourceSpan> inlineSpansFor(Markoff::BlockId id) const;

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

    /// If a `d2DocumentChanged` is queued (via `scheduleD2Changed`), emit it
    /// synchronously now and cancel the deferred fire. Used by the typing
    /// path so the span-update cascade (model → delegate → highlighter) lands
    /// inside the same `QTextDocument::contentsChange` emission chain — the
    /// `QSyntaxHighlighter`'s own subscriber then runs `highlightBlock` with
    /// fresh spans, avoiding a one-paint-frame flicker of inline delimiters
    /// after the insertion point. No-op if nothing is pending.
    void flushPendingD2Changed();

Q_SIGNALS:
    void documentReloaded();
    void sessionCreated(Markoff::Session *);
    void sessionDestroyed(Markoff::Session *);

    /// Consumer-facing no-arg signal. Emitted whenever the document content
    /// changes: on loadFromMarkdown(), on every debounced D2 CRDT edit cycle,
    /// and on resetContent(). Views that only need "something changed" connect
    /// here instead of the more granular d2DocumentChanged().
    void documentChanged();

    /// Emitted (debounced: once per event-loop spin) whenever any D2 CRDT
    /// changes (applyBlockEdit or applyStructural).
    void d2DocumentChanged();

    /// Emitted when loadFromMarkdown() completes successfully.
    void documentLoaded();

    // Targeted block-update signals (v1.0). Subscribers can react to specific
    // touched blocks without re-walking the whole document.

    /// Emitted synchronously when one or more block buffers are edited
    /// in-place, carrying the IDs of the touched blocks.
    void blocksChanged(QList<Markoff::BlockId> ids);

    /// Emitted synchronously when a new block is structurally inserted,
    /// carrying its ID and its row index in the post-insertion block list.
    void blockInserted(Markoff::BlockId id, int row);

    /// Emitted synchronously when a block is structurally removed,
    /// carrying its ID and its former row index.
    void blockRemoved(Markoff::BlockId id, int row);

    /// Emitted on dirty-state transitions: true when the document becomes
    /// dirty (d2EditSequence diverges from savedSequence), false when
    /// markSaved() is called with the current sequence. Fires only on
    /// transitions, not on every edit.
    void dirtyChanged(bool dirty);

    /// Emitted once per UndoLog::Transaction commit with all ops the
    /// transaction produced. Only emitted when isCollabConfigured() is true.
    /// See D5 spec §2.2.
    void localOpsProduced(QList<Markoff::MarkoffOp> ops,
                          Markoff::MarkoffBundleMeta meta);

    /// Emitted after each save when the document advances its local watermark.
    /// W is the max Lamport counter produced so far. Consumer uses this to
    /// know "I've committed state up to W" for downstream ack tracking.
    void localWatermarkAdvanced(quint64 watermark);

    /// Emitted after save in collab mode when the document wants to compact
    /// but is gated on peer acks. Consumer calls notifyAcksAtWatermark(W)
    /// once all peers confirm they've seen ops ≤ W.
    void wantsAcksAtWatermark(quint64 snapshotWatermark);

    /// Emitted after compaction actually runs (either immediately on save
    /// when ackedWatermark ≥ W, or deferred after notifyAcksAtWatermark).
    void watermarkCompacted(quint64 watermark);

public:
    /// Tell the document all known peers have ack'd up to W. Monotonic.
    /// Triggers deferred compaction if the current snapshot watermark ≤ W.
    void notifyAcksAtWatermark(quint64 watermark);

    /// Test hook: pretend a save just succeeded, triggering the
    /// watermark-gate logic. Production code calls this via save().
    /// Always compiled (same pattern as testInsertBlock) so test
    /// executables can link against the library.
    void simulateSaveSucceeded();

public:
    // ===== D5 remote cursor state (Phase 7) =====

    /// Store a remote peer's cursor and notify the view layer.
    void setRemoteCursor(quint16 replicaId,
                         Markoff::Cursor cursor,
                         QColor color,
                         QString label);

    /// Remove a remote peer's cursor and notify the view layer.
    void clearRemoteCursor(quint16 replicaId);

    /// Remove all remote cursors and emit remoteCursorCleared for each.
    void clearAllRemoteCursors();

    /// Test inspection: returns the stored cursor for the given replica,
    /// or NoCursor{} if not present.
    Markoff::Cursor remoteCursorOf(quint16 replicaId) const;

Q_SIGNALS:
    void remoteCursorChanged(quint16 replicaId,
                             Markoff::Cursor cursor,
                             QColor color,
                             QString label);
    void remoteCursorCleared(quint16 replicaId);

private:
    friend class WatermarkCoordinator;

    void scheduleD2Changed();
    void maybeEmitDirtyChanged();
    void onSaveComplete();
    void materializeBlocksFromParsedDoc(const Markoff::Document &parsed,
                                        const QString &body);
    BlockId allocateD2BlockId() noexcept;
    void applyRemoteBufferOp(Markoff::BlockId blockId, const QByteArray &payload);
    void applyRemoteIdListOp(const QByteArray &payload);
    void applyRemoteKindTagMapOp(const QByteArray &payload);
    void applyRemoteBlockAttrsMapOp(const QByteArray &payload);
    void applyRemoteFrontmatterMapOp(const QByteArray &payload);
    void applyRemoteLinkRefMapOp(const QByteArray &payload);
    void applyRemoteFootnoteDefMapOp(const QByteArray &payload);

    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
