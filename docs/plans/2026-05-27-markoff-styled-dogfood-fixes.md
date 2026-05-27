# `markoff-styled` v0.1 dogfood fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the three dogfood-reported bugs in `markoff-styled` (headings unstyled, scroll lag, viewport jump) by porting two `markoff-live` patterns into the single-document QWidget context and adding two QWidget-specific affordances.

**Architecture:** Five targeted changes inside `libs/markoff-styled/`, no public-API changes, no new files. (1) Per-block content hash in `StyleApplier::m_blockHashes` gates the per-block restyle so only changed blocks invalidate layout. (2) Prefix-rule kind inference issues `Cmd::changeKind` via deferred dispatch when the stored kind disagrees with the text. (3) Scroll position is captured before `applyFormats` and restored after `endEditBlock` when the block set didn't change structurally. (4) `LinkInteraction::resolveLinkAt` uses `MarkoffDocument::blockAt` for O(log N) lookup instead of linear scan. (5) Tests added; existing `QEXPECT_FAIL` slot promoted to passing.

**Tech Stack:** C++20, Qt6 6.8+ (Core, Gui, Widgets, Test), CMake 3.19+, `markoff-core` (`Cmd::changeKind` from `<markoff/core/Cmd/D2.h>`).

**Spec:** `docs/specs/2026-05-27-markoff-styled-dogfood-fixes-design.md`.

**Build/test commands** (used throughout):

```bash
cmake --build build-dev --target markoff_styled -j 8
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_'           # all styled tests
scripts/run-tests.sh --bin tst_styled_dogfood_invariants
```

`scripts/run-tests.sh` defaults to `QT_QPA_PLATFORM=offscreen`. Never pass `--direct` or `--nested` without explicit user permission.

**Branch posture:** Project works on `master` directly (single-line-of-development per `CLAUDE.md`). Commit each task as a separate atomic commit on master.

---

## File structure (target)

```
libs/markoff-styled/
├── src/
│   ├── StyleApplier.h           # +m_blockHashes, +hashSkips accessor, +setTextEdit
│   ├── StyleApplier.cpp         # hash gate, kind transition, scroll preserve
│   ├── LinkInteraction.cpp      # resolveLinkAt fast path
│   └── Editor.cpp               # wires setTextEdit(m_editor) in constructor
├── CLAUDE.md                    # updated: v0 known gaps closed by v0.1
└── tests/
    ├── CMakeLists.txt           # +tst_styled_dogfood_invariants
    ├── tst_styled_d2_integration.cpp   # QEXPECT_FAIL removed
    └── tst_styled_dogfood_invariants.cpp   # new (3 slots)
docs/queue.md                    # one Discipline Log entry for the QTimer::singleShot smell
```

---

## Task 1: Per-block hash gate

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.h` — add `m_blockHashes`, `hashSkips()` accessor
- Modify: `libs/markoff-styled/src/StyleApplier.cpp` — add `computeBlockHash` helper, skip-on-match inside `applyFormats()`, prune stale entries, clear cache on `rerender()`-triggering setters
- Modify: `libs/markoff-styled/tests/CMakeLists.txt` — register new test binary
- Create: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`

- [ ] **Step 1: Write the failing test**

`libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/styled/Editor.h>

class TstStyledDogfoodInvariants : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void hash_gate_skips_unchanged_blocks() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // 10 paragraph blocks separated by blank lines.
        doc.loadFromMarkdown(QByteArrayLiteral(
            "a\n\nb\n\nc\n\nd\n\ne\n\nf\n\ng\n\nh\n\ni\n\nj"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        // After initial load, the styler has run at least once and
        // populated the hash for every block. Counter starts at 0.
        // Tickle the document with a single-character edit in block 0.
        const quint64 skipsBefore = e.styleApplierHashSkips();
        Q_UNUSED(skipsBefore);

        // Append "X" to the first block (block 0 byte range is [0,1)).
        doc.applyFlatEdit(1, 1, QByteArrayLiteral("X"),
                          Markoff::Origin::UserEdit);

        // Spin the event loop so the debounced d2DocumentChanged fires
        // and StyleApplier::applyFormats runs.
        QTRY_VERIFY(e.styleApplierHashSkips() > 0);
        // 9 of 10 blocks should be hash-skipped on this pass.
        QCOMPARE(e.styleApplierHashSkips(), quint64(9));
    }
};

QTEST_MAIN(TstStyledDogfoodInvariants)
#include "tst_styled_dogfood_invariants.moc"
```

- [ ] **Step 2: Register the test binary**

Append to `libs/markoff-styled/tests/CMakeLists.txt`:
```cmake
add_executable(tst_styled_dogfood_invariants tst_styled_dogfood_invariants.cpp)
add_test(NAME tst_styled_dogfood_invariants COMMAND tst_styled_dogfood_invariants)
target_link_libraries(tst_styled_dogfood_invariants
    PRIVATE Qt6::Test Qt6::Widgets markoff_styled markoff_core)
set_tests_properties(tst_styled_dogfood_invariants
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
scripts/run-tests.sh --bin tst_styled_dogfood_invariants
```
Expected: COMPILE FAIL — `Markoff::Styled::Editor::styleApplierHashSkips()` does not exist yet.

- [ ] **Step 4: Add the `m_blockHashes` member and `hashSkips()` accessor to StyleApplier**

In `libs/markoff-styled/src/StyleApplier.h`, add the include + members:

```cpp
#include <QHash>
#include <markoff/core/BlockId.h>
```

Inside the class, near the other accessors, add:
```cpp
/// Number of blocks that were hash-skipped (formats not reapplied)
/// during the most recent applyFormats pass. Tests assert this.
quint64 hashSkips() const noexcept { return m_hashSkipsLastPass; }
```

In the private members section, add:
```cpp
QHash<Markoff::BlockId, quint64> m_blockHashes;
quint64                          m_hashSkipsLastPass = 0;
```

- [ ] **Step 5: Add `computeBlockHash` helper in StyleApplier.cpp**

In `libs/markoff-styled/src/StyleApplier.cpp`, inside the existing anonymous namespace (next to the format helpers), add:

```cpp
quint64 computeBlockHash(Markoff::BlockKind kind,
                        const QByteArray &text,
                        const QList<Markoff::SourceSpan> &spans,
                        qreal fontScale) {
    quint64 h = qHash(int(kind));
    h ^= qHash(text);
    h ^= quint64(text.size()) * 0x9E3779B97F4A7C15ULL;
    h ^= quint64(spans.size()) << 32;
    for (const Markoff::SourceSpan &span : spans) {
        h ^= quint64(span.charOffset) * 0xBF58476D1CE4E5B9ULL;
        h ^= quint64(span.charLength) << 16;
        const quint64 flagBits =
            (span.bold          ? 1ULL << 0  : 0) |
            (span.italic        ? 1ULL << 1  : 0) |
            (span.strikethrough ? 1ULL << 2  : 0) |
            (span.code          ? 1ULL << 3  : 0) |
            (span.highlight     ? 1ULL << 4  : 0) |
            (span.isLink        ? 1ULL << 5  : 0) |
            (span.isWikilink    ? 1ULL << 6  : 0) |
            (span.isTag         ? 1ULL << 7  : 0) |
            (span.isFootnoteRef ? 1ULL << 8  : 0);
        h ^= flagBits;
    }
    // Mix in fontScale (cast to quint64 bits for stable hashing).
    quint64 fsBits = 0;
    std::memcpy(&fsBits, &fontScale, sizeof(fsBits));
    h ^= fsBits;
    return h;
}
```

Add the include for `std::memcpy` at the top of `StyleApplier.cpp` if not already present:
```cpp
#include <cstring>
```

- [ ] **Step 6: Add skip-on-match + prune logic to `applyFormats()`**

In `libs/markoff-styled/src/StyleApplier.cpp::applyFormats()`, modify the block-iteration loop. Replace the current loop body header:

```cpp
for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
    // ... existing range computation ...
    const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
    // ... rest as before ...
}
```

with:

```cpp
m_hashSkipsLastPass = 0;
QHash<Markoff::BlockId, char> currentIds;  // set-like, value unused

for (Markoff::BlockId id : m_markoffDocument->iterateBlocks()) {
    currentIds.insert(id, 0);
    // ... existing range computation up to and including blockEnd ...

    const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
    const QByteArray text = m_markoffDocument->blockText(id);
    const auto spans = m_markoffDocument->inlineSpansFor(id);
    const quint64 h = computeBlockHash(kind, text, spans, m_fontScale);
    if (m_blockHashes.value(id, 0) == h) {
        ++m_hashSkipsLastPass;
        bytePos = blockEndBytes;
        if (i + 1 < blocks.size()) bytePos += kSepLen;
        continue;
    }
    m_blockHashes[id] = h;

    // ... existing per-kind dispatch (Heading / Paragraph / CodeBlock /
    //     BlockQuote / ListItem / HorizontalRule / fallback Paragraph) ...
    // ... existing inline-span application loop ...
}

// Prune stale entries from m_blockHashes.
for (auto it = m_blockHashes.begin(); it != m_blockHashes.end(); ) {
    if (!currentIds.contains(it.key())) {
        it = m_blockHashes.erase(it);
    } else {
        ++it;
    }
}
```

**Note:** The existing inline-span loop uses `m_markoffDocument->inlineSpansFor(id)` again — change it to use the local `spans` variable so we don't fetch twice per block. Replace:

```cpp
for (const Markoff::SourceSpan &span : m_markoffDocument->inlineSpansFor(id)) {
```

with:

```cpp
for (const Markoff::SourceSpan &span : spans) {
```

inside the inline-format loop body of `applyFormats()`.

- [ ] **Step 7: Clear hash cache when `rerender()` is called**

In `libs/markoff-styled/src/StyleApplier.cpp`, replace the `rerender()` body:

```cpp
void StyleApplier::rerender() {
    if (!m_textDocument || !m_markoffDocument) return;
    m_blockHashes.clear();  // Force every block to apply on next pass.
    applyFormats();
}
```

The existing callers — `setTextDocument`, `setMarkoffDocument`, `setTheme`, `setFontScale` — already call `rerender()`, so they pick up the clear automatically.

- [ ] **Step 8: Expose `hashSkips()` through `Editor` for tests**

In `libs/markoff-styled/include/markoff/styled/Editor.h`, add to the public section (after `textEdit()` accessor):

```cpp
/// Test-only accessor: number of hash-skipped blocks during the
/// most recent StyleApplier restyle pass.
quint64 styleApplierHashSkips() const;
```

In `libs/markoff-styled/src/Editor.cpp`, add the include for `StyleApplier.h` if not already there (it should be from Task 4 of the original plan), then add the body:

```cpp
quint64 Editor::styleApplierHashSkips() const {
    return m_styleApplier ? m_styleApplier->hashSkips() : 0;
}
```

- [ ] **Step 9: Build and run test**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
scripts/run-tests.sh --bin tst_styled_dogfood_invariants
```
Expected: PASS — `hash_gate_skips_unchanged_blocks` passes; `e.styleApplierHashSkips() == 9` (one block changed, nine skipped).

- [ ] **Step 10: Run the full styled suite to verify no regressions**

```bash
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: all 7 prior styled binaries pass, plus the new `tst_styled_dogfood_invariants`. The `QEXPECT_FAIL` slot in `tst_styled_d2_integration` may now pass or still fail depending on whether kind transition has been landed — leave it as-is for now (Task 2 promotes it).

- [ ] **Step 11: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): per-block content hash gates restyle pass

StyleApplier::applyFormats now skips blocks whose (kind, text, spans,
fontScale) hash hasn't changed since the prior pass. On a single-char
edit in one block of N, only that one block's formats are reapplied —
the layout invalidation shrinks proportionally, fixing the bulk of the
scroll lag and viewport jump dogfood findings. Stale entries pruned at
end of pass. setTheme/setFontScale/setMarkoffDocument clear the cache
via rerender()."
```

---

## Task 2: Kind-transition pass with deferred `Cmd::changeKind`

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.h` — add `m_pendingKindChanges` member, slot for deferred dispatch
- Modify: `libs/markoff-styled/src/StyleApplier.cpp` — add `inferKindFromPrefix` helper, queue kind changes, deferred dispatch via `QTimer::singleShot`
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` — add `kind_transition_paragraph_to_heading` slot
- Modify: `libs/markoff-styled/tests/tst_styled_d2_integration.cpp` — remove `QEXPECT_FAIL` from `remote_edit_replays_text_and_restyles`

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`, append the new slot inside the class (before the closing `};`):

```cpp
    void kind_transition_paragraph_to_heading() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("plain"));
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);

        const Markoff::BlockId id =
            *doc.blockAnchorAt(0);  // first block

        // Sanity: starts as Paragraph.
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Paragraph);

        // Prepend "## " to the block content, turning it into a heading.
        doc.applyFlatEdit(0, 0, QByteArrayLiteral("## "),
                          Markoff::Origin::UserEdit);

        // StyleApplier should infer Heading and emit Cmd::changeKind
        // on the next event-loop tick. Wait for the model to update.
        QTRY_COMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);

        // And the QTextBlock should now render at heading size.
        const QTextBlock blk =
            e.textEdit()->document()->findBlockByNumber(0);
        QTRY_VERIFY(blk.charFormat().fontPointSize() > 11.0);
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
scripts/run-tests.sh --bin tst_styled_dogfood_invariants
```
Expected: FAIL on `kind_transition_paragraph_to_heading` — `blockKind(id)` stays `Paragraph` because no kind inference runs.

- [ ] **Step 3: Add `m_pendingKindChanges` + slot to StyleApplier.h**

In `libs/markoff-styled/src/StyleApplier.h`, add the includes:
```cpp
#include <markoff/core/BlockKind.h>
#include <vector>
```

Inside the class, in the private section near `m_blockHashes`:
```cpp
struct PendingKindChange {
    Markoff::BlockId   id;
    Markoff::BlockKind newKind;
};
std::vector<PendingKindChange> m_pendingKindChanges;
```

Add a private slot:
```cpp
private Q_SLOTS:
    void applyPendingKindChanges();
```

(If `private Q_SLOTS:` already has `onD2Changed`, append `applyPendingKindChanges` to the same section.)

- [ ] **Step 4: Add `inferKindFromPrefix` helper in StyleApplier.cpp**

In `libs/markoff-styled/src/StyleApplier.cpp`, add the include at the top:
```cpp
#include <QRegularExpression>
#include <QTimer>
#include <markoff/core/Cmd/D2.h>
```

In the anonymous namespace, add:
```cpp
Markoff::BlockKind inferKindFromPrefix(const QByteArray &text,
                                       Markoff::BlockKind currentKind) {
    if (text.isEmpty()) return Markoff::BlockKind::Paragraph;

    // Heading: 1-6 '#' followed by space, OR 1-6 '#' followed by EOF.
    int hashCount = 0;
    while (hashCount < text.size() && hashCount < 7 && text[hashCount] == '#')
        ++hashCount;
    if (hashCount >= 1 && hashCount <= 6) {
        if (hashCount == text.size()
            || text[hashCount] == ' '
            || text[hashCount] == '\n') {
            return Markoff::BlockKind::Heading;
        }
    }

    // BlockQuote: starts with "> " or is exactly ">".
    if (text.startsWith("> ") || text == ">") {
        return Markoff::BlockKind::BlockQuote;
    }

    // ListItem: ^[ \t]{0,3}([-*+]|\d+[.)])\s — same as markoff-live.
    static const QRegularExpression listRe(
        QStringLiteral("^[ \\t]{0,3}([-*+]|\\d+[.)])\\s"));
    if (listRe.match(QString::fromUtf8(text)).hasMatch()) {
        return Markoff::BlockKind::ListItem;
    }

    // CodeBlock and HorizontalRule inference deferred to v0.2
    // (fence-state matching, not pure prefix). Currently we rely on
    // the CRDT load path to set these correctly.
    if (currentKind == Markoff::BlockKind::CodeBlock
        || currentKind == Markoff::BlockKind::HorizontalRule) {
        return currentKind;  // preserve, don't reinfer.
    }

    return Markoff::BlockKind::Paragraph;
}
```

- [ ] **Step 5: Queue kind changes inside `applyFormats()`**

In `libs/markoff-styled/src/StyleApplier.cpp::applyFormats()`, inside the block-iteration loop, AFTER the hash-match check (so we only infer for blocks that actually changed) and BEFORE the per-kind dispatch:

```cpp
m_blockHashes[id] = h;

// Kind transition: if text prefix disagrees with stored kind, queue a
// Cmd::changeKind for deferred dispatch. The current pass still
// formats using `kind` (the stored kind) — the next d2 cycle, after
// changeKind lands, will format using the corrected kind.
const Markoff::BlockKind inferred = inferKindFromPrefix(text, kind);
if (inferred != kind) {
    m_pendingKindChanges.push_back({id, inferred});
}

// ... existing per-kind dispatch using `kind` ...
```

At the END of `applyFormats()`, after `endEditBlock()` and after `m_applyingFormats = false;`, schedule deferred dispatch:

```cpp
if (!m_pendingKindChanges.empty()) {
    QTimer::singleShot(0, this, &StyleApplier::applyPendingKindChanges);
}
```

- [ ] **Step 6: Implement `applyPendingKindChanges`**

In `libs/markoff-styled/src/StyleApplier.cpp`, add the method body:

```cpp
void StyleApplier::applyPendingKindChanges() {
    if (!m_markoffDocument) {
        m_pendingKindChanges.clear();
        return;
    }
    auto changes = std::move(m_pendingKindChanges);
    m_pendingKindChanges.clear();
    for (const auto &chg : changes) {
        Markoff::Cmd::changeKind(*m_markoffDocument, chg.id, chg.newKind);
    }
}
```

(`Cmd::changeKind` is a free function in `Markoff::Cmd` namespace — confirmed in `libs/markoff-core/include/markoff/core/Cmd/D2.h:55`. Empty `attrNames`/`attrValues` default args are fine for prefix-inferred transitions.)

- [ ] **Step 7: Remove the `QEXPECT_FAIL` from existing integration test**

In `libs/markoff-styled/tests/tst_styled_d2_integration.cpp`, find the `remote_edit_replays_text_and_restyles` slot and remove the `QEXPECT_FAIL` line (if present). The slot body assertions should now pass on their own.

If the file uses `QEXPECT_FAIL(..., Continue)` to expect-and-continue, also remove that line entirely.

- [ ] **Step 8: Build and run**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: PASS — both `kind_transition_paragraph_to_heading` (new) and `remote_edit_replays_text_and_restyles` (promoted from QEXPECT_FAIL) pass. All other slots stay green. All 8 styled binaries now pass.

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): kind transition via Cmd::changeKind

StyleApplier infers block kind from text prefix using rules ported
from markoff-live::KindTransition (Heading via leading #, BlockQuote
via '> ', ListItem via list-marker regex). When the inferred kind
disagrees with the stored CRDT kind, Cmd::changeKind is queued for
deferred dispatch via QTimer::singleShot(0). The next d2 cycle picks
up the corrected kind and applies the right format. This fixes the
'headings render as body text' dogfood finding and promotes the
remote_edit_replays_text_and_restyles QEXPECT_FAIL slot to passing."
```

---

## Task 3: Scroll-position preservation

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.h` — add `setTextEdit` setter + `m_textEdit` member
- Modify: `libs/markoff-styled/src/StyleApplier.cpp` — capture+restore scroll around the block walk; structural-change detection
- Modify: `libs/markoff-styled/src/Editor.cpp` — call `m_styleApplier->setTextEdit(m_editor)` in constructor
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` — add `scroll_preserved_on_inplace_edit` slot

- [ ] **Step 1: Extend the failing test**

In `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`, append the new slot inside the class:

```cpp
    void scroll_preserved_on_inplace_edit() {
        Markoff::Styled::Editor e;
        Markoff::MarkoffDocument doc(1);
        // Build a 50-block document so scrolling matters.
        QByteArray src;
        for (int i = 0; i < 50; ++i) {
            src += QByteArrayLiteral("paragraph ");
            src += QByteArray::number(i);
            src += QByteArrayLiteral("\n\n");
        }
        // Drop the final separator.
        src.chop(2);
        doc.loadFromMarkdown(src);
        auto *s = doc.createSession();
        e.setSession(s);
        e.setDocument(&doc);
        e.resize(400, 200);
        e.show();
        QTRY_VERIFY(e.isVisible());

        // Scroll to roughly the middle.
        auto *bar = e.textEdit()->verticalScrollBar();
        QVERIFY(bar->maximum() > 0);  // sanity
        const int target = bar->maximum() / 2;
        bar->setValue(target);
        QCOMPARE(bar->value(), target);

        // Locate the first block's byte range and append a character
        // (in-place edit, no structural change).
        const Markoff::BlockId firstId = *doc.blockAnchorAt(0);
        const auto range = doc.blockByteRange(firstId);
        Q_UNUSED(range);
        // Simpler: append a char to the first block via flat edit at
        // position 1 (mid-block on the first block).
        doc.applyFlatEdit(1, 1, QByteArrayLiteral("X"),
                          Markoff::Origin::UserEdit);

        // Spin the event loop for the d2 cycle.
        QTest::qWait(50);

        // Scroll position must be preserved (in-place edit, no
        // structural change). Allow a tiny tolerance for layout drift
        // in the edited block.
        QVERIFY(qAbs(bar->value() - target) <= 5);
    }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
scripts/run-tests.sh --bin tst_styled_dogfood_invariants
```
Expected: FAIL — scroll bar jumps to near 0 (Qt's "ensure cursor visible" after layout change).

- [ ] **Step 3: Add `setTextEdit`/`m_textEdit` to StyleApplier.h**

In `libs/markoff-styled/src/StyleApplier.h`, add to the includes:
```cpp
#include <QPointer>
```

Forward-declare near the top of `Markoff::Styled`:
```cpp
class QTextEdit;
```

(If a Qt forward declaration is needed, use `QT_BEGIN_NAMESPACE class QTextEdit; QT_END_NAMESPACE` — but a normal forward-declare works since QTextEdit is in the global namespace.)

Actually, the cleanest pattern is just to include `<QTextEdit>` since the constructor already does. Forward-declare with `class QTextEdit;` at file scope.

In the public section, add:
```cpp
/// Set the QTextEdit whose viewport scrollbar should be captured
/// and restored across in-place restyle passes. Optional; if not
/// set, scroll preservation is a no-op.
void setTextEdit(QTextEdit *edit);
QTextEdit *textEdit() const noexcept { return m_textEdit.data(); }
```

In the private member section, add:
```cpp
QPointer<QTextEdit> m_textEdit;
```

- [ ] **Step 4: Implement `setTextEdit` in StyleApplier.cpp**

In `libs/markoff-styled/src/StyleApplier.cpp`, add the include:
```cpp
#include <QScrollBar>
#include <QTextEdit>
```

Add the body:
```cpp
void StyleApplier::setTextEdit(QTextEdit *edit) {
    m_textEdit = edit;
}
```

- [ ] **Step 5: Capture + restore scroll around `applyFormats`**

In `libs/markoff-styled/src/StyleApplier.cpp::applyFormats()`, immediately after the `m_applyingFormats = true;` line and BEFORE the `QSignalBlocker`, snapshot the scrollbar value and the current block IDs:

```cpp
m_applyingFormats = true;

// Snapshot scroll position + previous block IDs for in-place
// detection. If the block set is unchanged after the pass, we restore
// the saved scroll; otherwise we let Qt do its thing.
const int savedScroll = (m_textEdit && m_textEdit->verticalScrollBar())
    ? m_textEdit->verticalScrollBar()->value() : -1;
QHash<Markoff::BlockId, char> previousBlockIds;
for (auto it = m_blockHashes.constBegin();
     it != m_blockHashes.constEnd(); ++it) {
    previousBlockIds.insert(it.key(), 0);
}
```

(`QHash<BlockId, char>` is used as a set — value is unused. Matches the `currentIds` shape from Task 1 for symmetric comparison.)

At the END of `applyFormats()`, after the prune loop and BEFORE `m_applyingFormats = false;`, compare `previousBlockIds` to `currentIds` and decide whether to restore:

```cpp
// Detect structural change: did any block ID disappear or appear?
bool structural = previousBlockIds.size() != currentIds.size();
if (!structural && !previousBlockIds.isEmpty()) {
    for (auto it = currentIds.constBegin();
         it != currentIds.constEnd(); ++it) {
        if (!previousBlockIds.contains(it.key())) {
            structural = true;
            break;
        }
    }
}
// previousBlockIds.isEmpty() means first pass after setMarkoffDocument
// — not "structural change," but Qt's natural scroll behavior is
// correct on first paint (cursor at start, scroll at top). Skip
// restore in this case too.
if (!structural && !previousBlockIds.isEmpty() && savedScroll >= 0
    && m_textEdit && m_textEdit->verticalScrollBar()) {
    m_textEdit->verticalScrollBar()->setValue(savedScroll);
}

++m_restyleCount;
m_applyingFormats = false;
```

**Note:** the `previousBlockIds` snapshot must happen BEFORE the block walk modifies `m_blockHashes`. Make sure the snapshot loop runs before the `for (... iterateBlocks() ...)` loop.

- [ ] **Step 6: Wire `setTextEdit(m_editor)` in `Editor` constructor**

In `libs/markoff-styled/src/Editor.cpp::Editor::Editor(...)`, find the line that creates the StyleApplier:
```cpp
m_styleApplier = new StyleApplier(this);
m_styleApplier->setTextDocument(m_editor->document());
m_styleApplier->setTheme(&m_theme);
```

Add the new wiring AFTER the existing setters:
```cpp
m_styleApplier->setTextEdit(m_editor);
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build-dev -j 8
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: PASS — `scroll_preserved_on_inplace_edit` passes; all other slots stay green.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-styled
git commit -m "feat(styled): preserve scroll position on in-place edits

StyleApplier::applyFormats now snapshots verticalScrollBar->value()
before the block walk and restores it after endEditBlock — but only
when the block set is unchanged (in-place edit, no structural insert/
remove). Combined with Task 1's hash gate, single-character edits no
longer jump the viewport around. Editor wires setTextEdit(m_editor) so
the applier has a handle to the scrollbar."
```

---

## Task 4: `LinkInteraction::resolveLinkAt` fast path

**Files:**
- Modify: `libs/markoff-styled/src/LinkInteraction.cpp`

This task does not add new tests — the existing `tst_styled_link_interaction` covers correctness (the new fast path must produce the same results), and performance is not directly tested in v0.1.

- [ ] **Step 1: Confirm `MarkoffDocument::blockAt` and helpers are available**

```bash
grep -n 'blockAt\|textAnchorAt\|offsetInBlock' /home/clinton/dev/Markoff/libs/markoff-core/include/markoff/core/MarkoffDocument.h
```

Expected output should include:
- `TextAnchor textAnchorAt(quint32 byteOffset, bool rightBias) const;`
- `std::optional<BlockAnchor> blockAt(const TextAnchor &) const;`
- `int offsetInBlock(const BlockAnchor &, const TextAnchor &) const;`

If any are missing, STOP — the spec assumed they exist (validated during brainstorm). Report blocked.

- [ ] **Step 2: Rewrite `resolveLinkAt` to use `blockAt`**

In `libs/markoff-styled/src/LinkInteraction.cpp`, replace the existing `resolveLinkAt` body. The current implementation walks all blocks linearly; the new one bisects via the CRDT index.

The current signature stays the same:
```cpp
std::optional<Markoff::LinkActivation>
LinkInteraction::resolveLinkAt(int charPos, Qt::KeyboardModifiers mods) const
```

New body:
```cpp
{
    if (!m_doc || !m_edit) return std::nullopt;

    // Convert Qt UTF-16 char position to UTF-8 byte offset in the
    // document's flat view. The flat view is what the QTextDocument
    // mirrors (per SourceTextDocumentBinding's contract), so a char
    // offset there maps to the matching byte offset here.
    const QString plain = m_edit->toPlainText();
    if (charPos < 0 || charPos > plain.size()) return std::nullopt;
    const QByteArray prefix = plain.left(charPos).toUtf8();
    const quint32 byteOffset = static_cast<quint32>(prefix.size());

    // Bisect to the containing block via the CRDT index.
    const Markoff::TextAnchor anchor =
        m_doc->textAnchorAt(byteOffset, /*rightBias=*/false);
    const auto blockAnchorOpt = m_doc->blockAt(anchor);
    if (!blockAnchorOpt) return std::nullopt;
    const Markoff::BlockId id = *blockAnchorOpt;

    // Offset within the block, in UTF-8 bytes.
    const int blockByteOffset = m_doc->offsetInBlock(*blockAnchorOpt, anchor);

    // Convert block-byte-offset to UTF-16 char offset within the
    // block by walking the block text once. (Block text is typically
    // <1KB; this loop is cheap.)
    const QByteArray blockBytes = m_doc->blockText(id);
    const QString blockChars = QString::fromUtf8(blockBytes);
    const QByteArray blockBytesPrefix = blockBytes.left(blockByteOffset);
    const int blockCharPos = QString::fromUtf8(blockBytesPrefix).size();

    // Walk only this block's spans.
    for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(id)) {
        if (!span.isLink && !span.isWikilink) continue;
        if (blockCharPos < span.charOffset) continue;
        if (blockCharPos >= span.charOffset + span.charLength) continue;

        Markoff::LinkActivation a;
        a.kind        = span.isWikilink ? Markoff::LinkKind::WikiLink
                                        : Markoff::LinkKind::External;
        a.rawText     = span.isWikilink ? span.linkTarget.page
                                        : span.linkTarget.url;
        a.modifiers   = mods;
        a.fromContext = m_fromContext;
        return a;
    }
    return std::nullopt;
}
```

- [ ] **Step 3: Build**

```bash
cmake --build build-dev -j 8
```
Expected: clean build.

- [ ] **Step 4: Run the full styled suite**

```bash
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: all 8 styled binaries pass. Specifically:
- `tst_styled_link_interaction` (7 slots) covers click + hover + leave + idempotency + lazy DefaultLinkService. All must still pass with the new fast-path implementation.

If any slot fails, the new path produces different `BlockId`/`charPos` resolution than the old path — investigate.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled/src/LinkInteraction.cpp
git commit -m "perf(styled): LinkInteraction::resolveLinkAt fast path via blockAt

Use MarkoffDocument::blockAt(TextAnchor) for O(log N) block lookup
instead of linear scan over iterateBlocks(). Only walk spans of the
containing block. Eliminates a hot loop on every mouse move (including
scroll-synthetic moves), the second contributor to dogfood scroll lag."
```

---

## Task 5: Documentation — close v0 gaps, log new smells

**Files:**
- Modify: `libs/markoff-styled/CLAUDE.md` — close the kind-transition gap, note new restyle-incrementality + scroll-preserve invariants
- Modify: `docs/queue.md` — Discipline Log entry for the new `QTimer::singleShot` smell in `applyPendingKindChanges`
- Modify: `CLAUDE.md` (project root) — bump the markoff-styled entry to mention v0.1 fixes

- [ ] **Step 1: Update `libs/markoff-styled/CLAUDE.md`**

Read `libs/markoff-styled/CLAUDE.md` first. In the "Known v0 gaps" section, REMOVE the entry beginning "**Kind transition on `applyFlatEdit`**" (the entire bullet — three or four lines including its sub-text).

In its place, add a new "## v0.1 invariants" section just above "Known v0 gaps":

```markdown
## v0.1 invariants

- **Per-block hash gating.** `StyleApplier::applyFormats` skips blocks
  whose `(kind, text, spans, fontScale)` hash is unchanged. Test:
  `tst_styled_dogfood_invariants::hash_gate_skips_unchanged_blocks`.
  When adding new format inputs (e.g., a new `SourceSpan` flag), extend
  the bit-pack in `computeBlockHash` to include it, or risk a missed
  restyle on the change.
- **Kind transition via `Cmd::changeKind`.** Prefix-rule kind
  inference (Heading via leading `#`, BlockQuote via `> `, ListItem
  via list-marker regex) runs inside the block walk; on disagreement
  with the stored kind, `Cmd::changeKind` is queued for deferred
  dispatch via `QTimer::singleShot(0)` to avoid synchronous re-entry
  into `d2DocumentChanged`. CodeBlock and HorizontalRule are NOT
  inferred (fence-state matching; left to the CRDT load path until
  v0.2).
- **Scroll position preserve.** In-place edits (no block added/removed)
  preserve `verticalScrollBar()->value()`. Structural edits let Qt's
  natural "ensure cursor visible" behavior position the viewport.
```

The "Known v0 gaps" section now starts with delimiter-visibility (the kind-transition bullet is gone).

- [ ] **Step 2: Add Discipline Log entry to `docs/queue.md`**

Find the Discipline Log section in `docs/queue.md` (the entries added during Task 16 of the v0 plan). Append:

```
2026-05-27 libs/markoff-styled/src/StyleApplier.cpp `applyPendingKindChanges`, invariant 6 — QTimer::singleShot(0) defers Cmd::changeKind out of d2DocumentChanged slot; the smell is justified per spec §4.4 (avoids synchronous CRDT re-entry during cascade). Mirrors markoff-live::LiveListModelBinding pattern.
```

(Use the same date format and bullet style as the existing entries.)

- [ ] **Step 3: Update project root `CLAUDE.md`**

In `/home/clinton/dev/Markoff/CLAUDE.md`, find the markoff-styled entry in the Layout section (added during the v0 wrap-up). Append a line noting v0.1:

Look for the existing entry that starts with `- libs/markoff-styled        — third view leaf`. Add a sentence near the end:

```
... `Markoff::Styled::Editor` is the public widget. Spec
`docs/specs/2026-05-26-markoff-styled-leaf-design.md`. v0.1 added
per-block hash gating + kind transition + scroll-position preserve
(`docs/specs/2026-05-27-markoff-styled-dogfood-fixes-design.md`).
```

- [ ] **Step 4: Run the full styled suite one more time to confirm all green**

```bash
scripts/run-tests.sh -R '^tst_styled_'
```
Expected: 8 styled binaries, all passing. `tst_styled_d2_integration::remote_edit_replays_text_and_restyles` now passes without `QEXPECT_FAIL`.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-styled/CLAUDE.md docs/queue.md CLAUDE.md
git commit -m "docs(styled): v0.1 invariants documented; v0 kind-transition gap closed

Per-leaf CLAUDE.md now lists three v0.1 invariants (hash gating, kind
transition, scroll preserve) and removes the corresponding entry from
the Known v0 gaps list. Project root CLAUDE.md references the dogfood-
fixes spec. Discipline Log records the QTimer::singleShot(0) smell in
applyPendingKindChanges with its spec justification."
```

---

## Self-review checklist

After implementing all tasks, run through this once before declaring v0.1 complete:

- [ ] All 8 test binaries pass under `scripts/run-tests.sh -R '^tst_styled_'`.
- [ ] `markoff-styled-app` launches on a real markdown file and:
  - Headings render at heading sizes.
  - Scroll is smooth — no 200ms lag after mouse-wheel.
  - Typing does not cause the viewport to jump.
- [ ] No new failures in `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` baseline.
- [ ] `git log --oneline 800eb27..HEAD` shows the 5 task commits with their `fix(styled):` / `feat(styled):` / `perf(styled):` / `docs(styled):` prefixes.

If anything fails, fix in a follow-up commit on the same branch — do not amend prior commits.
