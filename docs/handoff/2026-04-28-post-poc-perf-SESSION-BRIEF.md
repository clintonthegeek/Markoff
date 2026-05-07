> **Status: superseded.** Initial perf investigation landed (`6cdf0f2`); the broader perf arc has moved to `docs/specs/2026-04-30-optimization-baseline-and-plan.md` + `docs/handoff/2026-04-30-post-blockwalker-SESSION-BRIEF.md`. Do not execute.

# Post-POC perf investigation — fresh-context SESSION BRIEF

**Read this first.** This document is a self-contained briefing for a fresh-context Claude session picking up the markoff-view-qml POC's typing-perf problem.

## Status

The POC works: the test app (`build-dev/bin/markoff-view-qml-app <markdown-file>`) opens a window with rendered + syntax-highlighted markdown, edits round-trip cleanly via the cycle-guarded edit bridge, and 6/6 view-qml + 25/25 foundation tests pass. The foundation+POC is tagged `exploration/foundation-poc-2026-04-28`.

**The blocker:** typing rapidly in `docs/specs/2026-04-28-foundation-design.md` (~16KB / ~700 lines) maxes CPU and freezes the UI for ~30 seconds while the keyboard buffer drains. The editor is unusable for real-world docs at this stage. **No more architectural perf "fixes" until profiling lands** — one allocation-removal commit (`5b116be`) didn't help, so the dominant cost is somewhere we haven't measured.

## TL;DR

1. Profile the test app under `perf record` while typing into a long doc.
2. Compare cost breakdown vs. a tiny doc (100 lines) — same hot functions or different?
3. Write a focused plan for the targeted fixes that the profile motivates. Don't merge "obvious" hot-path fixes without a profile that confirms each is the dominant cost.

## Suspected cost centres (top three; profile will confirm or refute)

1. **3× full-document copies in `SourceTextDocumentBinding::onQtContentsChange`** per keystroke (`doc->toMarkdownUtf8()` + `QString::fromUtf8(preBytes)` + `m_qtDoc->toPlainText()`). For 16KB doc that's 80KB allocation per keystroke. Pure-insertion fast-path (when `charsRemoved == 0` — typing) can skip the pre-state fetch entirely.
2. **Foundation's `ParsePool` parses the whole document on every change** — no incremental tree-sitter parsing. Documented as deferred in `docs/specs/2026-04-28-foundation-design.md`. Foundation territory; touches `markoff-foundation/src/ParsePool.cpp` and `MarkoffDocument.cpp`.
3. **KSyntaxHighlighter rehighlight scope** — KF6's QML `SyntaxHighlighter` may or may not be incremental. Unverified.

## Required reading (in order)

1. **POC implementation plan**: `~/.claude/plans/nah-a-is-fine-fuzzy-backus.md` — the 24-task plan that landed.
2. **POC library guide**: `libs/markoff-view-qml/CLAUDE.md` — invariants + the new "Performance" section.
3. **Source binding hot path**: `libs/markoff-view-qml/src/SourceTextDocumentBinding.cpp` — read `onQtContentsChange` (~120 LOC) carefully; this is where the allocation pressure is.
4. **Foundation parse pipeline**: `libs/markoff-core/src/MarkoffDocument.cpp` (the `applyLocalEdit` path) and `libs/markoff-core/src/ParsePool.cpp` if you go after parser cost.
5. **Audit prior pain**: `docs/2026-04-28-codebase-audit.md` §2.2 (legacy `markoff-live` had the same "2-4 redundant parses per keystroke" problem; we shouldn't recreate it but the foundation's coalescing may already prevent some of it).

## What to do, in order

### Step 1: Profile

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
# Build with optimization but keep symbols. (Default is RelWithDebInfo via CMakePresets if any; if not, force Release+debug.)
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff-view-qml-app -j

# In one terminal, launch the app:
./build-dev/bin/markoff-view-qml-app docs/specs/2026-04-28-foundation-design.md

# In another terminal, attach perf:
perf record -F 99 -p $(pgrep -f markoff-view-qml-app) -g -- sleep 30
# During the 30s, in the app, type rapidly at the END of the document.

# After:
perf report --no-children --stdio | head -50
```

Capture the top 10 hottest functions. The user will likely review this with you.

Compare with a small file (`echo '# small' > /tmp/small.md`) — does the same function stay hot, or does the cost drop dramatically? That tells you whether the cost is constant-per-keystroke or O(doc_size)-per-keystroke.

### Step 2: Plan

Based on the profile, write a `docs/plans/2026-04-28-typing-perf.md` (or similar) with the targeted fixes ordered by expected impact. Use the brainstorming or writing-plans skill if appropriate.

DO NOT skip to implementation without a plan. The previous "obvious fix" attempt failed because the obvious cost wasn't the dominant one.

### Step 3: Execute

Subagent-driven-development for the planned fixes. Each fix is a `perf:` commit with a before/after measurement (even rough — `time` or perf-counts) noted in the commit message body or the plan's progress log.

## Constraints

- **Don't merge speculative fixes.** Every perf commit needs a measurement that motivates it.
- **Don't break the 6 view-qml tests** — especially T11's UTF-8/16 roundtrip and T13's bidirectional cycle-guard tests.
- **Keep cycle-guard discipline.** Any new cache or incremental-state in `SourceTextDocumentBinding` needs the same `m_applying*` bool pattern that existing seams use.
- **Foundation changes need their own commit prefixed `feat(foundation):`/`perf(foundation):`** — don't mix view-qml + foundation work in one commit.
- **Don't touch master.** Branch is purely additive.

## What's out of scope

- Phase 2 live preview (Obsidian-style inline rendering). That's a separate plan.
- Tag rotation. The POC tag (`exploration/foundation-poc-2026-04-28`) stays; perf work doesn't tag until visibly complete.
- Host integration (CorbomiteApp consuming the foundation). Separate phase.

## Notes

- The user is pragmatic, not pedagogical. Heads-down build flow.
- Recent foundation work used subagent-driven-development with one-task-per-commit; same flow works here.
- Test app smoke test (`tst_view_qml_app_smoke`) catches regressions in the launch-path; KEEP IT GREEN through perf work.
