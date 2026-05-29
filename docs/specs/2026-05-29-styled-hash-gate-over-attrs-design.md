# `markoff-styled` hash gate over block attrs

**Date:** 2026-05-29
**Arc:** WP-unification flat-view follow-ups (queue #8.5)
**Scope:** `libs/markoff-styled`
**Related:** v0.1 dogfood fixes
  (`docs/specs/2026-05-27-markoff-styled-dogfood-fixes-design.md`)
  introduced the per-block hash gate. This spec closes the
  documented "Hash gate is text-only (caveat)" gap captured in
  `libs/markoff-styled/CLAUDE.md` § v0.1 invariants.

## 1. Problem

`StyleApplier::computeBlockHash` covers
`(kind, text, spans, fontScale)`. Block format application is
gated on hash equality — a block whose hash matches the cached
value is skipped on the next `d2DocumentChanged` cascade.

`applyFormats` also reads three attrs to drive ListItem rendering
(`IndentLevel`, `MarkerStyle`, `Checked`). Those reads are not
hashed. So:

- A task-list item toggled via `toggleListItemChecked` flips the
  `Checked` attr without touching the buffer. Hash unchanged. The
  hash gate skips the block. The `QTextBlockFormat::Marker` does
  not flip. Visual stays stale until something else changes the
  hash (e.g. font scale, text edit).
- `IndentLevel` rewrites and marker-style flips have the same
  pattern.

Other view leaves (markoff-live) re-derive everything on each
cascade and don't have this gap. This spec brings markoff-styled's
hash gate in line with its actual render inputs.

## 2. Approach

Extend `computeBlockHash` to take the block's full attrs map and
mix every entry into the result via XOR. XOR is order-insensitive,
which sidesteps QHash's non-deterministic iteration order across
Qt versions. AttrValue is
`std::variant<int, QString, bool>` so the per-value mix is small
and exhaustive.

```cpp
quint64 computeBlockHash(
    Markoff::BlockKind kind,
    const QByteArray &text,
    const QList<Markoff::SourceSpan> &spans,
    const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs,
    qreal fontScale);
```

Per-attr contribution:

```cpp
for (auto it = attrs.cbegin(); it != attrs.cend(); ++it) {
    quint64 entry = qHash(it.key());
    entry *= 0x9E3779B97F4A7C15ULL;          // mix key into upper bits
    std::visit([&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>)
            entry ^= quint64(v) * 0xBF58476D1CE4E5B9ULL;
        else if constexpr (std::is_same_v<T, bool>)
            entry ^= v ? 1ULL : 2ULL;
        else if constexpr (std::is_same_v<T, QString>)
            entry ^= qHash(v);
    }, it.value());
    h ^= entry;
}
```

The key-by-mixing-then-XORing-with-value pattern means two
attrs with the same value but different keys produce different
entry contributions; an attr's contribution depends on both its
name and its value. Order across the QHash doesn't matter.

The hash is over-conservative for attrs that don't affect
rendering (e.g. `LooseRun`, `MarkerNumber`) — they trigger a
spurious format re-application when they change. This is
acceptable per the user-confirmed scope (build complex-first;
allowlist would drift as `applyFormats` grows new attr reads).

## 3. Call-site change

The single call site is `StyleApplier::applyFormats`
(`StyleApplier.cpp:423`). Add a `blockAttrs(id)` lookup just
before `computeBlockHash` and pass it through. `applyFormats`
already calls `blockAttrs(id)` later in the ListItem branch; the
two lookups can share a single read or remain independent (the
later lookup is inside a kind-specific branch that may be
skipped). Single shared lookup is simpler; do it.

## 4. Tests

Add to `tst_styled_dogfood_invariants`:

1. `attr_toggle_restyle_re_renders_task_marker` — load
   `"- [ ] task\n"`. Walk to the task ListItem block. Flip the
   `Checked` attr via `Cmd::changeKind`-style attr write (use
   the same primitive the live view uses). After the cascade,
   assert the QTextBlock at that position has its
   `markerType()` equal to `QTextBlockFormat::MarkerType::Checked`.

The falsifiable proof: temporarily revert the attrs hashing
change. The test must fail (marker stays `Unchecked` because the
hash gate skipped the block).

`tst_styled_dogfood_invariants::hash_gate_skips_unchanged_blocks`
already exists and continues to hold: a no-op `d2DocumentChanged`
emit with no actual block mutation produces zero re-applications.
That test must not regress.

## 5. Scope and exclusions

**In scope:**
- `computeBlockHash` signature extension to accept attrs.
- XOR-based attr-entry mixing.
- One falsifiable test slot in `tst_styled_dogfood_invariants`.
- CLAUDE.md update: remove the "Hash gate is text-only (caveat)"
  line, replace with the new contract.
- Queue #8.5 closeout.

**Out of scope:**
- Setext heading renders as H1 (the `applyHeading` level
  derivation reads `text[i] == '#'` — for setext, no `#`, level
  defaults to 1). This is a separate latent bug; folded into a
  future micro-spec, not this one.
- Render-input allowlist (rejected per §2).
- Hash gate added to the markoff-live render path (markoff-live
  has different gating mechanics; out of scope).

## 6. Files touched

- `libs/markoff-styled/src/StyleApplier.cpp` — extend
  `computeBlockHash` definition; single call-site change in
  `applyFormats`.
- `libs/markoff-styled/src/StyleApplier.h` — if the declaration
  is in the header (it isn't currently; the function is
  file-local `namespace { ... }`), update there.
- `libs/markoff-styled/tests/tst_styled_dogfood_invariants.cpp`
  — one new slot.
- `libs/markoff-styled/CLAUDE.md` — invariants section.
- `docs/queue.md` — close #8.5.

## 7. Definition of done

- New test slot passes; falsifiability proof shows it fails when
  the attrs mixing is reverted.
- `tst_styled_dogfood_invariants::hash_gate_skips_unchanged_blocks`
  continues to pass.
- Full fast suite at the 2026-05-29 baseline (249/254). No new
  failures.
- CLAUDE.md "Hash gate is text-only (caveat)" line replaced with
  the new contract.
- Queue #8.5 closed.

## 8. Risks and notes

- **Over-restyle on irrelevant attrs.** Documented in §2.
  Acceptable.
- **QHash iteration order.** Mitigated by XOR (commutative).
- **AttrValue variant changes.** If a fourth variant alternative
  is added later, the `std::visit` `if constexpr` ladder above
  must grow a branch. The compiler does not enforce exhaustiveness
  on `std::visit` without an `else` branch — add a `static_assert`
  fallback to catch this at compile time.

```cpp
else
    static_assert(sizeof(T) == 0, "Unhandled AttrValue alternative");
```
