# Setext Heading buffer canonicalisation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the third member of the WP-unification load-canonicalisation trio. Setext-form headings should land in the model with content-only buffers (no underline, no internal `\n`), and round-trip through save by reconstructing the underline from `(content.size(), level)`.

**Architecture:** Three coupled production-code edits in `markoff-core`: (a) load-side strip in `materializeBlocksFromParsedDoc`, (b) underline reconstruction in `serializeHeading`, (c) setext fast-path bypass in `serializeForSave`. The three changes ship as a single commit (per `fa3d9ce` and `845fc0f` pattern) along with six new `tst_block_buffer_invariant` slots.

**Tech Stack:** C++20, Qt6 (Core/Test), CMake.

**Authoritative spec:** [`../specs/2026-05-29-setext-heading-buffer-canonicalisation-design.md`](../specs/2026-05-29-setext-heading-buffer-canonicalisation-design.md). Read it (and `docs/INVARIANTS.md`) before starting.

**Reference commits (just-landed peers):**
- `fa3d9ce` — Paragraph canonicalisation (~12 lines core, ~40 lines tests).
- `845fc0f` — ListItem canonicalisation + bullet rendering.

**Build/test commands:**
- Build the test binary: `cmake --build build-dev --target tst_block_buffer_invariant -j 8`
- Run that binary offscreen: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant [slotName]`
- Fast suite: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
- Known pre-existing failures (do NOT let them block; 2026-05-29 baseline = 249/254): `tst_live_render_e2_nav_shift_extend`, `tst_live_render_focus_chokepoint_invariant`, `tst_live_render_cursor_typing_invariant`, `tst_styled_block_formats::heading_levels_descend_in_size`, `tst_styled_block_formats::horizontal_rule_uses_monospace`.

---

## Task 1: Tests + production code (single commit per fa3d9ce pattern)

**Files:**
- Modify: `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp` — add six new slots.
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp:1900-1919` — extend canonicalisation block to handle setext.
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp:2170-2183` — add setext fast-path bypass in `serializeForSave`.
- Modify: `libs/markoff-core/src/BlockSerializers.cpp:73-82` — reconstruct underline in `serializeHeading`'s setext branch.

- [ ] **Step 1: Add six new test slot declarations and corpus entries**

In `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`, add to the slot-decl block (after `listitem_buffers_have_no_internal_newlines()` at line 69):

```cpp
    void setext_h1_strips_underline_from_buffer();
    void setext_h2_strips_underline_from_buffer();
    void setext_multiline_title_collapses_to_space();
    void setext_h1_save_reconstructs_underline_after_edit();
    void setext_h2_save_reconstructs_underline_after_edit();
    void setext_untouched_roundtrip_is_byte_identical();
```

- [ ] **Step 2: Add six new slot implementations**

Append these slot bodies before `QTEST_MAIN(TstBlockBufferInvariant)` at line 188:

```cpp
// Setext headings ("Title\n========" for H1, "Title\n--------" for H2) used
// to land with the underline bytes in the block buffer; flat-view leaves
// then rendered the underline as a literal second QTextBlock. The load
// path now strips the underline (everything from the last '\n' onward in
// the byte range) and collapses any soft-breaks in multi-line titles to
// space. HeadingForm="setext" attr is already set so the serializer can
// reconstruct on save.

void TstBlockBufferInvariant::setext_h1_strips_underline_from_buffer()
{
    const QByteArray source = "Heading\n========\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    const QByteArray text = doc.blockText(blocks[0]);
    QCOMPARE(text, QByteArrayLiteral("Heading"));
    QVERIFY(!text.contains('\n'));
    QVERIFY(!text.contains('='));
}

void TstBlockBufferInvariant::setext_h2_strips_underline_from_buffer()
{
    const QByteArray source = "Heading\n--------\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    const QByteArray text = doc.blockText(blocks[0]);
    QCOMPARE(text, QByteArrayLiteral("Heading"));
    QVERIFY(!text.contains('\n'));
    // Note: bare "-" never appears in "Heading" so a literal !contains('-')
    // assertion would be redundant. The QCOMPARE above is the contract.
}

void TstBlockBufferInvariant::setext_multiline_title_collapses_to_space()
{
    // CommonMark allows the setext title to wrap across multiple source
    // lines; soft-breaks inside the title collapse to space the same way
    // Paragraph soft-breaks do.
    const QByteArray source = "line one\nline two\n==========\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    QCOMPARE(doc.blockText(blocks[0]), QByteArrayLiteral("line one line two"));
}

void TstBlockBufferInvariant::setext_h1_save_reconstructs_underline_after_edit()
{
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(QByteArrayLiteral("Heading\n========\n"));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    const BlockId id = blocks[0];

    // Append "X" at end of title.
    doc.d2ApplyBufferEdit(id, /*byteOffset=*/7, /*removeBytes=*/0,
                          QByteArrayLiteral("X"), Origin::Local);
    QCOMPARE(doc.blockText(id), QByteArrayLiteral("HeadingX"));

    const QByteArray saved = doc.serializeForSave();
    QCOMPARE(saved, QByteArrayLiteral("HeadingX\n========\n"));
}

void TstBlockBufferInvariant::setext_h2_save_reconstructs_underline_after_edit()
{
    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(QByteArrayLiteral("Sub\n---\n"));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    const BlockId id = blocks[0];

    // Append "head" at end.
    doc.d2ApplyBufferEdit(id, /*byteOffset=*/3, /*removeBytes=*/0,
                          QByteArrayLiteral("head"), Origin::Local);
    QCOMPARE(doc.blockText(id), QByteArrayLiteral("Subhead"));

    const QByteArray saved = doc.serializeForSave();
    QCOMPARE(saved, QByteArrayLiteral("Subhead\n-------\n"));
}

void TstBlockBufferInvariant::setext_untouched_roundtrip_is_byte_identical()
{
    // The `setext-h1` and `setext-h2` fixtures in kCorpus already exercise
    // roundtrip_stability(). This slot is the explicit by-name guard so a
    // future corpus reshuffle can't hide a setext regression.
    const QByteArray source = "Heading\n========\n";

    MarkoffDocument doc(/*replicaId=*/1);
    doc.loadFromMarkdown(source);

    const QByteArray saved = doc.serializeForSave();
    // After the fix, the fast-path bypass routes untouched setext through
    // serializeHeading. Underline width = title byte length (7) — coincides
    // with the source's 8-char underline being shortened to 7. CommonMark
    // accepts any width >=1, still parses as the same heading.
    QCOMPARE(saved, QByteArrayLiteral("Heading\n=======\n"));
}
```

Also add this include at the top alongside the existing includes:

```cpp
#include <markoff/core/Origin.h>
```

- [ ] **Step 3: Build the test binary and run it. All six new slots must FAIL.**

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant
```

Expected: existing slots (`no_load_terminator`, `roundtrip_stability`, `paragraph_buffers_have_no_internal_newlines`, `listitem_buffers_have_no_internal_newlines`) pass. The six new slots **all fail**: tests 1–3 because buffer still contains `\n` and underline bytes; tests 4–5 because serializer emits verbatim and the edited buffer contains the original underline; test 6 because the untouched fast path emits the full original load-time bytes (`"Heading\n========"`), giving `"Heading\n========\n"` (8 `=`s, not 7). That last failure is delicate — see the QCOMPARE expected value above; the 8→7 shift is the visible signature of reconstruction.

This is the natural falsifiability proof for the production fix: each new slot demonstrates an HEAD bug the fix will close.

- [ ] **Step 4: Implement load-side strip + soft-break collapse**

In `libs/markoff-core/src/MarkoffDocument.cpp`, replace the existing canonicalisation block (currently lines 1904–1919, the `if (kind == BlockKind::Paragraph || kind == BlockKind::ListItem)` clause and its preceding comment):

```cpp
        // CommonMark "soft line break" rule: a single '\n' between non-blank
        // lines inside a paragraph (or a list item's text content, or a
        // setext heading's title) renders as whitespace. Storing the raw
        // source bytes would leave hard-wrap '\n's inside the buffer, which
        // then become spurious QTextBlock boundaries in flat-view leaves
        // (markoff-styled, markoff-source). Collapse to a single space at
        // load time so these kinds honour the B1 "no internal '\n'"
        // invariant on the load ingress, matching applyFlatEdit.
        //
        // For setext headings the byte range also covers the underline line
        // ("Title\n========"); strip from the last '\n' onward FIRST so the
        // soft-break collapse only touches title interior. HeadingForm="setext"
        // attr (set above) preserves the form for serializeHeading to
        // reconstruct the underline on save.
        //
        // ListItem is safe to collapse because harvestListItem already
        // narrows the byte range to the item's content child (post-marker);
        // the marker syntax never enters the buffer. BlockQuote still
        // retains its internal '\n's pending separate marker-aware handling
        // (`> ` strip).
        const bool isSetext = (kind == BlockKind::Heading
                               && tb.kind == TLB::Kind::SetextHeading);
        if (isSetext) {
            const int lastNl = content.lastIndexOf('\n');
            if (lastNl >= 0)
                content.truncate(lastNl);
        }
        if (kind == BlockKind::Paragraph
            || kind == BlockKind::ListItem
            || isSetext) {
            content.replace('\n', ' ');
        }
```

- [ ] **Step 5: Build and re-run. Tests 1, 2, 3 should now PASS; test 6 should still FAIL (different reason — fast path now emits `"Heading"` with no underline at all); tests 4, 5 should still FAIL (serializer still verbatim).**

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant
```

Also run the broader fixture-driven slots — `roundtrip_stability` will now FAIL for `setext-h1` and `setext-h2` (the parameterised rows) because the fast path emits `"Heading"` without an underline. That failure is the signal we need the fast-path bypass next.

- [ ] **Step 6: Implement serializer reconstruction in `serializeHeading`**

In `libs/markoff-core/src/BlockSerializers.cpp`, replace the setext branch (currently lines 73–82):

```cpp
    // Setext form: the buffer is content-only (the load path strips the
    // underline; the per-block edit ingress preserves the no-internal-'\n'
    // invariant). Reconstruct the underline from level: '=' for L1, '-' for
    // L2. Underline width = title byte length so ASCII titles get a visually
    // matching rule; multibyte titles end up with a "too long" underline in
    // glyphs but CommonMark accepts any width >=1 and re-parses correctly.
    // Only valid for level 1 / 2 per CommonMark; fall through to ATX
    // otherwise (defensive).
    auto fmIt = attrs.constFind("headingForm");
    if (fmIt != attrs.cend()) {
        if (const QString *p = std::get_if<QString>(&fmIt.value())) {
            if (*p == QStringLiteral("setext") && (level == 1 || level == 2)) {
                const char c = (level == 1) ? '=' : '-';
                return content + "\n" + QByteArray(content.size(), c);
            }
        }
    }
```

- [ ] **Step 7: Implement setext fast-path bypass in `serializeForSave`**

In `libs/markoff-core/src/MarkoffDocument.cpp`, replace the untouched/touched branch in `serializeForSave` (currently lines 2170–2183):

```cpp
        QByteArray bytes;
        // Setext headings need reconstruction even when "untouched":
        // blockLoadTimeBytes stores the post-canonicalisation buffer
        // (content only — no underline), so emitting it verbatim would
        // produce a plain paragraph on reload. Route untouched setext
        // through the reg serializer so serializeHeading can reconstruct
        // the underline. Width drifts toward title length (acceptable;
        // see 2026-05-29-setext-heading-buffer-canonicalisation-design.md
        // §7).
        const bool isSetextHeading =
            kind == BlockKind::Heading
            && std::holds_alternative<QString>(
                blockAttrs(id).value("headingForm", AttrValue{}))
            && std::get<QString>(
                blockAttrs(id).value("headingForm", AttrValue{}))
                == QStringLiteral("setext");
        if (!isBlockTouched(id) && !isSetextHeading) {
            // Untouched: use original load-time bytes for byte-identical
            // content round-trip. Strip the load-time terminator so the
            // serializer owns separator placement (B1 §3).
            bytes = d->blockLoadTimeBytes.value(id);
            if (bytes.endsWith('\n'))
                bytes.chop(1);
        } else {
            // Touched (or setext): re-serialize from CRDT state. Per-kind
            // serializer is contracted to emit body only — no terminator
            // (B1 §4).
            auto fn = reg.get(kind);
            bytes = fn(kind, blockAttrs(id), blockText(id));
        }
        out += bytes;
```

Note: `AttrValue` is `std::variant<...>` defined in `libs/markoff-core/include/markoff/core/BlockAttrs.h`. The `holds_alternative<QString>` + `get<QString>` pattern matches existing call sites (e.g. `BlockSerializers.cpp:77-81`). If a simpler accessor exists in `BlockAttrs.h` (e.g. `getStringOr(...)`), prefer that — check before settling on the variant pattern.

- [ ] **Step 8: Build and run all `tst_block_buffer_invariant` slots. All must PASS.**

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant
```

Expected: 100% pass, including the `setext-h1` / `setext-h2` rows under `roundtrip_stability_data`. If `roundtrip_stability` still fails for setext, the fast-path bypass is misrouting — re-check the `isSetextHeading` condition.

- [ ] **Step 9: Quick spot-check that nothing else broke immediately**

Build and run the closest neighbours that exercise the load + serialize paths:

```bash
cmake --build build-dev --target tst_d2_reset_content tst_d2_widget_flat_view tst_styled_d2_integration tst_source_paragraph_margins -j 8
for t in tst_d2_reset_content tst_d2_widget_flat_view tst_styled_d2_integration tst_source_paragraph_margins; do
    QT_QPA_PLATFORM=offscreen ./build-dev/bin/$t || echo "FAIL: $t"
done
```

Expected: all pass. If a binding-side test fails, the most likely culprit is the fast-path bypass condition leaking into a non-setext code path — re-check `isSetextHeading`.

- [ ] **Step 10: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/BlockSerializers.cpp \
        libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp
git commit -m "$(cat <<'EOF'
feat(core): canonicalise setext heading buffers; reconstruct on save

CommonMark setext headings ("Title\n========" / "Title\n--------")
landed with the underline bytes in the block buffer, so flat-view
leaves rendered the underline as a literal second QTextBlock and
multi-line titles broke into per-line QTextBlocks. Closes the third
member of the WP-unification load-canonicalisation trio (peers:
fa3d9ce Paragraph, 845fc0f ListItem).

Three coupled changes in markoff-core:

* materializeBlocksFromParsedDoc: for setext (kind==Heading +
  tb.kind==SetextHeading), strip from the last '\n' onward (drops
  the underline line) before extending the existing soft-break
  collapse to include the title.
* serializeHeading: the setext branch now reconstructs the underline
  from (content, level) instead of emitting the buffer verbatim.
  Width = content.size() bytes; '=' for L1, '-' for L2.
* serializeForSave: untouched-block fast path bypasses for setext —
  blockLoadTimeBytes is the post-canonicalisation buffer (no underline),
  so emitting it verbatim would round-trip as a plain paragraph.
  Setext routes through serializeHeading regardless of touched state.

Trade-off: original underline width is not preserved across a
load+save cycle. Width on save matches title byte length; CommonMark
accepts any width >=1 and re-parses as the same setext heading.

Six new tst_block_buffer_invariant slots pin the three behaviours
(strip H1, strip H2, multi-line title collapse, save-reconstruct H1,
save-reconstruct H2, untouched-byte-identical guard). The
roundtrip_stability fixture-driven slots already exercise
setext-h1/setext-h2 and now cover the fast-path bypass too.

Spec: docs/specs/2026-05-29-setext-heading-buffer-canonicalisation-design.md
Plan: docs/plans/2026-05-29-setext-heading-buffer-canonicalisation.md
Queue: #8.2.
EOF
)"
```

---

## Task 2: Falsifiability proofs (revert each branch; witness fail)

Per `docs/INVARIANTS.md` invariant 4, the new tests must be provably falsifiable. The natural falsification — running the new tests against HEAD before any production fix — was demonstrated in Task 1 Step 3. This task makes the three-way decomposition explicit by reverting each production change in turn, running the tests, and committing the proof commit. Each proof commit will be reverted before moving on.

**Files modified:** same three production files as Task 1; reverted in three commits.

- [ ] **Step 1: Proof A — revert the load-side strip**

In `MarkoffDocument.cpp` revert ONLY the load-side change from Task 1 Step 4 (restore the pre-fix `if (kind == BlockKind::Paragraph || kind == BlockKind::ListItem)` block; remove the `isSetext` lines). Keep the serializer + fast-path bypass changes intact.

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant setext_h1_strips_underline_from_buffer setext_h2_strips_underline_from_buffer setext_multiline_title_collapses_to_space
```

Expected: all three slots FAIL (buffer still carries `\n` and underline bytes).

Commit the broken state with `proof:` prefix so it's identifiable in history:

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "proof: revert setext load-side strip; tests 1-3 fail"
```

Then revert the proof commit:

```bash
git revert --no-edit HEAD
```

- [ ] **Step 2: Proof B — revert the serializer reconstruction**

In `BlockSerializers.cpp` revert ONLY the `serializeHeading` setext-branch change (restore `return content;` verbatim). Keep the other two changes intact.

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant setext_h1_save_reconstructs_underline_after_edit setext_h2_save_reconstructs_underline_after_edit
```

Expected: both reconstruction slots FAIL — serialized output equals `"HeadingX"` / `"Subhead"` instead of `"HeadingX\n========\n"` / `"Subhead\n-------\n"`.

```bash
git add libs/markoff-core/src/BlockSerializers.cpp
git commit -m "proof: revert setext serializer reconstruction; tests 4-5 fail"
git revert --no-edit HEAD
```

- [ ] **Step 3: Proof C — revert the fast-path bypass**

In `MarkoffDocument.cpp`'s `serializeForSave`, revert the bypass condition (restore the unconditional `if (!isBlockTouched(id))` branch; drop the `isSetextHeading` short-circuit). Keep load-side strip and serializer reconstruction intact.

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant setext_untouched_roundtrip_is_byte_identical roundtrip_stability
```

Expected: `setext_untouched_roundtrip_is_byte_identical` FAILS (output is `"Heading\n"`, the bare post-canon buffer, because fast path emits `blockLoadTimeBytes` verbatim). `roundtrip_stability` FAILS for `setext-h1` and `setext-h2` rows.

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "proof: revert setext fast-path bypass; test 6 + roundtrip fail"
git revert --no-edit HEAD
```

- [ ] **Step 4: Verify post-proof tree is identical to post-Task-1 tree**

```bash
git diff HEAD~6 HEAD -- libs/markoff-core/src/MarkoffDocument.cpp libs/markoff-core/src/BlockSerializers.cpp libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp
```

Expected: empty diff. Three proof/revert pairs cancel out exactly.

```bash
cmake --build build-dev --target tst_block_buffer_invariant -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_block_buffer_invariant
```

Expected: all slots pass (back to the Task 1 Step 8 state).

---

## Task 3: Docs / banners / queue closeout

**Files:**
- Modify: `CLAUDE.md` (top-level) — banner updates.
- Modify: `libs/markoff-core/CLAUDE.md` — "Load ingress — Paragraph kind only" note.
- Modify: `docs/VIEW-IMPLEMENTORS-GUIDE.md` §0 — load-side enforcement note.
- Modify: `docs/queue.md` — close #8.2.

- [ ] **Step 1: Update the top-level CLAUDE.md banner**

In `/home/clinton/dev/Markoff/CLAUDE.md`, update the "Still open from this arc" paragraph (under the 2026-05-29 banner). Find:

```
> - `BlockQuote` and setext `Heading` retain internal `\n`s — their
>   byte ranges include marker syntax (`> `, setext underline) that
>   needs separate marker-aware stripping. Flat-view leaves still
>   render those as multi-line until that's done.
```

Replace with:

```
> - `BlockQuote` retains internal `\n`s — its byte range includes
>   per-line `> ` markers that need marker-aware stripping. Flat-view
>   leaves still render BlockQuotes as multi-line until that's done.
>   Setext `Heading` closed 2026-05-29 in commit <hash> — load-side
>   strip + serializer reconstruction, parallel to Paragraph/ListItem.
```

(Substitute `<hash>` with the short SHA of the Task 1 commit; obtain via `git log --oneline -5`.)

- [ ] **Step 2: Update `libs/markoff-core/CLAUDE.md`**

Find the "Load ingress — Paragraph kind only" paragraph. Replace:

```
**Load ingress — Paragraph kind only (2026-05-29).** `loadFromMarkdown` (via `buildD2FromBytes`) also collapses internal `\n` → space in `Paragraph` block buffers, so CommonMark hard-wrapped paragraphs (one block, source-side `\n`-joined lines) honour the "no internal `\n`" rule on the load ingress too. Other multi-line kinds (`BlockQuote`, `ListItem`, setext `Heading`) still retain their internal `\n`s pending marker-aware handling; flat-view leaves will still see spurious QTextBlock boundaries inside those kinds. See guide §0 "Load-side enforcement".
```

With:

```
**Load ingress canonicalisation (2026-05-29).** `loadFromMarkdown` (via `buildD2FromBytes`) collapses internal `\n` → space in `Paragraph`, `ListItem`, and setext `Heading` block buffers, so the "no internal `\n`" rule (B1) holds on the load ingress for those kinds. Setext headings additionally have their underline line stripped before the collapse; `serializeHeading` reconstructs the underline from `(content.size(), level)` on save (width drift toward title length on touched blocks is accepted; original width is not preserved). `BlockQuote` still retains its internal `\n`s pending marker-aware handling of per-line `> ` markers; flat-view leaves will still see spurious QTextBlock boundaries inside BlockQuotes. See guide §0 "Load-side enforcement".
```

- [ ] **Step 3: Update `docs/VIEW-IMPLEMENTORS-GUIDE.md` §0**

Locate the load-side enforcement note in §0 (the same content the previous step paraphrases). Update to mention setext is now canonicalised; BlockQuote remains the outstanding case. Match the exact wording from the markoff-core CLAUDE.md edit above for consistency.

- [ ] **Step 4: Close queue item #8.2 in `docs/queue.md`**

Find the "## #8 — Flat-view kind follow-ups …" section's item 2:

```
2. **Setext `Heading` internal `\n` collapse.** Buffer for a setext H1/H2
   is `"Title\n======="` or `"Title\n-------"`. Need to drop the underline
   line at load time and collapse the title's soft breaks. Heading
   attrs already carry `HeadingForm = "setext"` so the serializer can
   reconstruct the underline. Small.
```

Replace with:

```
2. ~~**Setext `Heading` internal `\n` collapse.**~~ → closed 2026-05-29 in commit `<hash>`. Load-side strip + soft-break collapse in `materializeBlocksFromParsedDoc`; `serializeHeading` reconstructs underline from `(content.size(), level)`; `serializeForSave` fast-path bypasses for setext (untouched setext goes through reconstruction because `blockLoadTimeBytes` is the post-canon buffer). Width drift on touched blocks accepted (CommonMark accepts ≥1 chars). Spec: `docs/specs/2026-05-29-setext-heading-buffer-canonicalisation-design.md`. Plan: `docs/plans/2026-05-29-setext-heading-buffer-canonicalisation.md`.
```

Also add a Discipline Log entry if anything surfaced during implementation:

```
- 2026-05-29 <file>:<line> — inv #N — <one phrase>
```

(Skip if nothing new.)

- [ ] **Step 5: Verify queue + guide changes parse cleanly**

```bash
grep -n "Setext" docs/queue.md | head -5
grep -n "setext\|Setext" docs/VIEW-IMPLEMENTORS-GUIDE.md | head -10
grep -n "setext\|Setext" libs/markoff-core/CLAUDE.md | head -5
grep -n "Setext" CLAUDE.md | head -5
```

Expected: each file references setext as closed/canonicalised, no remaining "retains internal `\n`" claim for setext.

- [ ] **Step 6: Commit docs**

```bash
git add CLAUDE.md libs/markoff-core/CLAUDE.md docs/VIEW-IMPLEMENTORS-GUIDE.md docs/queue.md
git commit -m "$(cat <<'EOF'
docs: setext heading load-canonicalisation banner + queue #8.2 closeout

Update top-level CLAUDE.md, libs/markoff-core/CLAUDE.md, and the
View Implementor's Guide §0 to reflect that setext headings join
Paragraph and ListItem as load-canonicalised. BlockQuote remains
the outstanding kind. Closes queue #8.2 with a back-reference to
the spec + plan + implementation commit.
EOF
)"
```

---

## Task 4: Full-suite verification

**Files:** none — running existing test suite.

- [ ] **Step 1: Run the fast suite**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 249/254 pass (the 2026-05-29 baseline). The five pre-existing failures listed in the top-level CLAUDE.md banner remain:

- `tst_live_render_e2_nav_shift_extend`
- `tst_live_render_focus_chokepoint_invariant`
- `tst_live_render_cursor_typing_invariant`
- `tst_styled_block_formats::heading_levels_descend_in_size`
- `tst_styled_block_formats::horizontal_rule_uses_monospace`

Plus, **note any change** in `tst_source_widget_format_ops` (the 4-failure cluster from queue #8.6). If those failures shift, log the delta in the Discipline Log — they're tracked as a separate investigation item, but if a setext change accidentally moves the needle it's worth knowing.

If a non-baseline test fails, **stop and diagnose** before claiming completion. The most likely culprits:

- Fast-path bypass condition misfiring — affects every serialized file. Check that the `isSetextHeading` short-circuit is gated tightly (`kind == Heading` + `HeadingForm == "setext"`, both checks).
- Test fixture in the corpus tripping on the new behaviour — extend corpus fixture if the new shape is correct, or fix the production change if not.

- [ ] **Step 2: Report**

Summarise:
- Test count: X pass / Y fail; deltas from 249/254 baseline.
- Pre-existing failures still pre-existing? (yes/no per failure).
- Any new failures? (each with one-line cause if known.)
- Commits landed (short SHAs).

Plan complete when this report shows zero new failures.

---

## Self-review

Spec coverage:
- §2.1 load-side → Task 1 Step 4 + tests 1, 2, 3.
- §2.2 serializer → Task 1 Step 6 + tests 4, 5.
- §2.3 fast-path bypass → Task 1 Step 7 + test 6 + existing roundtrip_stability slots.
- §3 tests → Task 1 Step 2 (all six slots).
- §3 falsifiability proof → Task 2 (three proof/revert pairs).
- §4 banner/queue updates → Task 3.
- §6 Definition of done → Task 4 baseline check.

Placeholder scan: `<hash>` token in Task 3 Steps 1 + 4 is intentional — to be substituted at commit time; explicit instruction to run `git log --oneline -5` to obtain it.

Type consistency: `BlockKind::Heading`, `TLB::Kind::SetextHeading`, `Origin::Local`, `Markoff::AttrValue`, `Markoff::BlockId`, `d->blockLoadTimeBytes`, `serializeForSave`, `serializeHeading`, `isBlockTouched`, `d2ApplyBufferEdit` — all match the existing code as inspected during plan-writing.
