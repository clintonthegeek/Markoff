# YAML API contract for Cluster A (post-RapidYAML port)

> **Addressed to:** whoever is porting `libs/markoff-parser` from yaml-cpp to RapidYAML (ryml).
>
> **From:** Cluster A (link/frontmatter correctness) — consumer of this API.
>
> **Purpose:** enumerate the precise surface Cluster A needs `Markoff::Document` / `markoff-parser` to expose once the YAML backend swap is complete, so that `libs/core/FrontMatter` and `libs/core/FrontMatterWriter` can delegate to it instead of re-parsing frontmatter.
>
> **Authoritative spec reference:** `docs/obsidian-audit/domains/parsing.md §2` (Obsidian's `eemeli/yaml` option set) and `docs/obsidian-audit/domains/parsing.md §8` (frontmatter delimiter regexes — EOF-tolerant close).

---

## 1. Parse side — what Cluster A needs to read

### 1.1 `Document::frontmatterRaw() -> QString`

Return the **raw YAML text between the delimiters**, `---\n` … `---\n|EOF`, **without** the delimiter lines themselves, **without** a trailing newline, and **byte-exact** with what's on disk (whitespace, key order, comments, quoting style all preserved).

Cluster A uses this for:
- Round-trip equality checks in tests.
- Computing the `ContentStart` offset (first character after the closing `---` line) so mutations don't corrupt body text.

**Not the same as** the current `frontmatter()` method if that one normalises whitespace. If the current method already returns byte-exact raw, rename is optional.

### 1.2 `Document::frontmatterSpan() -> std::optional<SourceSpan>`

Return `{startByte, endByte}` covering the **complete frontmatter block** including both delimiter lines and the final newline. `std::nullopt` if no frontmatter present.

Cluster A uses this for:
- `FrontMatterWriter::process()` — splice a new frontmatter block into the file without re-tokenising the body.
- EOF-close detection: if `endByte == fileSize`, we know the frontmatter closed at EOF with no trailing newline (Obsidian's `Qx` regex case).

### 1.3 `Document::parsedFrontmatter() -> YamlValue` (REPLACES current `QList<FrontmatterProperty>`)

Drop the flattened key/value list. Return a **ryml::Tree** (owned) or a thin Corbomite wrapper around it — call it `Markoff::YamlValue`. Requirements:

- **Preserves key order** exactly as it appears on disk.
- **Preserves node styles**: flow vs. block, single-quoted vs. double-quoted vs. unquoted, folded vs. literal scalars. Round-tripping an unchanged document through `parse → stringify` must be a byte-exact no-op (modulo newline-at-EOF normalisation — see §2.4).
- **Preserves unknown keys and nested structure.** Do not flatten. Cluster A needs nested maps/lists intact for Bases and properties-panel work.
- **Exposes comment round-trip:** if ryml can attach comments to nodes, keep them. If not, document that comments are dropped (matches Obsidian baseline — see `VAULT-FORMAT.md §1`). Dropping comments is acceptable; silently discarding unknown keys is not.
- **YAML 1.2 core schema, strict.** `yes` / `no` / `on` / `off` / `y` / `n` must parse as **strings**, never booleans. (yaml-cpp required a custom schema for this; ryml is YAML-1.2-strict by default.)

### 1.4 `YamlValue` accessor surface

Minimum for Cluster A:

```cpp
class YamlValue {
public:
    enum class Kind { Null, Bool, Int, Double, String, Seq, Map };
    Kind kind() const;

    // Scalars
    bool asBool() const;
    int64_t asInt() const;
    double asDouble() const;
    QString asString() const;  // for scalars of any type, using ryml's stored representation

    // Containers
    bool isSeq() const;
    bool isMap() const;
    int size() const;

    // Sequence access
    YamlValue at(int index) const;

    // Map access — ORDER-PRESERVING
    bool contains(const QString &key) const;
    YamlValue get(const QString &key) const;           // Null if absent
    QStringList keys() const;                          // in document order
    void forEach(std::function<void(const QString&, const YamlValue&)>) const;

    // Optional: convenience for common cases
    QStringList asStringList() const;  // for `tags:` and `aliases:` when they're a seq of scalars
};
```

Non-negotiable: `keys()` returns keys in **document order**, not alphabetical, not insertion-hash order.

### 1.5 `Document::frontmatterHasEofClose() -> bool`

Return `true` iff the closing `---` is at EOF with no trailing newline (matches Obsidian's `Qx` regex `/---(\r?\n|$)/g`). Currently **Corbomite rejects this shape** — P0.2 in the gap analysis. Cluster A's tests will assert this flag round-trips correctly.

---

## 2. Emit side — NEW surface, doesn't exist today

Current API is read-only. Cluster A's `FrontMatterWriter::process(path, mutator)` needs write support.

### 2.1 `YamlValue::mutate(...)` — editable builder

```cpp
class YamlValue {
    // ...accessors from §1.4...

    // Mutation — preserves node styles where possible, emits default style for new nodes
    void setString(const QString &key, const QString &value);
    void setInt(const QString &key, int64_t value);
    void setDouble(const QString &key, double value);
    void setBool(const QString &key, bool value);
    void setNull(const QString &key);
    void setSeq(const QString &key, const QStringList &values);   // convenience
    YamlValue &setMap(const QString &key);                        // returns child for further mutation
    YamlValue &setSeq(const QString &key);

    // Removal
    void remove(const QString &key);

    // Sequence append
    YamlValue &appendMap();
    void appendString(const QString &value);
};
```

Insertion order matters: new keys are appended to the end of the map, never reordered to the middle. If a key already exists, update in place (preserves its position and, where feasible, its node style).

### 2.2 `Markoff::stringifyFrontmatter(const YamlValue &) -> QString`

Emit the YAML body (no `---` delimiters) using **Obsidian's option set**:

| Option | Value | Rationale |
|---|---|---|
| YAML version | 1.2, core schema | matches `eemeli/yaml` v2 defaults per audit §2 |
| `nullStr` | `""` (empty) | Obsidian emits `key:` not `key: null` for nulls |
| `lineWidth` | `0` | no soft-wrapping; long strings stay on one line |
| `aliasDuplicateObjects` | `false` | never emit `&anchor` / `*alias` |
| `indent` | 2 | Obsidian default |
| `blockQuoteStyle` | preserve-on-round-trip, default `literal` for new multi-line strings | matches Obsidian |
| Key order | **document order (preserve) for existing keys, append for new keys** | strict requirement |
| Trailing newline | exactly one `\n` after the final scalar | matches Obsidian |

The emitted output, prefixed with `---\n` and suffixed with `---\n`, must be what `FrontMatterWriter` splices back into the file.

### 2.3 `Document::withFrontmatter(const YamlValue &) -> QString`

Convenience: returns the full file content with the old frontmatter replaced by `---\n<stringifyFrontmatter(v)>---\n<body>`. Equivalent to slicing `frontmatterSpan()` out and splicing a new block in. Cluster A's `FrontMatterWriter` will use this + `QSaveFile`.

Behaviour when the document had no frontmatter and `v` is non-empty: prepend a new frontmatter block. When `v` is empty: return body unchanged (drop delimiters if present).

### 2.4 Round-trip invariant

`Document::fromMarkdown(s)->withFrontmatter(Document::fromMarkdown(s)->parsedFrontmatter())` MUST equal `s`, modulo:
- trailing-newline-at-EOF normalisation (document MAY add one if missing; MUST NOT strip one that exists if its presence is load-bearing — the `frontmatterHasEofClose()` case),
- comments inside frontmatter (MAY be dropped; document the behaviour).

This invariant is the single most important test — Cluster A will add it to `tests/markoff-parser/` and run it against a corpus of real-vault `.md` files.

---

## 3. Non-functional requirements

### 3.1 Performance
ryml benchmarks at 10–70× faster than yaml-cpp on parse. Cluster A's vault-scanning loop reads frontmatter for every `.md` file on vault open; this perf win is the reason for the port, so **don't regress it** by wrapping ryml in a string-copy-heavy facade. `YamlValue` should hold a `std::shared_ptr<ryml::Tree>` + a node id, not copy scalars eagerly.

### 3.2 Error handling
- Malformed YAML frontmatter → `parsedFrontmatter()` returns `YamlValue{}` (empty map) **and** `Document::frontmatterParseError()` returns a diagnostic string. Do not throw across the C++ boundary to Qt slots.
- Missing frontmatter delimiters → no error; empty map, `frontmatterSpan() == nullopt`.

### 3.3 Thread-safety
`Document` is already immutable after construction (current contract). Maintain this — `YamlValue` accessors are `const`-correct and thread-safe for concurrent reads on the same `Document`. Mutation (§2.1) operates on a **copy** returned by a method like `YamlValue::clone()`, never on the `Document`'s original tree.

### 3.4 Test fixtures
Add these to `tests/markoff-parser/fixtures/`:
- `eof-close.md` — frontmatter closes at EOF, no trailing newline.
- `yaml-1-1-booleans.md` — keys like `active: yes` — must remain strings.
- `nested-map-and-list.md` — for order-preservation round-trip.
- `bom-and-crlf.md` — edge case on Windows-authored vaults.
- `comment-in-frontmatter.md` — documents drop-behaviour.
- `unicode-keys-and-values.md` — emoji keys, RTL values.

---

## 4. Out of scope for the port

These are **not** expected from markoff-parser; Cluster A implements them in `libs/core`:
- Subpath parsing (`parseLinktext` — `libs/core/LinkUtils`).
- Heading stripping (`stripHeading` / `stripHeadingForLink` — `libs/core/LinkUtils`).
- Wikilink resolution (`libs/storage/LinkResolver`).
- Atomic file writes (`libs/core/FrontMatterWriter` owns `QSaveFile` + fsync/rename).

markoff-parser's scope stays: lex/parse markdown, expose frontmatter as structured data, and (newly) emit frontmatter back to text. It does not touch the filesystem.

---

## 5. Migration checklist for markoff-parser

When the port is done, Cluster A expects this checklist green:

- [ ] `find_package(yaml-cpp)` removed from `libs/markoff-parser/CMakeLists.txt`.
- [ ] ryml vendored or `FetchContent`-fetched; pinned to a specific tag.
- [ ] `#include <yaml-cpp/yaml.h>` gone from `src/Document.cpp`; replaced with ryml include.
- [ ] Old `QList<FrontmatterProperty> parsedFrontmatter()` deprecated or removed; new `YamlValue parsedFrontmatter()` in place.
- [ ] `frontmatterRaw()`, `frontmatterSpan()`, `frontmatterHasEofClose()` exposed per §1.
- [ ] `stringifyFrontmatter()` and `withFrontmatter()` exposed per §2.
- [ ] Round-trip test (§2.4) passes on the full `testvaults/` corpus.
- [ ] Existing markoff-parser tests still pass (guard against perf/ABI regressions).
- [ ] `libs/core/` gains `FrontMatter` / `FrontMatterWriter` that delegate here — no second YAML library anywhere in the tree.

Cluster A does not block on this port landing — we can build `libs/core/FrontMatter` against the existing yaml-cpp API first and swap it when the port lands — **but** we would prefer the port lands first so we only write the `libs/core` wrapper once, against the final API.
