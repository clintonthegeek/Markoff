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

#include <markoff/core/BlockId.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/core/BlockSerializerRegistry.h>
#include <markoff/parser/SourceSpan.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/LinkRefMap.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/SessionParams.h>

#include <collabtext/ActorId.h>
#include <markoff/vnext/ProviderBackedDocumentState.h>
#include <markoff/vnext/DocumentStorageProvider.h>

namespace Markoff {

enum class RemoteActionRouteState { committed, deferred, backpressured, quarantined, rejected, uncertain };

struct RemoteActionQueueBudget {
    size_t maxActions = 4096;
    size_t maxBytes = 32U * 1024U * 1024U;
};

struct RemoteActionReceipt {
    QByteArray actionId;
    Markoff::VNext::AcceptedRevision acceptedRevision;
};

struct RemoteActionQuarantine {
    QByteArray actionId;
    QByteArray originalMab;
    CollabText::Error reason;
};

struct RemoteActionRouteResult {
    RemoteActionRouteState state = RemoteActionRouteState::rejected;
    std::optional<CollabText::Error> diagnostic;
    /// Receipts for the offered MAB and any older deferred MABs made ready by
    /// this commit. A transport may acknowledge only these exact action IDs.
    std::vector<RemoteActionReceipt> committed;
    /// Action IDs whose rejected envelopes were durably retained by the
    /// document provider during this offer or a deferred-queue drain.
    std::vector<QByteArray> quarantined;
};

class Document;       // markoff-parser
class Session;        // forward; defined in Session.h after this task

struct BlockPasteResult {
    Markoff::VNext::ActionPublicationResult publication;
    BlockId caretBlock;
    uint32_t caretByteOffset = 0;
};

/// Canonical text + AST + sessions. Views subscribe to this document's
/// block, structure and metadata signals.
class MARKOFF_CORE_EXPORT MarkoffDocument : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MarkoffDocument)
public:
    struct ExportSnapshot {
        QByteArray markdown;
        Markoff::VNext::AcceptedRevision revision;
    };

    /// Open a document only after provider recovery and accepted-prefix replay
    /// complete. Storage and scope remain caller-owned; the returned document
    /// owns its lifecycle session and candidate projection.
    [[nodiscard]] static CollabText::Result<std::unique_ptr<MarkoffDocument>> open(
        Markoff::VNext::DocumentStorageProvider &provider,
        const Markoff::VNext::DocumentStorageScope &scope,
        Markoff::VNext::StorageMode mode,
        const Markoff::BlockSerializerRegistry *registry = nullptr);

    [[nodiscard]] Markoff::VNext::ProviderDocumentState providerState() const noexcept;
    [[nodiscard]] bool canStartOrdinaryAction() const noexcept;
    /// Rebuilds accepted state after a proven-absent publication and records
    /// terminal coverage for every remaining local allocation reservation.
    [[nodiscard]] CollabText::Result<void> recoverPendingAllocations();
    [[nodiscard]] Markoff::VNext::ActionPublicationResult importInitialMarkdown(
        const QByteArray& source);
    /// Validate, durably accept and replay one complete remote MAB action.
    /// A caller may advance its transport receipt only for a committed result.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult acceptRemoteAction(
        const QByteArray& mabBytes);
    /// Offer a complete MAB to the bounded, owner-thread remote router.
    /// Causally deferred and unknown-target MABs are retained without a receipt
    /// and retried after commits.
    [[nodiscard]] Markoff::RemoteActionRouteResult routeRemoteAction(
        const QByteArray& mabBytes);
    /// Retry durably retained causal waits after startup or an external stream
    /// registration. Committed actions in the result are receipt eligible.
    [[nodiscard]] Markoff::RemoteActionRouteResult retryDeferredRemoteActions();
    /// Read locally committed MABs that still need to be sent after startup or
    /// reconnect. A host must authenticate receipt delivery before acknowledging.
    [[nodiscard]] CollabText::Result<std::vector<Markoff::VNext::PendingOutboundAction>>
    pendingOutboundActions() const;
    [[nodiscard]] CollabText::Result<void> acknowledgeOutboundActionReceipt(
        const QByteArray& actionId, const QByteArray& acceptedRevision);
    /// Recover whole-action quarantine records after provider reopen. Records
    /// written by primitive routers without an enclosing MAB are omitted.
    [[nodiscard]] CollabText::Result<std::vector<Markoff::RemoteActionQuarantine>>
    quarantinedRemoteActions() const;
    [[nodiscard]] size_t pendingRemoteActionCount() const noexcept;
    /// Set owner-thread routing limits. The update is rejected if the current
    /// deferred queue would exceed the new bounds; zero disables retention.
    [[nodiscard]] CollabText::Result<void> setRemoteActionQueueBudget(
        RemoteActionQueueBudget budget);
    /// Publish external/plugin Markdown bytes as one accepted replacement
    /// action. Replacement retains the registered document history but creates
    /// fresh block identities for the replacement projection.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult replaceMarkdown(
        const QByteArray& source);
    [[nodiscard]] CollabText::Result<ExportSnapshot> snapshotForExport() const;
    [[nodiscard]] CollabText::Result<void> acknowledgeExport(
        const ExportSnapshot& snapshot);

    /// A provider and document scope are required to open a document.
    explicit MarkoffDocument(QObject *parent = nullptr) = delete;
    ~MarkoffDocument() override;

    /// Returns the BlockSerializerRegistry passed at construction, or nullptr
    /// if none was provided.
    const Markoff::BlockSerializerRegistry *serializerRegistry() const;

    // ===== Reads =====
    /// Mint and resolve opaque, stream-scoped position tokens for candidate
    /// consumers. Tokens are encoded by CollabText and cannot be interpreted
    /// as engine replica/character coordinates.
    [[nodiscard]] CollabText::Result<QByteArray> boundaryTokenAt(
        BlockId block, quint32 byteOffset, bool rightAffinity) const;
    [[nodiscard]] CollabText::Result<quint32> resolveBoundaryToken(
        BlockId block, const QByteArray& token) const;

    // ===== Sessions (filled in Task 23) =====
    Session *createSession(const SessionParams &params = {});
    void     destroySession(Session *);
    QList<Session *> sessions() const;
    Session *sessionForParticipant(const QString &participantId) const;

    // ===== Provider-backed block edits =====
    /// Apply a local edit as one durably published, undoable document action.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult applyBlockEdit(
        const Markoff::BlockEdit &edit);
    /// Publish non-overlapping block-local edits as one durable action.
    /// Offsets refer to the pre-action text in each block.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult applyBlockEdits(
        const QList<Markoff::BlockEdit>& edits);
    /// Atomically replace part of one block and remove subsequent blocks in
    /// the same accepted text/list/kind-map action. The first block survives.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult applyBlockMerge(
        const Markoff::BlockEdit& survivorEdit,
        const QList<BlockId>& removedBlocks);
    /// Atomically split a block into its retained prefix and a new block whose
    /// text begins with `newBlockPrefix` followed by the removed suffix.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult applyBlockSplit(
        const Markoff::BlockEdit& prefixEdit, BlockKind newBlockKind,
        const QByteArray& newBlockPrefix = {});
    [[nodiscard]] Markoff::VNext::ActionPublicationResult changeBlockKind(
        BlockId block, BlockKind kind,
        const QList<AttrName>& attrNames = {},
        const QList<AttrValue>& attrValues = {});
    /// Atomically exchange the visible text, kind, and attribute payloads of
    /// two blocks in one accepted action. Missing known attributes are
    /// written using the caller-provided defaults so stale values do not
    /// survive a kind swap.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult swapBlockPayload(
        BlockId first, BlockId second,
        const QHash<AttrName, AttrValue>& defaults);
    [[nodiscard]] BlockPasteResult pasteMarkdownAt(
        BlockId target, uint32_t byteOffset, const QByteArray& markdown);
    /// Replace a block-local source range, including any complete intervening
    /// blocks, with parsed Markdown in one accepted action.
    [[nodiscard]] BlockPasteResult pasteMarkdownReplacingRange(
        BlockId startBlock, uint32_t startByteOffset,
        BlockId endBlock, uint32_t endByteOffset,
        const QByteArray& markdown);

    /// Replace each match's byte span with literal `replacement` in one
    /// durable action. Matches use SearchEngine's block-local byte offsets.
    /// Stale matches are skipped; an empty list is a no-op.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult replaceMatches(
        const QList<SearchHit> &matches, const QString &replacement);

    /// Source-view representation of the document: per-block content joined
    /// by `interBlockSeparator()` (`"\n\n"`). No final document terminator —
    /// a source widget should not render a phantom empty line at the end;
    /// the trailing `"\n"` is `serializeForSave()`'s file-on-disk concern.
    /// This is the Markdown representation used for source display and export.
    QByteArray flatView() const;

    /// Runtime flat view for the WP-rendered flat-text widget views
    /// (`markoff-styled`, `markoff-source`). Joins blocks with a SINGLE
    /// `\n` (one QTextBlock per model block). The visible gap between
    /// paragraphs comes from `QTextBlockFormat::topMargin/bottomMargin`,
    /// not whitespace. Use this — NOT `flatView()` — for the QTextDocument
    /// the user edits.
    ///
    /// `flatView()` remains the canonical save/parse form (`\n\n` between
    /// content blocks).
    QByteArray widgetFlatView() const;

    /// The raw-markdown marker (indent + bullet/number/task-box + a single
    /// space) that a `ListItem` block's `serializeForSave()` output carries
    /// but its `blockText()`/`widgetFlatView()` segment does NOT — the
    /// parser harvest narrows `ListItem` byte ranges to post-marker content
    /// before the buffer is ever populated. `BlockQuote` is narrowed the
    /// same way (`> ` stripped per depth); `Heading` and `CodeBlock` are
    /// NOT — their buffers keep the ATX prefix / fences inline. See the
    /// per-kind buffer-convention table in `markoff-core/CLAUDE.md` before
    /// assuming either behavior for a kind not listed here. Returns an
    /// empty `QByteArray` for any non-ListItem block. Reuses the exact
    /// reconstruction rules `serializeForSave()` uses — single source of
    /// truth, no duplicated marker logic. Intended for display-only
    /// decoration (queue #8.3;
    /// docs/specs/2026-06-16-source-listitem-marker-decoration-design.md) —
    /// NOT part of the edited flat-view byte space.
    QByteArray listItemDisplayMarker(BlockId id) const;

    /// Same idea as listItemDisplayMarker() for BlockQuote: returns
    /// `depth × "> "` (BlockQuoteDepth attr, default/minimum 1) — the exact
    /// prefix serializeBlockQuote() reconstructs from a content-only
    /// buffer, single source of truth. Empty for any non-BlockQuote block.
    /// Intended for a selection-copy caller to restore the `> ` marker the
    /// same way listItemDisplayMarker() lets ListItem callers restore `-`/
    /// `1.` (buffers for both kinds are marker-stripped content — see the
    /// buffer-convention table in markoff-core/CLAUDE.md).
    QByteArray blockQuoteDisplayMarker(BlockId id) const;

    /// Parse a clipboard payload and publish the selected text/list/kind/
    /// attribute changes as one action. Offsets use concatenated block text.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult applyStructuredPaste(
        quint32 startByte, quint32 endByte, const QJsonArray &blocks);

    /// Serialise a clipboard JSON block payload back to Markdown text. Reconstructs the
    /// kind-specific markdown prefix (heading `#`, list marker `-`/`1.`/etc.,
    /// blockquote `>`, code-fence) from the per-block "kind" + "attrs" fields,
    /// and joins blocks with `\n\n`. Used by clipboard plain-text fallback.
    static QByteArray reconstructFlatMarkdown(const QJsonArray &blocks);

    /// Apply a structural operation as one durably published document action.
    Markoff::VNext::ActionPublicationResult applyStructural(
        const Markoff::StructuralOp &op);

    // ===== Durable undo/redo =====
    /// True when undoD2() has a local action to reverse. Uses the same event
    /// predicate undoD2() selects with, so a UI never advertises an undo that
    /// cannot run (ASK-M9).
    bool canUndoD2() const;

    /// True when redoD2() has an undone local action to reapply.
    bool canRedoD2() const;

    /// Number of local actions undoD2() could still reverse, newest first.
    qsizetype d2UndoDepth() const;

    /// Undo the most recent accepted local document action.
    void undoD2();

    /// Redo the most recently undone local document action.
    void redoD2();

    /// Undo the most recent local document action that touched the given block.
    Q_INVOKABLE void undoForBlock(Markoff::BlockId block);
    /// Redo the most recent durable action undone for the given block.
    Q_INVOKABLE void redoForBlock(Markoff::BlockId block);

    /// Returns whether an accepted local action touching this block can be
    /// undone. View code uses this to enable or disable its undo control.
    Q_INVOKABLE bool canUndoForBlock(Markoff::BlockId blockAnchor) const;

    /// Toggle the Checked attr on a ListItem block. No-op if the block is not
    /// a ListItem or does not have a Checked attr. Q_INVOKABLE for QML use.
    Q_INVOKABLE void toggleListItemChecked(Markoff::BlockId anchor);

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

    /// Sum of the per-block edit sequences for document-level change tracking.
    quint64 d2EditSequence() const noexcept;

    /// Increments on structural block changes, separately from per-block
    /// content edits.
    quint64 structuralEditSequence() const noexcept;

    // ===== Inline formatting cache =====
    /// Returns inline formatting spans for block `id`, computed on first access
    /// and cached until the block's edit sequence increments.
    QList<Markoff::SourceSpan> inlineSpansFor(Markoff::BlockId id) const;

    /// Returns the value stored for `key` in the frontmatter map, or
    /// std::nullopt if `key` is absent.
    std::optional<QByteArray> frontmatterValue(const QByteArray &key) const;

    // ===== D2 save (Phase 8) =====

    /// Returns all non-tombstoned attrs for the given block as a QHash.
    QHash<Markoff::AttrName, Markoff::AttrValue> blockAttrs(Markoff::BlockId id) const;
    /// Publish one block attribute value through its owner-held sibling map.
    [[nodiscard]] Markoff::VNext::ActionPublicationResult setBlockAttribute(
        Markoff::BlockId id, const Markoff::AttrName& name,
        const Markoff::AttrValue& value);
    [[nodiscard]] Markoff::VNext::ActionPublicationResult removeBlockAttribute(
        Markoff::BlockId id, const Markoff::AttrName& name);
    [[nodiscard]] Markoff::VNext::ActionPublicationResult setFrontmatterValue(
        const QByteArray& key, const QByteArray& value);
    [[nodiscard]] Markoff::VNext::ActionPublicationResult removeFrontmatterValue(
        const QByteArray& key);
    [[nodiscard]] Markoff::VNext::ActionPublicationResult setLinkReference(
        const Markoff::LinkRefId& id, const Markoff::LinkRefValue& value);
    [[nodiscard]] Markoff::VNext::ActionPublicationResult removeLinkReference(
        const Markoff::LinkRefId& id);
    [[nodiscard]] std::optional<Markoff::LinkRefValue> linkReference(
        const Markoff::LinkRefId& id) const;

    /// Serialise the current document snapshot to UTF-8 Markdown bytes.
    QByteArray serializeForSave() const;

    /// Atomically write serializeForSave() to `path` via QSaveFile.
    /// Returns true on success, false on any I/O error.
    bool save(const QString &path);

    /// If a `d2DocumentChanged` is queued (via `scheduleD2Changed`), emit it
    /// synchronously now and cancel the deferred fire. Used by the typing
    /// path so the span-update cascade (model → delegate → highlighter) lands
    /// inside the same `QTextDocument::contentsChange` emission chain — the
    /// `QSyntaxHighlighter`'s own subscriber then runs `highlightBlock` with
    /// fresh spans, avoiding a one-paint-frame flicker of inline delimiters
    /// after the insertion point. No-op if nothing is pending.
    void flushPendingD2Changed();

Q_SIGNALS:
    void sessionCreated(Markoff::Session *);
    void sessionDestroyed(Markoff::Session *);

    /// Consumer-facing signal emitted after an accepted document change.
    void documentChanged();

    /// Emitted after document projection changes have been applied.
    void d2DocumentChanged();

    /// Emitted after initial Markdown import completes successfully.
    void documentLoaded();

    // Targeted block-update signals (v1.0). Subscribers can react to specific
    // touched blocks without re-walking the whole document.

    /// Emitted synchronously when one or more block buffers are edited
    /// in-place, carrying the IDs of the touched blocks.
    void blocksChanged(QList<Markoff::BlockId> ids);

    /// Tentative candidate projection update, emitted before synchronous
    /// provider publication. `blocksChanged` and `documentChanged` report only
    /// a committed document action; definite rollback has its own signal.
    void tentativeBlockChanged(Markoff::BlockId id);
    void tentativeBlockReverted(Markoff::BlockId id);
    void tentativeBlockCommitted(Markoff::BlockId id);
    /// Emitted after in-session reservation recovery may have changed whether
    /// ordinary local actions are permitted.
    void providerStateChanged();
    /// Exact local MAB/3 bytes, emitted only after provider acceptance and projection refresh.
    void localActionEnvelopeCommitted(QByteArray actionId, QByteArray mabBytes);

    /// Emitted synchronously when a new block is structurally inserted,
    /// carrying its ID and its row index in the post-insertion block list.
    void blockInserted(Markoff::BlockId id, int row);

    /// Emitted synchronously when a block is structurally removed,
    /// carrying its ID and its former row index.
    void blockRemoved(Markoff::BlockId id, int row);


private:
    struct Private;
    explicit MarkoffDocument(std::unique_ptr<Private> state);
    [[nodiscard]] Markoff::VNext::ActionPublicationResult applyMarkdownSource(
        const QByteArray& source, bool replacement);
    void compensateReplacementAction(
        const Markoff::VNext::ActionHistoryEvent& target, bool redo);
    void compensateTextGroups(
        const Markoff::VNext::ActionHistoryEvent& target, bool redo);
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
