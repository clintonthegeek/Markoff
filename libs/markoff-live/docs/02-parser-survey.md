# Markoff: Markdown Parser Survey

## Overview

This document evaluates every viable C/C++ markdown parser for use as Markoff's
parsing foundation, then examines strategies for supporting Obsidian's markdown
extensions.

---

## Parser Candidates

### cmark (CommonMark Reference Implementation)

- **License:** BSD-2-Clause (GPL-compatible)
- **Language:** C99, zero external dependencies
- **Maintenance:** Active. Latest: v0.31.2 (February 2026), tracking spec 0.31.2
- **CommonMark compliance:** 100% — this IS the reference implementation
- **GFM support:** No (vanilla cmark is CommonMark only)

**Architecture:** Full AST parser. `cmark_parse_document()` returns a tree of
`cmark_node` objects. The tree can be traversed, modified, and serialized.
Iterator API provides ENTER/EXIT events per node.

**Built-in renderers:** HTML, groff man, LaTeX, CommonMark (round-trip), XML.

**Extensibility:** None in vanilla cmark. No plugin API, no custom node types,
no hook points for custom syntax. The only customization path is post-processing
the AST after parsing.

**Strengths:**
- Gold standard correctness. If it parses differently from cmark, it's wrong.
- Rock-solid, fuzz-tested, handles pathological input gracefully.
- Clean C API, easy to wrap in C++.
- AST is fully mutable — can insert/remove/modify nodes after parsing.

**Weaknesses:**
- No extension API means Obsidian syntax (wikilinks, callouts, math) must be
  handled entirely outside the parser. Options: pre-process the markdown before
  cmark sees it, or post-process the AST. Both are fragile.
- CommonMark only — no tables, strikethrough, task lists without cmark-gfm.

**Verdict:** Excellent parsing core, but the lack of extensions makes it
insufficient on its own for Obsidian compatibility.

---

### cmark-gfm (GitHub's Fork)

- **License:** BSD-2-Clause (GPL-compatible)
- **Language:** C99
- **Maintenance:** Stale. Last release: 0.29.0.gfm.13 (July 2023). No commits since.
- **CommonMark compliance:** Based on 0.29 (two major versions behind)
- **GFM support:** Yes — this is GitHub's own implementation

**Extension API:** This is cmark-gfm's killer feature. It provides:

```c
cmark_syntax_extension *ext = cmark_syntax_extension_new("wikilink");
cmark_syntax_extension_set_open_block_func(ext, wikilink_open);
cmark_syntax_extension_set_match_block_func(ext, wikilink_match);
cmark_syntax_extension_set_match_inline_func(ext, wikilink_match_inline);
cmark_syntax_extension_set_get_type_string_func(ext, wikilink_type_string);
cmark_parser_attach_syntax_extension(parser, ext);
```

This allows registering custom block and inline parsers that integrate with
cmark's parsing loop, producing custom AST node types that flow through the
normal traversal and rendering infrastructure.

**Built-in extensions:** Tables, strikethrough, autolinks, task lists, tag
filter, footnotes.

**Strengths:**
- The extension API is the right abstraction for Obsidian syntax.
- Already handles GFM features we need (tables, task lists, strikethrough).
- Same AST model as cmark — familiar, well-documented.

**Weaknesses:**
- **Abandoned.** Pinned to CommonMark 0.29. No upstream maintenance.
- We would need to maintain our own fork, potentially rebasing the extension API
  onto cmark 0.31.x. This is non-trivial but feasible — the extension API is
  relatively contained.
- The extension API documentation is sparse. Understanding it requires reading
  the source of the built-in extensions.

**Verdict:** The extension API is exactly what we need, but the abandonment is a
serious liability. If we adopt this, we must be prepared to own a fork.

---

### MD4C

- **License:** MIT (GPL-compatible)
- **Language:** C, single `.c` and `.h` file, zero dependencies
- **Maintenance:** Low activity. Last commit: February 2024. Stable.
- **CommonMark compliance:** Fully compliant with 0.31
- **GFM support:** Partial, via compile flags

**Architecture:** SAX-like push parser. No AST. The single function `md_parse()`
walks the markdown and fires callbacks:

```c
typedef struct MD_PARSER {
    int (*enter_block)(MD_BLOCKTYPE type, void *detail, void *userdata);
    int (*leave_block)(MD_BLOCKTYPE type, void *detail, void *userdata);
    int (*enter_span)(MD_SPANTYPE type, void *detail, void *userdata);
    int (*leave_span)(MD_SPANTYPE type, void *detail, void *userdata);
    int (*text)(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata);
} MD_PARSER;
```

**Built-in extensions (via flags):**
- `MD_FLAG_TABLES` — GFM tables
- `MD_FLAG_TASKLISTS` — task lists
- `MD_FLAG_STRIKETHROUGH` — `~~text~~`
- `MD_FLAG_WIKILINKS` — `[[link]]` and `[[target|label]]` **(!)**
- `MD_FLAG_LATEXMATHSPANS` — `$inline$` and `$$display$$` **(!)**
- `MD_FLAG_UNDERLINE` — `_text_` means underline

**Strengths:**
- Built-in wikilink and LaTeX math support — huge for Obsidian.
- Fastest parser in benchmarks (faster than cmark).
- Tiny footprint (single file, ~5000 lines).
- Already used by Penelope (proven in our ecosystem).

**Weaknesses:**
- **No AST.** This is the critical limitation. The SAX model means you must
  build your own tree from callbacks. This is exactly what Penelope does
  (`ContentBuilder`), so it's proven feasible, but it means we own the document
  model entirely.
- **No plugin API.** Custom syntax (callouts, highlights, comments, embeds)
  requires modifying MD4C source or pre/post-processing the markdown.
- Wikilink support is basic: `[[target]]` and `[[target|label]]` only. No
  `[[target#heading]]`, no `[[target#^block-id]]`, no `![[embed]]`.

**Verdict:** Best raw parser for one-pass rendering. The SAX model is proven
workable (Penelope did it). But we'd need to build our own AST on top of it,
and handle most Obsidian extensions ourselves.

---

### md4qt (KDE Library)

- **License:** MIT (GPL-compatible)
- **Language:** C++ header-only, can use Qt6 `QString` or STL `std::string`
- **Maintenance:** Active within KDE ecosystem. Small community.
- **CommonMark compliance:** 0.31.2 with GFM extensions
- **GFM support:** Tables, footnotes, task lists, strikethrough, LaTeX math, autolinks

**Architecture:** Full AST parser. Parses into a tree of `MD::Item` nodes with
Qt6-native types. Header-only — just `#include` and use.

**Users:** KleverNotes (KDE note-taking app).

**Strengths:**
- Native Qt6 types (`QString`, `QStringView`).
- Full AST with typed node hierarchy.
- KDE ecosystem — natural fit for our project.
- Header-only — no build complexity.
- LaTeX math support built in.

**Weaknesses:**
- Very small community (23 GitHub stars).
- No Obsidian-specific extensions (wikilinks, callouts, embeds, highlights).
- No extension API for adding custom syntax.
- Less battle-tested than cmark or MD4C.

**Verdict:** The most Qt-native option. Worth considering as a primary parser
if we're willing to add Obsidian extensions ourselves (by forking or contributing
upstream). The small community is a risk.

---

### tree-sitter-markdown

- **License:** MIT (GPL-compatible)
- **Language:** C (generated parser), tree-sitter runtime in C
- **Maintenance:** Active
- **CommonMark compliance:** Based on 0.29-gfm
- **GFM support:** Yes

**Architecture:** Incremental parser. Produces a concrete syntax tree (CST)
where every character in the source is represented. When the document changes,
tree-sitter efficiently updates only the affected parts of the tree without
re-parsing the entire document.

**Strengths:**
- **Incremental parsing** — the key advantage. On every keystroke, only the
  changed region is re-parsed. O(edit size), not O(document size).
- The CST preserves all source information — whitespace, syntax characters,
  positions — enabling perfect source mapping.
- The runtime is lightweight (~100KB) and embeddable.

**Weaknesses:**
- Designed for syntax highlighting, not semantic document processing.
- CST, not AST — contains syntactic noise that must be filtered.
- No Obsidian extensions (would require modifying the grammar).
- Grammar modifications require regenerating the parser with the tree-sitter
  toolchain (Node.js dependency for build, not runtime).

**Verdict:** Not suitable as the primary parser, but potentially valuable as
a **secondary parser for the editor** — providing incremental syntax
highlighting and cursor-aware region detection while the primary parser handles
full rendering.

---

### Hoedown / Sundown

- **Status:** Dead (last commit 2015)
- **CommonMark compliance:** No (predates spec)
- **Verdict:** Do not use. Historical interest only.

---

## Parser Comparison Matrix

| | cmark | cmark-gfm | MD4C | md4qt | tree-sitter |
|---|---|---|---|---|---|
| **License** | BSD-2 | BSD-2 | MIT | MIT | MIT |
| **AST** | Full | Full | No (SAX) | Full | CST |
| **Extension API** | No | Yes | No | No | Grammar-level |
| **CommonMark** | 0.31.2 | 0.29 | 0.31 | 0.31.2 | 0.29 |
| **Tables** | No | Yes | Flag | Yes | Yes |
| **Wikilinks** | No | No | Flag (basic) | No | No |
| **LaTeX math** | No | No | Flag | Yes | No |
| **Incremental** | No | No | No | No | Yes |
| **Active** | Yes | No | Low | Low | Yes |
| **Obsidian-ready** | 0% | ~30% | ~15% | ~10% | 0% |

---

## Obsidian Extension Strategy

Regardless of which parser we choose, we need a strategy for the Obsidian
extensions that no parser handles natively. There are three approaches:

### Approach A: Pre-Processing

Transform Obsidian-specific syntax into something the parser can handle before
parsing. For example:
- Replace `==text==` with `<mark>text</mark>` before parsing
- Replace `%%comment%%` with `<!-- comment -->` (if parser handles HTML)
- Replace `> [!type]` callout markers with custom HTML wrappers

**Pros:** Works with any parser. No parser modification needed.
**Cons:** Fragile (regex on markdown is what we're trying to escape). Lossy
(can't reconstruct original syntax from transformed output). Breaks source
position tracking (offsets shift after transformation).

### Approach B: Post-Processing the AST

Parse with a standard parser, then walk the AST and transform nodes that
contain Obsidian syntax:
- Scan blockquote nodes for `[!type]` callout markers → convert to callout nodes
- Scan text nodes for `==highlight==` → split into highlight spans
- Scan text nodes for `%%comment%%` → convert to comment nodes
- Scan link nodes for wikilink patterns → convert to wikilink nodes

**Pros:** Works with any AST-producing parser. Source positions preserved (with
care). Clean separation between parsing and extension handling.
**Cons:** Some extensions (callouts, embeds) interact with block structure in
ways that are hard to reconstruct from a generic AST. Performance overhead of
a second pass.

### Approach C: Parser Extension API

Use cmark-gfm's extension API (or fork a parser to add one) to handle Obsidian
syntax during parsing, producing custom node types in the AST.

**Pros:** Correct by construction — extensions participate in the parsing loop,
handling precedence, nesting, and interaction with other syntax correctly.
Source positions are exact. Custom node types flow naturally through traversal
and rendering.
**Cons:** Requires cmark-gfm (abandoned) or forking a parser. Extension API is
complex and under-documented. Tight coupling to parser internals.

### Approach D: Two-Layer Parsing

Use a standard parser for CommonMark/GFM structure, then run a second,
Obsidian-specific parser over the AST that understands the extension semantics.
This is a refined version of Approach B where the second parser is a proper
parser (not just regex), operating on the AST nodes' text content.

**Pros:** Clean separation. The second parser can be tested independently.
Parser choice for layer 1 is decoupled from Obsidian extension handling.
**Cons:** Two parsers to maintain. Edge cases at the boundary between layers.

### Recommended Strategy

**Approach D (Two-Layer Parsing)** with MD4C or cmark as the first layer.

Rationale:
- Layer 1 handles the hard part (CommonMark block structure, precedence,
  escaping) with a proven, fuzz-tested parser.
- Layer 2 handles Obsidian-specific syntax by walking the AST (or SAX events)
  and recognizing patterns that the base parser treats as generic text, links,
  or blockquotes.
- If we use MD4C, we get wikilinks and math for free in layer 1, reducing
  layer 2's scope.
- If we use cmark, we get a proper AST for free, reducing layer 2's complexity.
- This approach was proven by Penelope: MD4C (layer 1) → ContentBuilder
  (layer 2) → Content::Document (semantic model).

---

## Extension-by-Extension Strategy

Given two-layer parsing, here's how each Obsidian extension maps:

| Extension | Layer 1 Parser Handles? | Layer 2 Strategy |
|-----------|------------------------|------------------|
| Wikilinks `[[x]]` | MD4C: basic yes. cmark: no | Parse wikilink detail (heading, block-id, display text) from link target. Note: heading links support **subheading chains** (`[[Note#h1#h2]]`) to target a specific subheading under a parent — layer 2 must split on `#` and resolve the chain. |
| Embeds `![[x]]` | No | Detect `!` prefix on wikilinks, create embed node |
| Callouts `> [!type]` | Parsed as blockquote | Inspect first line of blockquote for `[!type]` pattern, convert to callout node |
| Highlights `==x==` | No | Scan text nodes for `==...==`, split into highlight spans |
| Comments `%%x%%` | No | Scan text nodes for `%%...%%`, convert to comment nodes |
| Math `$x$` / `$$x$$` | MD4C: yes. cmark: no | If cmark: scan for math delimiters in text/code nodes |
| Tags `#tag` | No | Scan text nodes for `#word` pattern (with validation rules) |
| Block refs `^id` | No | Scan for `^block-id` at end of blocks, attach ID to parent block |
| Frontmatter | Parsed as text/thematic-break | Detect `---` at document start, extract YAML, create metadata node |
| Footnotes | cmark-gfm: yes. MD4C: no | If MD4C: pre-process with footnote extraction (Penelope's `FootnoteParser` pattern) |
| Task states `[/]` `[-]` | Parsed as task list items | Inspect checkbox content for custom state characters |
| Mermaid | Parsed as fenced code block | Detect `mermaid` info string on code blocks, flag for special rendering |

---

## Parsing Performance Requirements

For a WYSIWYG editor, parsing must be fast enough for interactive use:

| Scenario | Budget | Notes |
|----------|--------|-------|
| Full parse on file open | < 50ms for 10K-line file | One-time cost, can be async |
| Incremental re-parse on keystroke | < 5ms | Must feel instantaneous |
| Subpath extraction | < 1ms | For canvas card rendering |

MD4C is the fastest (benchmarked faster than cmark). Both are fast enough for
full-file parsing on open. Neither supports incremental parsing — both re-parse
the entire document.

For incremental parsing, tree-sitter is the only option. This suggests a
**dual-parser architecture**:
- **Primary parser** (MD4C or cmark): Full document parsing for rendering,
  export, and AST operations
- **Editor parser** (tree-sitter-markdown): Incremental parsing for syntax
  highlighting and cursor-aware region detection in the editing surface

This dual-parser approach is exactly what VS Code uses: tree-sitter for syntax
highlighting, a separate semantic parser for language features.

---

## Recommendations

### Short-term (phase 1): MD4C + custom AST builder

Use MD4C as the primary parser. Build a `MarkoffDocumentBuilder` (modeled on
Penelope's `ContentBuilder`) that receives SAX callbacks and constructs our
own AST. Handle Obsidian extensions in the builder using the two-layer approach.

**Why MD4C over cmark:**
- Built-in wikilinks and LaTeX math reduce initial scope
- Already proven in our ecosystem (Penelope)
- Single file, trivial to vendor and modify if needed
- Faster than cmark

**Why not cmark-gfm:**
- Abandoned upstream — we'd be maintaining a fork from day one
- Extension API is powerful but complex, and we need it for features cmark-gfm
  doesn't handle anyway (callouts, highlights, embeds)

### Medium-term (phase 2): Add tree-sitter for editor

Integrate tree-sitter-markdown as the editor's incremental parser. Use it for
syntax highlighting and live-preview region detection. The primary parser (MD4C)
continues to handle full rendering.

### Long-term (phase 3): Evaluate parser evolution

Once the AST builder and rendering pipeline are stable, evaluate whether to:
- Stay with MD4C + custom builder
- Migrate to cmark + post-processing (if we need AST manipulation features)
- Contribute Obsidian extensions upstream to md4qt (if KDE ecosystem grows)
- Fork cmark and rebase cmark-gfm's extension API onto latest spec
