# Footnote Cleanup Design

**Date:** 2026-04-29
**Branch:** `exploration/new-foundation`
**Status:** Approved — ready for implementation plan
**Scope:** Option A (parser-only cleanup). Option B (custom tree-sitter grammar) deferred and documented for future work.

## Problem

`Markoff::Document::extract` mutates the body it returns to the parser:

1. Strips frontmatter (legitimate — body is post-frontmatter source).
2. Extracts footnote definitions `[^label]: content` into `out.footnotes` (legitimate — definitions are metadata).
3. **Removes footnote definition lines from `body`.**
4. **Replaces every `[^label]` reference with `<sup>N</sup>` HTML.**

Items 3 and 4 are the problem.

### Why item 4 is dead transformation

- No view in the repo consumes the `<sup>N</sup>` output.
- `markoff-source-widget` shows raw source via `Document::sourceText()`, not body.
- `markoff-view-qml` Phase 1 shows raw source. Phase 2 (live preview) hasn't started.
- The old `markoff-reading` (deleted in `309f9ce`) had a `SpanRenderer` that fed plain text via `insertText`, not HTML — so even there, `<sup>` would have rendered as literal characters.
- The actual working footnote rendering in the old `markoff-live` highlighter consumed `SourceSpan::isFootnoteRef` (set by `TreeSitterParser`'s post-process when tree-sitter parses `[^N]` as `shortcut_link`). It hid the `^` character and applied `QTextCharFormat::AlignSuperScript`. No `<sup>` HTML was involved.
- Conclusion: the `<sup>` substitution was intended as an HTML convenience for a reading-view pipeline that never wired it up. It is on the per-keystroke hot path of `IncrementalParseSession` for no observable gain.

### Why item 3 is also worth removing

- With definition lines stripped, `body` diverges from the source text on every footnote-definition edit.
- `IncrementalParseSession::diffBodyBytes` operates on body bytes; on definition edits the diff window is wider than the source edit window, costing the parser unnecessary work.
- Tree-sitter parses unstripped `[^label]: content` as a paragraph; harmless to the AST.
- Removing the strip makes `body == source.mid(postFrontmatterOffset)` — body diff trivially matches source diff modulo a constant offset.

### What item 4 actually needed

The future live preview needs **per-reference numbering** (each `[^label]` shows `<sup>N</sup>` where N is assigned in first-reference order). Currently this lives implicit in the substitution loop. Option A surfaces it as a metadata API instead.

## Goals (Option A)

1. **Remove dead substitution.** Drop the `[^label]` → `<sup>N</sup>` substitution loop in `Document::extract`.
2. **Make body == source-after-frontmatter.** Drop the definition-line strip in `Document::extract`.
3. **Surface footnote-ref metadata.** Add `Document::footnoteRefs()` returning `{label, number, sourceOffset}` per reference occurrence so the future live preview can render numbering without re-scanning.
4. **Preserve `Document::footnotes()`.** The definitions list (`{label, number, content}`) stays exactly as-is — that is canonical metadata.
5. **No view changes.** No view currently consumes the dead transformation, so none need to be updated.

## Non-goals (Option A)

- Inline footnotes `^[content]` (Obsidian-only syntax). Deferred to Option B.
- Multi-paragraph indented definitions. Current regex truncates after line 1; Option A leaves this behavior unchanged. Deferred to Option B.
- Replacing the `shortcut_link`-then-flip post-process in `TreeSitterParser`. It works correctly when the parser sees raw `[^N]` text, which is exactly what Option A delivers.
- Live preview rendering of footnote refs. Lives in the future view-qml Phase 2 work.

## Architecture

### Public API additions

```cpp
// libs/markoff-parser/include/markoff-parser/Document.h

namespace Markoff {

struct FootnoteRefInfo {
    QString label;     // e.g. "1", "bignote"
    int     number;    // 1-based, assigned by first-reference order
    int     sourceOffset;  // QString char offset into ExtractedSource::body
                           // (== source minus frontmatter)
};

class Document {
    // ... existing members ...
    QList<FootnoteRefInfo> footnoteRefs() const;
};

} // namespace Markoff
```

### `ExtractedSource` changes

`ExtractedSource` (the value returned by `Document::extract`) gains a parallel field:

```cpp
struct ExtractedSource {
    QString frontmatter;
    int frontmatterBlockStart = -1;
    int frontmatterBlockEnd = -1;
    bool frontmatterEofClose = false;
    QString body;                    // unchanged shape; new content rule (see below)
    QList<FootnoteInfo> footnotes;   // unchanged
    QList<FootnoteRefInfo> refs;     // NEW: ordered by sourceOffset (== first-occurrence order)
};
```

`Document::fromComponents` accepts the new field and stores it on `Private`.

### `Document::extract` body shape

After the cleanup, `body` is exactly the substring of `source` after the frontmatter block. Specifically:

- No frontmatter: `body == source`.
- With frontmatter: `body == source.mid(frontmatterBlockEnd)`.
- No regex transformations applied.
- `[^label]: content` definition lines remain in the body verbatim.
- `[^label]` references remain in the body verbatim.

### `Document::extract` work

```
1. Strip frontmatter (unchanged behavior).
2. Single-pass scan for `[^label]: content` lines:
   - For each match, populate footnoteMap[label] with {label, content, number=0}.
   - Definition lines are NOT removed from `body`.
3. Single-pass scan for `[^label]` references (excluding ones that are part of a
   definition line — see "Definition vs reference disambiguation" below):
   - Assign `number` to the first occurrence of each known label, starting at 1.
   - Append a `FootnoteRefInfo` to the new `refs` list with label, number, and
     sourceOffset (in body coordinates).
4. Sort referenced footnotes by assigned number into `out.footnotes` (unchanged).
5. Set `out.refs` to the populated list (already in offset order).
```

The double-scan (one to number, one to substitute) becomes a single scan (number-and-record). Net cost is one scan of the body for refs plus one for definitions.

### Definition vs reference disambiguation

The reference regex `\[\^([^\]]+)\]` will match the `[^label]` portion of a `[^label]: content` definition line. The current code naively counts those as references too — they get numbered, but since `processed` rebuilds the body using the same regex, definition labels also get substituted to `<sup>N</sup>` and then the definition regex (run *first*) had already removed the line. Net: definition-label refs were silently absorbed.

After the cleanup, definition lines stay in body and get scanned for refs. We must distinguish definition occurrences from real references. Cheapest reliable rule: **a `[^label]` immediately followed by `:` (with optional whitespace before the `:`) is a definition prefix, not a reference.** Implement during the reference scan: peek at the character(s) immediately after the closing `]`; if the next non-whitespace character is `:`, skip.

This rule matches the existing definition regex's anchor (`^\[\^...\]:`), so the two scans are consistent.

### `IncrementalParseSession` impact

No code change. `diffBodyBytes` continues to work on `m_extracted.body` bytes. The behavioral change is that those bytes now equal post-frontmatter source bytes, so the diff window matches the source edit window — the cost reduction is implicit.

### View impact

None. Source widget reads `sourceText()`. View-qml Phase 1 reads `sourceText()`. View-qml Phase 2 (future) will consume `Document::footnoteRefs()` plus `SourceSpan::isFootnoteRef` spans to render `<sup>N</sup>` at draw time, hiding `[^label]:` definition lines via render-layer logic (a known item, not engineered now).

## Components and ownership

| Component | What changes |
|-----------|--------------|
| `libs/markoff-parser/include/markoff-parser/Document.h` | Add `FootnoteRefInfo` struct, `Document::footnoteRefs()` getter, `ExtractedSource::refs` field. |
| `libs/markoff-parser/src/Document.cpp` | Rewrite `extract`: drop substitution loop, drop definition-line removal, fold refs scan into numbering, populate `refs`. Add `Document::footnoteRefs()` returning `d->refs`. |
| `libs/markoff-parser/tests/tst_document.cpp` | Add tests for the new body contract and `footnoteRefs()`. Update any test that asserted `<sup>` in body. |
| `libs/markoff-parser/tests/tst_document_queries.cpp` | Verify `footnotes()` still returns expected results post-cleanup. Verify `footnoteRefs()` returns expected refs for known fixtures. |
| `libs/markoff-foundation/tests/tst_foundation_*` | Verify no regression. None should depend on body == post-substitution form. |
| `libs/markoff-foundation/src/IncrementalParseSession.cpp` | No change required. (Comment update optional.) |
| `docs/TODO.md` | Move follow-up #1 to "landed" once committed. |

## Tests

### New tests

```cpp
// tst_document.cpp

void footnotesPreservedInBody();
// Input: "Text[^1] more.\n\n[^1]: definition.\n"
// Expect: extracted.body == input (no frontmatter, so body equals source).
// Expect: body contains "[^1]" and "[^1]:" verbatim — no <sup>, no stripping.

void noFootnotes_bodyEqualsPostFrontmatter();
// Input: "---\nkey: val\n---\nplain text"
// Expect: body == "plain text".

void footnoteRefsContractSingleRef();
// Input: "Text[^1] more.\n\n[^1]: definition.\n"
// Expect: footnoteRefs() returns one entry: {label="1", number=1, sourceOffset=4}.

void footnoteRefsContractTwoLabelsTwoRefsEach();
// Input contains [^a], [^b], [^a], [^b] (each appearing twice).
// Expect: 4 entries; numbers 1,2,1,2; sourceOffsets ascending.

void footnoteRefsExcludesDefinitionPrefix();
// Input contains "[^1]: defn" — the `[^1]` part of the definition line must
// NOT appear as a ref.

void footnoteRefsUnresolvedReference();
// Input contains "[^missing]" with no matching definition.
// Expect: footnoteRefs() entry exists with label="missing", number=0
// (no definition → no number assigned).
```

### Tests to update

- Any test in `tst_document.cpp` or `tst_document_queries.cpp` that asserts `<sup>` in body output. Search before edit; update to assert raw `[^N]` instead.
- The fingerprint-equivalence tests in `tst_incremental_parse.cpp` operate on parser-level inputs (raw text → tree-sitter), bypassing `Document::extract`. Not affected.

## Error handling

- **Unresolved reference (`[^foo]` with no `[^foo]:` definition):** populate `FootnoteRefInfo` with `number=0`. Live preview can choose to render unresolved refs as raw text (matches Obsidian's behavior). Documented; no special-case code beyond the `number=0` default.
- **Duplicate definition:** current behavior keeps the first definition (QHash::insert with same key replaces, but the `if (footnoteMap[label].number == 0)` guard prevents re-numbering). Post-cleanup behavior is the same; the regex is still single-line so duplicates of the same label keep the last definition's content. Document as a known item; Option B fixes it via tree walk.
- **Empty label `[^]`:** Current regex `[^]\]+` requires at least one character so this never matches. Post-cleanup behavior identical.

## Migration / risk

- **Tree-sitter sees `[^label]:` lines as paragraphs.** Tree-sitter will parse `[^label]` inside a definition line as a `shortcut_link`, and `TreeSitterParser`'s post-process will set `isFootnoteRef = true` on those spans. This is invisible in source mode (current view) and will need handling in live preview (cheap: skip refs whose containing line starts with `[^...]:`). **Logged as a known item; do not engineer a fix in Option A.**
- **Body length grows on documents with footnotes.** Tree-sitter is fed a slightly longer buffer. Parsing cost is linear in length; negligible.
- **Existing fingerprint tests in `tst_incremental_parse.cpp`** operate on synthetic input, not `Document::extract` output. Unaffected.

## Acceptance criteria

1. All existing tests in the fast suite (76) and the slow tail (`tst_benchmark`, `tst_realistic`) pass.
2. `Document::extract(s).body` equals `s` (no frontmatter case) or `s.mid(frontmatterBlockEnd)` (frontmatter case), byte-for-byte.
3. New `Document::footnoteRefs()` returns expected entries for the new test fixtures above.
4. `Document::footnotes()` continues to return canonical metadata (no behavior change).
5. `tst_document.cpp` and `tst_document_queries.cpp` are updated; nothing asserts the old substituted-body shape.
6. `docs/TODO.md` records follow-up #1 (the original "Stop pre-processing inside Document") as landed.

## Option B — custom tree-sitter grammar (deferred)

Captured here so it can be picked up cleanly when live preview lands and the Obsidian-quirk gaps become user-visible.

### What Option A leaves on the table

- **Inline footnotes `^[content]`.** Obsidian-only syntax for footnotes whose content is given inline at the reference site rather than at a separate definition. Obsidian renders these only in Reading View, not Live Preview. Option A doesn't touch them.
- **Multi-paragraph indented definitions.** The current regex `^\[\^([^\]]+)\]:\s*(.+)$` (MultilineOption) captures only line 1. Indented continuation lines are silently truncated. Option A preserves this regression.
- **Definition-line ref leak.** With definition lines now in the body, `TreeSitterParser`'s post-process flags `[^label]:` prefixes as `isFootnoteRef`. Option A documents this and lets live preview filter; Option B eliminates the false positive structurally.
- **`shortcut_link`-then-flip heuristic.** Tree-sitter currently parses `[^N]` as a `shortcut_link` (because the upstream grammar has zero footnote rules) and `TreeSitterParser` post-processes to set `isFootnoteRef`. A native `footnote_reference` node retires this dance.
- **Refs with whitespace `[^my note]`.** Obsidian permits arbitrary characters in labels. Current regex `[^\]]+` is permissive enough to handle this; verify in Option B that the grammar matches.
- **Named-label canonical identity.** Numbers are presentation; labels are the canonical key. Option A surfaces both via `FootnoteRefInfo`; Option B should preserve this.

### Grammar generation pipeline

- Sources at `libs/markoff-parser/src/vendor/tree-sitter-markdown/{tree-sitter-markdown,tree-sitter-markdown-inline}/grammar.js`.
- `tree-sitter generate` regenerates the C parser from `grammar.js`. CMakeLists in the vendor dir invokes it during build.
- Upstream grammar has zero footnote rules — confirmed by repo grep.
- Two paths: **fork-and-rebase** (maintain a Markoff fork branch and rebase on upstream) or **patch series** (apply patches at build time before generate). Recommendation: patch series — smaller maintenance surface, clearer audit trail, easier upstream sync.

### Proposed grammar additions

Block grammar (`tree-sitter-markdown`):

```
footnote_definition := footnote_label ":" inline_content (
                          "\n" continuation_line
                       )*
footnote_label       := "[^" (label characters) "]"
continuation_line    := indent inline_content
                        (where indent is 4 spaces or 1 tab)
```

Inline grammar (`tree-sitter-markdown-inline`):

```
footnote_reference := "[^" (label characters) "]"
inline_footnote    := "^[" inline_content "]"
```

Edge: the inline grammar must distinguish `^[content]` from a leading caret in regular text. Look-ahead constraint on the `^[` token solves this.

### Migration list (when Option B lands)

| Site | Change |
|------|--------|
| `Document::extract` | Drop the regex-based definition scan. Walk the parsed tree for `footnote_definition` nodes via a new helper. |
| `Document::footnoteRefs()` | Re-implement via tree walk for `footnote_reference` and `inline_footnote` nodes. |
| `TreeSitterParser` post-process 3 (line ~796) | Retire — `isFootnoteRef` becomes a structural span flag set during walkNode for `footnote_reference` / `inline_footnote` nodes. |
| `SourceSpan::isFootnoteRef` | Either keep as a flag-with-payload or replace with a richer span shape carrying `label` and `number`. Decision when live preview is ready. |
| `IncrementalParseSession` | No change — incremental parse machinery is grammar-agnostic. |
| Test corpus | Add the cases listed below before turning on the new grammar; verify each. |

### Test corpus needs (Option B)

- Mismatched labels (ref with no def, def with no ref).
- Multi-paragraph definitions with 4-space and tab indent.
- Inline footnote `^[content]` inside bold/italic/code.
- Refs with whitespace, punctuation, unicode in label.
- Definition lines indented beneath a list item.
- Nested formatting inside ref text (Obsidian permits markdown inside refs in Reading View).

### Reference material

- The deleted-leaf survey doc `02-parser-survey.md` (recoverable via `git show 309f9ce -- '**/02-parser-survey.md'`) cited "Penelope's `FootnoteParser` pattern" and identified footnotes as one of the Obsidian features requiring grammar-level support.
- Obsidian's official Help vault is the canonical behavior reference for Obsidian-flavoured footnotes, especially around inline footnotes and Live-Preview-vs-Reading-View rendering differences.
- Old `markoff-live` highlighter (also recoverable from `309f9ce`) had a working `isFootnoteRef`-driven WYSIWYG superscript implementation worth re-reading before rebuilding render code.

### Estimated effort

Multi-day. Grammar work is the bulk; it requires building a working dev loop for `tree-sitter generate` and the C compile, plus a patch-management strategy. Worth doing only when (a) live preview exists and (b) the Obsidian quirks above are user-visible enough to justify.

## Decision log

- **Why parser-only and not also touch view-qml/source-widget?** The TODO listed three libs as the blast radius, but research showed neither view consumes the dead substitution today. View-qml Phase 1 and source-widget show raw source. Live preview is future work that will design its own consumption against the new metadata API. Touching views now is premature.
- **Why drop the definition-line strip and not just the substitution?** The two transformations together are what caused body-diverging-from-source. Dropping only the substitution leaves the definition-line strip in place, which still makes body diff != source diff on def edits. The acceptance criterion "body == post-frontmatter source" is the load-bearing simplification.
- **Why not custom grammar today?** Option B is properly its own epic. The design above gives the future implementer a clean migration path and explicit list of what Option A leaves on the table. Combining now would balloon scope on a branch already committed to landing live preview Phase 1 and beyond.
