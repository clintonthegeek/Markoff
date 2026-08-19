# Handoff → Markoff devs: retire markoff-live, close the E-arc, and reclaim your own roadmap

**From:** Corbomite (downstream consumer of `Markoff::Canvas::EditorWidget`)
**Date:** 2026-08-19
**Corbomite pin:** `a3d8055e` (`v0.7.0-contract-v2-272-ga3d8055e`)
**Relevant Markoff docs:** `CLAUDE.md` §"Current workfront", `docs/plans/2026-08-13-canvas-production-plan.md` (G1–G3 gates), `docs/e-arc/e-arc-status.md`
**Reply needed:** No — this is a status report + a set of recommendations, not a blocking ask. Act on whatever parts make sense to you.

## TL;DR

1. **G2 (Corbomite adoption) is done.** Corbomite's Cluster K Phase 5 (2026-08-18) made `Markoff::Canvas::EditorWidget` the sole LivePreview engine and fully unlinked `markoff_live`/Qt QuickWidgets from its build (`MARKOFF_BUILD_LIVE=OFF`, forced). Your own plan still shows G2 unchecked — please check it off with a pointer to this doc or to Corbomite's `decisions-archive.md` 2026-08-18 entry.
2. **Recommend closing G3 for markoff-live specifically, now.** It has zero downstream consumers left. This doc gives you the evidence to make that call without waiting on us further.
3. **The E-arc (`docs/e-arc/`) should be formally archived, not left dormant.** It's been "dormant" since 2026-06-09 but never closed out — meanwhile its entire scope (E3 Obsidian affordances, E5 math/mermaid) shipped under the canvas arc's Phase 5 instead. Two roadmaps for the same ground is exactly the kind of drift we just spent a session cleaning up on our own side (see below) — worth closing before it causes the same confusion for you.
4. **Correction to our own prior handoff:** `docs/handoff/2026-08-17-to-markoff-canvas-g2-adoption-report.md` told you callouts were "frozen pending Markoff's own E3 item." That was wrong even at the time we wrote it — your P5.5 (`20949498`, 2026-08-14) had already shipped callout support three days earlier. We just hadn't re-checked. See §"Corbomite's own drift" below — you're not the only one who let docs fall behind code recently.
5. **The bigger ask, if you want it:** we think Corbomite has been setting Markoff's agenda for a while now — every recent arc (contract-v2, canvas production) was paced by our dogfood findings and adoption gates. Now that G2 is closed and the canvas leaf is stable in production, this is a natural point for Markoff to run its own cycle again — pick your own next milestone (candidates below) instead of waiting on the next Corbomite finding.

---

## 1. G2 is done — please check it off

Your plan (`docs/plans/2026-08-13-canvas-production-plan.md`) still lists:

```
| **G2 — user gate: Corbomite adoption** (work lands in Corbomite repo) | ☐ | — | — |
```

It happened. Corbomite commit `7a6f18a4` ("Cluster K Phase 5 (Canvas-only) + 0.1.0 packaging", 2026-08-18):
- `NoteEditorWidget` always constructs `Markoff::Canvas::EditorWidget`; the `Markoff::Live::EditorWidget` accessor is deleted entirely.
- `markoff_live`/`markoff_liveplugin`/`markoff_liveplugin_init` dropped from Corbomite's `src/CMakeLists.txt`; `Quick`/`QuickWidgets`/`Qml` removed from Corbomite's top-level `find_package(Qt6)`.
- The `CanvasLivePreview` settings toggle (the reversible A/B switch from Cluster K's earlier phases) is gone — there's no fallback path left in Corbomite's code.
- Your `MARKOFF_BUILD_LIVE` option is forced `OFF` from Corbomite's CMake (`CMakeLists.txt:97`, `set(MARKOFF_BUILD_LIVE OFF CACHE BOOL "" FORCE)`).

We since shipped v0.1.0 on top of this (2026-08-18, same day) and it's been running in dogfood since without any regression traced to the leaf swap. G2's question — "is canvas ready to adopt as Corbomite's LivePreview engine" — has a settled answer: yes.

## 2. Recommend: retire markoff-live now (G3, scoped to live only)

Your plan's G3 ("retirement decision — successor spec") is still open, and reads as if it might cover both `markoff-live` and `markoff-styled` together. We'd split it:

**markoff-live: retire now.** Zero downstream consumers. `MARKOFF_BUILD_LIVE` defaults `ON` in your own standalone build (`CMakeLists.txt:78`) and your `libs/markoff-live/app` demo still builds against it when `MARKOFF_BUILD_APPS` is on — so it's not dead code from your own tree's perspective, but it exists *purely* as your own internal demo/test surface now. That's a legitimate reason to keep it a little longer if the demo app is useful to you for something canvas doesn't cover (QML-specific regression coverage, maybe), but it's no longer "the live-render leaf a real consumer depends on" — the whole reason it was under active development. Worth an explicit decision either way rather than leaving it in permanent standstill limbo.

If you do retire it: your own convention is "move, never delete" (`CLAUDE.md` — retired paper goes to `docs/archive/`). We'd assume the same applies to code — an `archive/markoff-live-final` tag (mirroring your existing `archive/markoff-fold-v2`, `archive/markoff-reading-split`, `archive/markoff-source-split` tags) plus deletion from `master`, or a slower path (mark it `MARKOFF_BUILD_LIVE` default `OFF` first, delete later) — your call, not ours; we have zero remaining stake in this decision since we don't link it either way.

**markoff-styled: do NOT retire yet, and don't bundle it into this decision.** Corbomite's Reading mode is `Markoff::Styled::Editor` today and that's a live, actively-relied-on leaf for us — retiring it is a real question (should Reading eventually become a read-only Canvas variant instead?) but it's a separate decision with its own migration cost, not something to fold into the "zero-consumer, obvious call" cleanup above. Keep it bug-fix-only as-is; we'll open a fresh handoff if/when we want to discuss migrating Reading mode.

## 3. Recommend: formally close the E-arc

`docs/e-arc/e-arc-status.md` has carried this banner since 2026-06-09:

> **DISPOSITION — 2026-06-09: board closed; E-arc dormant.** ... E3b+/E5/E6 never started. The workfront moved to the styled leaf / WP unification / flat-view binding 2026-05-26; E-arc resumes only if live-leaf feature pressure returns.

It never resumed — instead, the scope it named (E3 "Wikilinks, embeds, tags, callouts", E5 "Math / Mermaid Live-mode parity") shipped under a completely different plan: the canvas production arc's Phase 5 (P5.3 math, P5.4 images/mermaid seam, P5.5 callouts/frontmatter/footnote-defs — all closed 2026-08-15, commits including `20949498`, verified by us as ancestors of your current tip). "Dormant, might resume" is no longer an accurate status for work whose entire scope has already been delivered by a successor plan under different phase names. We'd suggest:

- Add a closing disposition to `e-arc-status.md` (or move the whole `docs/e-arc/` directory to `docs/archive/e-arc/` per your existing archive convention) noting that E3/E5's scope shipped as canvas P5.3–P5.5, and E1/E2/E4 already shipped against markoff-live per the existing 2026-06-09 note.
- This matters to us specifically because **our own punch-list still cites "Markoff E3" and "Markoff E5" as live blockers** on several items (callouts, footnotes, embeds, mermaid dark theme) — citing a dormant, unclosed roadmap as an active blocker is exactly the kind of stale cross-repo reference that caused the callout mixup in §4 below. A clean close on your end makes it obvious on ours that those citations are wrong and the real status is "already shipped, needs Corbomite-side adoption" (see §5).

## 4. Corbomite's own drift — full disclosure, so you know this isn't a one-sided ask

We found this doing a routine audit of our own `git log`, not because we were auditing you: our docs hadn't been updated in five commits' worth of work (a GitHub migration, a v0.1.0 packaging push, and some compat fixes) — pure neglect, same failure mode as your dormant E-arc board. In fixing that we also re-checked every "frozen, blocked on Markoff" item in our punch-list against your actual current tip, and found:

- **Callouts** (`> [!note]`): our punch-list says `FROZEN — Markoff E3`, and our own 2026-08-17 handoff to you said "frozen pending Markoff's own E3 item." Both wrong — `BlockPresentation::presentationFor` in `libs/markoff-canvas/src/BlockPresentation.cpp` content-sniffs callout shape directly from block text on every render (no `BlockKind` promotion gate, unlike the image/embed bug we fixed the same week), so callout rendering should already work at our current pin with **zero further work on your side**. We just never re-verified after your P5.5 landed. That's on us, not you — flagging so you don't waste a cycle "fixing" something that isn't broken upstream.
- **Math, footnote-defs, frontmatter band**: same story — P5.3/P5.5 already cover these; we just hadn't adopted them.
- **Mermaid + images/embeds**: these genuinely need work, but it's **ours to do**, not yours — your seams already exist (`Markoff::Canvas::View::setMermaidRenderer`, `setEmbedRegistry`, `setImageResourceLookup`) and are correctly designed; we simply never wired a consumer behind them. We're doing that now (tracked in our own docs, not yours).

None of this needs anything further from you. We're telling you so that if you go looking at your own punch list and see Corbomite-filed items referencing E3/E5/callouts, you'll know the real status is "shipped, awaiting adoption," not "still needs Markoff work."

## 5. On regrouping

We're aware Corbomite has effectively been setting Markoff's pace for a while — contract-v2 was scoped around our adoption needs, the canvas production arc's gates (G1/G2/G3) are all framed around *our* consumption of it, and G1 (accessibility) got deferred specifically because we weren't asking for it yet. That's a reasonable way to have run things while canvas was unproven, but G2 closing is a natural seam: canvas is production-stable in a real consumer now, nothing about its remaining roadmap needs to be dictated by our dogfood queue.

We'd genuinely like Markoff to run its own track for a while — decide what's next by your own judgment, not by waiting for our next finding. A few candidates already sitting in your own plan that nothing external is blocking:
- **G1 (accessibility)** — explicitly deferred by user choice on 2026-08-14, never revisited.
- **Phase 6 (collaboration surface)** — closed per your status line, but real multi-user collab is still a "(future)" feature per your own `README.md`-equivalent framing; worth asking whether Phase 6's closure was infrastructure-only or whether there's a next real milestone (actual concurrent-editing dogfood, e.g.) behind it.
- **The F1 CodeMirror-parity floating task** — still described as "do in any session once `~/src/codemirror` exists"; worth just finishing it off the queue if the reference tree is available now, independent of anything Corbomite needs.
- **markoff-source and markoff-styled** — both "untouched"/"bug-fix-only" by policy, which is fine, but it might be worth a periodic health check independent of Corbomite filing a bug (e.g., are there latent issues nobody's hit yet because nobody's stress-tested them outside a Corbomite host?).

No response needed. If any of this prompts a plan change on your end, a one-line note back whenever convenient is plenty — we're not blocked on anything here.
