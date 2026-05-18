# E3a — dogfood request (2026-05-18)

**Scope:** wikilink + standard-link navigation in Live.

## Checklist

Run `./build-dev/bin/markoff-live-app some-doc.md` on a document that contains
at least one `[[Wikilink]]`, one `[[Page|Alias]]`, one `[[Page#Section]]`, and
one `[text](https://example.com)`.

- [ ] Plain click on a wikilink places the caret (no navigation).
- [ ] Ctrl+click on `[[Page]]` logs activation to stdout (kind=WikiLink, page="Page").
- [ ] Ctrl+click on `[[Page|Alias]]` logs activation with alias populated.
- [ ] Ctrl+click on `[[Page#Section]]` logs activation with section populated.
- [ ] Ctrl+click on `[text](https://...)` opens the URL in the system browser.
- [ ] Ctrl-hover over a link flips the cursor to a pointing hand; status bar shows "Ctrl+click to open: ...".
- [ ] Releasing Ctrl (without moving) flips the cursor back to I-beam.
- [ ] Hover off the link clears the status bar message.
- [ ] If a sibling `<page>.md` exists, Ctrl+click on `[[<page>]]` opens it in-place.
- [ ] Caret-inside-link autohide-reveal: caret in the link span still shows the source delimiters; Ctrl+click still navigates; plain click moves caret normally.

## Out of scope for E3a (do not regress; do not test)

- Tags (`#tag`) navigation — E3b.
- Embeds (`![[image.png]]`, `![[Page]]`) — E3c.
- Callouts — E3d.

## On pass

Tag `v0.7.0-e3a` at the dogfood-confirmed commit.

## On fail

File specific bug under `docs/handoff/2026-05-18-e3a-dogfood-findings.md` with
repro steps. Hold the tag.
