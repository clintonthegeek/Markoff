# Setext Heading buffer canonicalisation

**Date:** 2026-05-29
**Arc:** WP-unification flat-view follow-ups (queue #8.2)
**Scope:** `libs/markoff-core`
**Related:** `fa3d9ce` (Paragraph canonicalisation, 2026-05-29),
  `845fc0f` (ListItem canonicalisation + bullet rendering, 2026-05-29),
  `docs/specs/2026-05-28-flat-view-wp-unification-design.md` (parent arc).

## 1. Problem

Setext-form headings (CommonMark `Title\n=======` for H1,
`Title\n-------` for H2) load with their underline byte range included
in the block buffer. With WP unification in effect, the buffer is
handed to a `QTextDocument` that treats every `\n` as a paragraph
boundary, so:

- The styled view renders the underline as a literal second
  QTextBlock below the title.
- The source view shows the `=` / `-` line as if it were body text.
- Multi-line setext titles (CommonMark allows soft-breaks before the
  underline) also break into per-line QTextBlocks.

This is exactly the failure mode `fa3d9ce` fixed for Paragraph and
`845fc0f` fixed for ListItem. The CLAUDE.md banner already names
setext Heading as the open peer; this spec closes it.

The B1 invariant ("block buffers hold content only; no internal `\n`")
must hold on the load ingress for setext headings the same way it now
holds for Paragraph and ListItem.

## 2. Approach

Two coupled changes — load-side strip + save-side reconstruct.

### 2.1 Load — `MarkoffDocument::materializeBlocksFromParsedDoc`

Both `kind == BlockKind::Heading` and `tb.kind == TLB::Kind::SetextHeading`
are already in scope at the buffer-content step (the
`HeadingForm = "setext"` attr is set a few lines up using the same
test). After the existing trailing-`\n` chop and before the per-kind
soft-break collapse:

1. If setext, find the last `\n` in `content` and truncate from that
   `\n` onward. That removes the underline line.
2. Extend the existing `\n → space` collapse to include
   `BlockKind::Heading` when the form is setext, so multi-line titles
   fold to a single space (matches Paragraph semantic).

After this step, a setext buffer contains the title text only — no
underline, no internal `\n`. The `HeadingForm = "setext"` attr remains
the load-time record of the original form; `level` (already set,
values 1 or 2) selects the underline character on save.

ATX headings (`# Title`) are unaffected — they have no internal `\n`
to collapse and no underline to strip; this branch is gated on
`tb.kind == TLB::Kind::SetextHeading`.

### 2.2 Save — `serializeHeading` (`BlockSerializers.cpp:63`)

Today's setext branch returns `content` verbatim, trusting the buffer
to already carry the underline. With the buffer now content-only it
must reconstruct:

```cpp
if (form == "setext" && (level == 1 || level == 2)) {
    char c = (level == 1) ? '=' : '-';
    return content + "\n" + QByteArray(content.size(), c);
}
```

Width = title's UTF-8 byte length. For pure-ASCII titles this matches
visual width. For multibyte titles the underline ends up "too long" in
glyphs — harmless: CommonMark requires `≥1` and the result still parses
back as setext.

### 2.3 Round-trip — `serializeForSave` fast-path bypass

`serializeForSave` (`MarkoffDocument.cpp:2170-2183`) has a fast path
for untouched blocks that emits `blockLoadTimeBytes` verbatim. That
cache stores the **post**-canonicalisation buffer (set at
`MarkoffDocument.cpp:1925`, after the `\n` collapse). After this
spec, the cache for a setext block contains the title only
(`"Heading"`), no underline. If the fast path emits that, the doc
round-trips to a plain paragraph — heading shape lost.

The fast path therefore needs a setext-aware bypass: for
`kind == Heading && HeadingForm == "setext"`, route through
`reg.get(kind)` (i.e. `serializeHeading`) even when untouched, so the
reconstruction in §2.2 always runs for setext.

Consequences:

- **Untouched setext** — emits reconstructed `title\n<underline>`.
  Underline length = title byte length, not the original source
  width. Heading shape preserved; underline width drifts toward
  matching the title. Acceptable per the brainstorm decision.
- **Touched setext** — same reconstruction path; same width rule.
  Hard-wrapped multi-line titles collapse to a single space
  (consistent with Paragraph behaviour landed in `fa3d9ce`).
- **All other untouched kinds** — fast path unchanged, still emits
  `blockLoadTimeBytes` verbatim.

### 2.4 Why not parser-side?

An alternative would be teaching the parser to expose a separate
"title byte range" for setext headings. That's more invasive (touches
`harvestHeading` and `TLB`) for a narrow benefit. Load-site
post-processing is contained to the canonicalisation step alongside
the Paragraph and ListItem peers, keeping the three load-time
canonicalisations together for the next reader.

## 3. Tests

Add to `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp`,
the home of the just-landed Paragraph/ListItem invariants:

1. `setext_h1_strips_underline_from_buffer` — load
   `"Heading\n=======\n"`, assert buffer is `"Heading"` (no `\n`,
   no `=`).
2. `setext_h2_strips_underline_from_buffer` — same for `-` underline.
3. `setext_multiline_title_collapses_to_space` — source
   `"line1\nline2\n=====\n"`, buffer is `"line1 line2"`.
4. `setext_h1_save_reconstructs_underline_after_edit` — load setext H1,
   mutate the title via `d2ApplyBufferEdit` (e.g. append "X"), save,
   assert serialized output ends with `\n` + underline of correct
   character and length matching the new title.
5. `setext_h2_save_reconstructs_underline_after_edit` — same for level 2.
6. `setext_untouched_roundtrip_is_byte_identical` — load a setext doc,
   serialize immediately, assert byte-equal to source. Guards the
   `blockLoadTimeBytes` fast path stays effective.

### Falsifiability proof

Per `docs/INVARIANTS.md` invariant 4, the load-side fix is provable
falsifiable by temporarily reverting the strip-from-last-`\n` step —
tests #1, #2, #3 must fail (buffer still carries the underline /
soft-breaks). The serializer fix is provable by reverting to verbatim
emit — test #4 / #5 must fail (output is `"NewTitleX"` with no
reconstructed underline). Commit the proof commits, then revert
in-history per the established pattern.

## 4. Scope and exclusions

**In scope:**
- Load-side setext canonicalisation (point 2.1).
- Save-side underline reconstruction (point 2.2).
- `serializeForSave` fast-path bypass for setext (point 2.3).
- New tests in `tst_block_buffer_invariant` (point 3).
- CLAUDE.md banner update (top-level + `libs/markoff-core/CLAUDE.md`
  + view-implementor's guide §0) noting setext now joins
  Paragraph/ListItem as load-canonicalised.
- Queue #8.2 closeout.

**Out of scope:**
- BlockQuote `\n` collapse (queue #8.1; different shape — per-line
  `> ` markers in the byte range).
- Hash gate extension to attrs (queue #8.5).
- Source-view list-item marker reconstruction (queue #8.3).
- Ordered-list continuous numbering (queue #8.4).
- Parser-side surfacing of a separate title byte range for setext
  (alternative considered in §2.4; rejected for scope).

## 5. Files touched

- `libs/markoff-core/src/MarkoffDocument.cpp` — extend the
  canonicalisation block in `materializeBlocksFromParsedDoc` (~6 lines
  + comment update).
- `libs/markoff-core/src/BlockSerializers.cpp` — reconstruct the
  underline in `serializeHeading`'s setext branch (~5 lines + comment
  update).
- `libs/markoff-core/src/MarkoffDocument.cpp` (separate site from the
  load change) — `serializeForSave` fast-path bypass for setext
  (~6 lines + comment).
- `libs/markoff-core/tests/d2/tst_block_buffer_invariant.cpp` — six
  new slots (~120 lines).
- `libs/markoff-core/CLAUDE.md` — banner: move setext from "still
  retains \n" to canonicalised list.
- `CLAUDE.md` (top-level) — same banner update.
- `docs/VIEW-IMPLEMENTORS-GUIDE.md` §0 — same update.
- `docs/queue.md` — close #8.2.

## 6. Definition of done

- All six new test slots pass against the implementation; the
  falsifiability proof commits land (and are reverted) showing each
  branch of the fix is necessary.
- Existing `tst_block_buffer_invariant` Paragraph/ListItem slots
  remain green.
- Full fast suite (`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`)
  remains at the 2026-05-29 baseline (249/254 pass) — no new
  regressions; no change in the 5 pre-existing failures listed in
  the top-level CLAUDE.md banner.
- Banners updated; queue #8.2 closed in `docs/queue.md`.
- Commit message references this spec.

## 7. Risks and notes

- **Heading kinds that aren't setext but somehow carry an internal
  `\n`** — the fix is gated on `tb.kind == TLB::Kind::SetextHeading`,
  so ATX headings flow through unchanged. If a future parser path
  starts injecting `\n`s into ATX heading buffers, that's its own
  spec.
- **Setext heading demoted to paragraph at runtime via `Cmd::changeKind`**
  — the kind attr flips to `Paragraph` and the next save goes through
  `serializeParagraph` instead. The stale `HeadingForm` attr in the
  attrs map is harmless (no reader without `kind == Heading` consults
  it). No cleanup required.
- **`blockLoadTimeBytes` and `isBlockTouched`** — `isBlockTouched`
  is edit-sequence-based (`MarkoffDocument.cpp:2034-2043`), not a
  byte comparison against the cache. The cache is purely an emit
  shortcut. Setext bypassing the cache means `isBlockTouched` is
  not consulted on the setext path — the reg serializer runs
  regardless. No correctness impact on touched-flag semantics.
- **Untouched setext underline width drift** — explicitly acknowledged
  in §2.3. The original underline width (e.g. `============================`)
  is not preserved through a load+save cycle. Width on save matches
  title byte length. CommonMark accepts any width ≥ 1, so the file
  re-parses as the same setext heading. If preserving original width
  becomes a requirement, store it in an attr at load and use it on
  save (option C from the brainstorm; out of scope here).
