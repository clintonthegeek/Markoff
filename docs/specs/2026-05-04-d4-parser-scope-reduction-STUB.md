# D4 — Parser scope reduction (STUB)

**Date stub created:** 2026-05-04
**Status:** STUB — intended scope and inputs captured. **Substantive design happens after D2 lands** and probably parallels D3 (D4 has no D3 dependency; can run concurrently). Practically D4 is the smallest of the post-D1 phases.

---

## What this stub is for

Orient a fresh agent context picking up D4. Not a substantive design.

---

## Inputs

1. **`docs/specs/2026-05-04-d2-foundation-reshape-design.md` §6** — the parser scope contract. D2 commits to using only two parser surfaces: `Document::fromMarkdown` at load time + `inlineSpansFor(content, kind)` per-block. D4 trims the parser library to expose only those two and deletes everything else.
2. **`libs/markoff-parser/` source tree** — the parser library to trim. Specifically:
   - `ParsePool` (the worker thread that owns long-lived `IncrementalParseSession`)
   - `IncrementalParseSession` and its `parseIncremental({edit}, newBody)` API
   - Document-wide incremental reparse infrastructure (block-tree edit/parse, inline-tree shifting via `sortedEdits`)
   - The `parseUpdated` signal plumbing
3. **What survives.** `Document::fromMarkdown` (used at load); `Document::extract` (frontmatter + footnote harvesting at load); a small `inlineSpansFor(QByteArray, BlockKind) → InlineSpanTree` function (used per-block, on calling thread).

---

## Intended scope

D4 deletes:

1. `ParsePool::schedule(QByteArray)` and `ParsePool::scheduleReset(QByteArray)`.
2. The `ParsePool` class entirely (no callers after D2 + D3 ship).
3. `IncrementalParseSession` and its `parseIncremental(...)` public API.
4. The document-wide incremental machinery in `TreeSitterParser` — block-tree `ts_tree_edit` + reuse, inline-tree shifting through `sortedEdits`, `parseIncremental` orchestration.
5. The `parseUpdated` signal chain through `MarkoffDocument` (the signal itself retires in D2 §8.3; D4 deletes the emit-side plumbing).

D4 introduces / refines:

1. The `inlineSpansFor` API as a clean function (today it's tangled inside the document-wide parse path).
2. Parser library tests for the two surviving surfaces only.
3. CMake / dependency cleanup — anything that was needed only by the deleted machinery.

---

## Explicitly out of scope

- **Foundation API changes.** D4 doesn't touch `MarkoffDocument` or the foundation library. It only touches `libs/markoff-parser`.
- **Tree-sitter grammar changes.** The Markdown grammar stays. D4 trims the parser-library wrapper around tree-sitter, not tree-sitter itself.
- **Per-block parse cache.** Lives in the foundation (`InlineParseCache` per D2 §2.1). D4 doesn't own it.

---

## Open questions for D4 brainstorm time

- Does D4 land before D3 (parser library cleanup is independent), in parallel with D3, or after D3 (be conservative; only delete after the consumer has stopped using)? Recommend after D3, to be safe.
- `Document::extract` shape — does it stay coupled with `Document::fromMarkdown` or split out for explicit load-only use?
- Tests: does D4 keep `markoff-parser`'s existing test suite (most of which exercises `parseIncremental`) or rewrite for the trimmed API?

---

## Brainstorming checklist (when ready)

1. Read inputs above.
2. Invoke `superpowers:brainstorming`.
3. Resolve open questions.
4. Write substantive D4 spec, replace this stub. Update D-arc status board + roadmap.
