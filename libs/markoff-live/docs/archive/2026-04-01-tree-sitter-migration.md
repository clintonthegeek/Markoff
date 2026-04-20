# Markoff: Tree-Sitter Migration

## Decision

Replace MD4C with tree-sitter-markdown as the single parser for Markoff.
Tree-sitter produces a Concrete Syntax Tree where every character —
including syntax delimiters — has an explicit node with byte offsets.
This eliminates the need for gap-based delimiter inference, Layer 2
post-processing, and the span map builder.

## Why

MD4C is a SAX parser that consumes syntax delimiters without reporting
their positions. This is fine for HTML rendering but fatal for a live
preview editor that needs to show/hide individual `**`, `*`, `` ` ``,
`##`, `>` characters based on cursor position.

Tree-sitter gives us:
- Explicit delimiter nodes (`emphasis_delimiter`, `code_span_delimiter`,
  `atx_h2_marker`, `block_quote_marker`) with byte offsets
- Incremental parsing (only re-parse changed regions on keystroke)
- Built-in extensions: wiki links, LaTeX math, GFM tables, YAML frontmatter
- Forkable grammar for Obsidian extensions (==highlights==, %%comments%%)

## What Gets Deleted

- MD4C dependency (pkg-config md4c)
- `DocumentBuilder.cpp` / `DocumentBuilder_p.h` (MD4C SAX callback builder)
- `SourceSpan.cpp` / `SourceSpan.h` (gap-based span map builder)
- Layer 2 post-processing (splitInlinePattern, postProcess)
- All footnote preprocessing in Document.cpp

## What Gets Added

- Vendored tree-sitter-markdown grammar (4 C files, already in tree)
- `TreeSitterParser.h/cpp` — wraps tree-sitter API, produces CST
- Rewritten `MarkdownHighlighter` — walks CST nodes, applies formatting
- Rewritten `Renderer` — walks CST for HTML generation

## What Stays

- `Document.h` public API (fromMarkdown, sourceText, extractSubpath)
- `Editor.h` public API
- `ReadingView.h` public API
- `RenderSettings.h`
- `AtomicBlock`, `CodeAtomicBlock`, `CalloutAtomicBlock`
- Test app, Corbomite adapter

## Migration Steps

1. Get tree-sitter grammars compiling in CMake
2. Write TreeSitterParser — parse text, expose CST
3. Rewrite highlighter to walk CST nodes
4. Rewrite renderer to walk CST for HTML
5. Update Document to use TreeSitterParser
6. Delete MD4C code
7. Fork grammar for ==highlights== and %%comments%%
