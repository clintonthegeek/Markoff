# Hash gate over block attrs — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `StyleApplier::computeBlockHash` to cover the block's full attrs map so attr-only mutations (`toggleListItemChecked`, `IndentLevel` rewrites, marker-style flips) trigger restyle.

**Architecture:** Add `attrs` parameter to `computeBlockHash`; XOR-combine per-attr contributions (`qHash(key) * mix ^ value-hash`). XOR is order-insensitive — sidesteps QHash's unstable iteration order. Single call-site change in `applyFormats`. One new falsifiable test in `tst_styled_dogfood_invariants`.

**Tech Stack:** C++20, Qt6 (Core/Gui/Widgets/Test), CMake.

**Authoritative spec:** [`../specs/2026-05-29-styled-hash-gate-over-attrs-design.md`](../specs/2026-05-29-styled-hash-gate-over-attrs-design.md).

**Build/test:**
- Build: `cmake --build build-dev --target tst_styled_dogfood_invariants -j 8`
- Run: `QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants`
- Fast suite: `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
- 2026-05-29 baseline: 249/254. Pre-existing failures unchanged.

---

## Task 1: Tests + production change (single commit)

**Files:**
- Modify: `libs/markoff-styled/src/StyleApplier.cpp` — extend `computeBlockHash` + update call site in `applyFormats`.
- Modify: `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp` — new slot.

- [ ] **Step 1: Write the failing test**

In `tst_styled_dogfood_invariants.cpp`, add a slot declaration (next to existing dogfood slots) and definition. The exact existing fixtures and helpers used by the test file should be reused — read the file for the load + cascade idioms before writing.

The slot must:
1. Construct a `MarkoffDocument` + `Editor` + `StyleApplier`, load `"- [ ] task\n"` (one ListItem block, `markerStyle = "task"`, `Checked = false`).
2. Process events so the initial cascade applies formats.
3. Assert the QTextBlock's `blockFormat().marker()` is `QTextBlockFormat::MarkerType::Unchecked`.
4. Flip the `Checked` attr via the foundation primitive (`MarkoffDocument::d2SetBlockAttr` or whichever the existing code uses for attr writes — discover by `grep -n 'setWithNextStamp.*Checked\|toggleListItemChecked' libs/markoff-core/src/`).
5. Process events.
6. Assert the QTextBlock's `blockFormat().marker()` is now `QTextBlockFormat::MarkerType::Checked`.

The slot should be named `attr_toggle_re_renders_task_marker`.

Add it to the existing `private slots:` declarations near the top of the test class.

- [ ] **Step 2: Build and run; the new slot must FAIL**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants attr_toggle_re_renders_task_marker
```

Expected: FAIL with marker still `Unchecked` after the toggle. This is the falsifiability proof for the hash-gate extension.

- [ ] **Step 3: Extend `computeBlockHash` signature and body**

In `libs/markoff-styled/src/StyleApplier.cpp`, find `quint64 computeBlockHash(...)` at `:233` and replace the function with:

```cpp
quint64 computeBlockHash(Markoff::BlockKind kind,
                         const QByteArray &text,
                         const QList<Markoff::SourceSpan> &spans,
                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
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
    // Attrs: XOR-combine per-entry (order-insensitive). Each entry mixes
    // key-name + value into a per-entry quint64 that XORs into h.
    // Spec: docs/specs/2026-05-29-styled-hash-gate-over-attrs-design.md.
    for (auto it = attrs.cbegin(); it != attrs.cend(); ++it) {
        quint64 entry = qHash(it.key());
        entry *= 0x9E3779B97F4A7C15ULL;
        std::visit([&](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int>) {
                entry ^= quint64(v) * 0xBF58476D1CE4E5B9ULL;
            } else if constexpr (std::is_same_v<T, bool>) {
                entry ^= v ? 1ULL : 2ULL;
            } else if constexpr (std::is_same_v<T, QString>) {
                entry ^= qHash(v);
            } else {
                static_assert(sizeof(T) == 0,
                              "Unhandled AttrValue alternative");
            }
        }, it.value());
        h ^= entry;
    }
    // Mix in fontScale (cast to quint64 bits for stable hashing).
    quint64 fsBits = 0;
    std::memcpy(&fsBits, &fontScale, sizeof(fsBits));
    h ^= fsBits;
    return h;
}
```

The headers `<variant>` and `<type_traits>` are needed; check whether they're already in via existing includes before adding.

- [ ] **Step 4: Update the call site in `applyFormats`**

In `libs/markoff-styled/src/StyleApplier.cpp`, find the call to `computeBlockHash` at `:423` (inside the block walk in `applyFormats`). Insert an attrs lookup just before the hash computation and pass it through:

```cpp
            const Markoff::BlockKind kind = m_markoffDocument->blockKind(id);
            const QList<Markoff::SourceSpan> spans = m_markoffDocument->inlineSpansFor(id);
            const auto attrs = m_markoffDocument->blockAttrs(id);
            const quint64 h = computeBlockHash(kind, text, spans, attrs, m_fontScale);
```

Then in the ListItem branch (around line 475), replace the existing `const auto attrs = m_markoffDocument->blockAttrs(id);` with a reuse of the outer `attrs` lookup (the local one shadows the outer; remove it). If reusing is clumsy because of scoping, leave both lookups in — `blockAttrs` is a simple QHash copy, performance impact is negligible.

- [ ] **Step 5: Build and re-run; the new slot must PASS**

```bash
cmake --build build-dev --target tst_styled_dogfood_invariants -j 8
QT_QPA_PLATFORM=offscreen ./build-dev/bin/tst_styled_dogfood_invariants
```

Expected: all slots pass, including `attr_toggle_re_renders_task_marker` and the existing `hash_gate_skips_unchanged_blocks`.

- [ ] **Step 6: Spot-check neighbours**

```bash
cmake --build build-dev --target tst_styled_d2_integration tst_styled_inline_formats tst_styled_block_formats tst_styled_binding_caret -j 8
for t in tst_styled_d2_integration tst_styled_inline_formats tst_styled_block_formats tst_styled_binding_caret; do printf "%-40s " "$t"; QT_QPA_PLATFORM=offscreen ./build-dev/bin/$t 2>&1 | grep "Totals:"; done
```

Expected: `tst_styled_block_formats` keeps its 2 pre-existing failures (queue #8.7), nothing else regresses. The 2 pre-existing failures (`heading_levels_descend_in_size`, `horizontal_rule_uses_monospace`) are unrelated to attr hashing.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-styled/src/StyleApplier.cpp libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp
git commit -m "$(cat <<'EOF'
feat(styled): hash gate over block attrs

computeBlockHash now covers (kind, text, spans, attrs, fontScale)
— previously attrs were left out, so attr-only mutations
(toggleListItemChecked, IndentLevel rewrites, MarkerStyle flips)
left the hash unchanged and skipped block-format reapplication.
A toggled task checkbox would not flip its native marker until
something else moved the hash.

Per-attr contribution is XOR-combined into the hash (commutative;
sidesteps QHash's non-deterministic iteration order across Qt
versions). AttrValue's variant alternatives (int, bool, QString)
each get a small mix; an unhandled alternative wedge static_assert
catches future variant additions at compile time.

The hash is over-conservative for attrs that don't actually
affect rendering (e.g. LooseRun, MarkerNumber) — they trigger a
spurious format re-application when they change. Acceptable
trade-off vs. an allowlist that drifts as applyFormats grows new
attr reads.

New tst_styled_dogfood_invariants slot pins the contract:
attr_toggle_re_renders_task_marker toggles Checked on a task
ListItem and asserts the QTextBlock's marker flips to Checked
after the cascade. hash_gate_skips_unchanged_blocks remains
green (no-op cascade still produces zero re-applications).

Spec: docs/specs/2026-05-29-styled-hash-gate-over-attrs-design.md
Plan: docs/plans/2026-05-29-styled-hash-gate-over-attrs.md
Queue: closes #8.5.
EOF
)"
```

---

## Task 2: Docs + queue closeout

**Files:**
- Modify: `libs/markoff-styled/CLAUDE.md` — update the v0.1 invariants section.
- Modify: `docs/queue.md` — close #8.5.

- [ ] **Step 1: Update `libs/markoff-styled/CLAUDE.md`**

Find the bullet starting "Hash gate is text-only (caveat)" and replace with:

```
- **Per-block hash gating covers attrs (2026-05-29).** `computeBlockHash`
  hashes `(kind, text, spans, attrs, fontScale)`. Attr-only mutations
  (`toggleListItemChecked`, `IndentLevel` rewrite, marker-style flip)
  produce a fresh hash and trigger restyle. Per-attr XOR-combine is
  order-insensitive (sidesteps QHash iteration drift). Over-conservative
  on attrs that don't affect rendering (e.g. `LooseRun`) — acceptable
  vs. an allowlist that drifts as `applyFormats` grows new attr reads.
```

Also adjust the "Per-block hash gating" earlier bullet to drop the
"text-only" framing — its description should now refer to attrs as
part of the gate.

- [ ] **Step 2: Close queue #8.5**

In `docs/queue.md`, find the #8.5 entry:

```
5. **Hash gate covers attrs.** `computeBlockHash` hashes
   `(kind, text, spans, fontScale)`. Attr-only mutations
   (`toggleListItemChecked`, `IndentLevel` rewrites, marker-style change)
   leave text unchanged → hash unchanged → block format skipped → stale
   render. Extend the bit-pack to include the attrs that affect the
   render path (MarkerStyle, IndentLevel, Checked, HeadingForm). Touches
   `StyleApplier::computeBlockHash` and the new test:
   "checking a task item without text change restyles the marker."
```

Replace with:

```
5. ~~**Hash gate covers attrs.**~~ → closed 2026-05-29 in commit `<hash>`.
   `computeBlockHash` now mixes the full attrs map via XOR (order-
   insensitive). Slightly over-conservative on attrs that don't drive
   rendering — acceptable vs. an allowlist that drifts. Spec:
   `docs/specs/2026-05-29-styled-hash-gate-over-attrs-design.md`.
   Plan: `docs/plans/2026-05-29-styled-hash-gate-over-attrs.md`.
```

Substitute `<hash>` with the short SHA from Task 1's commit
(`git log --oneline -2`).

- [ ] **Step 3: Commit docs**

```bash
git add libs/markoff-styled/CLAUDE.md docs/queue.md
git commit -m "$(cat <<'EOF'
docs: hash-gate-over-attrs invariants + queue #8.5 closeout

Update libs/markoff-styled/CLAUDE.md to reflect that the per-block
hash gate now covers attrs. Closes queue #8.5 with a back-reference
to the spec + plan + implementation commit.
EOF
)"
```

---

## Task 3: Full-suite verification

- [ ] **Step 1: Run the fast suite**

```bash
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'
```

Expected: 249/254 baseline preserved. The same 5 pre-existing failures
remain. No new failures.

If a non-baseline test fails, the most likely culprit is the
attrs lookup tripping a code path that didn't expect it (e.g. a
test using a synthetic block without a properly populated attrs
map). The `blockAttrs` lookup on a freshly-minted block returns
an empty QHash; that should be hash-stable (the for-loop body
doesn't execute, h is unchanged).

- [ ] **Step 2: Report**

Summarise commits + test counts + deltas. Plan complete.

---

## Self-review

Spec coverage:
- §2 approach (XOR-combine) → Task 1 Step 3.
- §3 call-site → Task 1 Step 4.
- §4 test → Task 1 Steps 1-2 + 5.
- §5 doc/queue updates → Task 2.
- §7 def-of-done suite check → Task 3.

Placeholder scan: `<hash>` in Task 2 Step 2 is intentional, with explicit `git log` instruction.
