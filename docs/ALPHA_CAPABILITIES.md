# Markoff — alpha capability notes

Small manifest of deliberate non-capabilities so nobody has to wonder about
them again. Each row names the trigger that opens the work; until that
trigger fires, the current state is intentional, not an oversight. The
filing convention follows collabtext's `ALPHA_CAPABILITIES.md`. See
[`plans/2026-10-19-corbomite-open-asks-triage.md`](plans/2026-10-19-corbomite-open-asks-triage.md)
for the task-level record.

## M26 — export macros are no-ops

The `Q_DECL_EXPORT`/`Q_DECL_IMPORT` switch macros resolve to empty on this
toolchain (Linux/GCC baked with default visibility), so every shared library
exports everything and the macros do nothing. That is why a Linux shared
package builds and works with the macros as no-ops.

Trigger: an MSVC build, or any consumer that turns visibility on
(`CXX_VISIBILITY_PRESET hidden`). Then the work is mechanical but wide
(annotate every public symbol in core/parser/canvas/source/styled), and it
lands as one planned task rather than speculatively.

## M27 item 4 — the engine link is PUBLIC and a public header names the engine

Current state, true at the pin `e8a8e7d1`:

- `markoff_core` links `Markoff::CollabTextVNext` PUBLIC
  (`libs/markoff-core/CMakeLists.txt:132`), and
  `include/markoff/core/MarkoffDocument.h` includes
  `<collabtext/ActorId.h>` directly: the public contract returns
  `CollabText::Result<T>` by value in 15 signatures and carries
  `CollabText::Error` by value in the remote-action route types.
- By construction a consumer that includes `markoff/core` headers also
  inherits the collabtext include tree. Separately,
  `markoff-collabtext-vnext`'s public headers (`markoff/vnext/*`) name the
  engine deliberately — that leaf is the engine/transport boundary and will
  keep doing so at every pin.

Why deferred rather than fixed now: `CollabText::Result` is a value type, so
the asks' "forward-declare" shape is impossible; the real shape is
`Markoff::VNext::Result`/`Error` value types owned by markoff (a narrow
mirror of the engine's, with the engine's `ErrorCode` table mirrored by
number), a conversion seam inside `markoff_core`, and the engine link flipped
PRIVATE — after which including `markoff/core` headers no longer drags the
collabtext headers. Consumers pay nothing today: the only consumer
(Corbomite) is a submodule consumer and has no call sites against these
signatures yet.

Trigger: the first non-submodule consumer, or an MSVC/hidden-visibility
package build (Corbomite's Arch package qualifies when it ships). Then the
task above runs as one planned change with the equivalence oracle intact.
Until then the engine link stays PUBLIC so every consumer keeps compiling.
