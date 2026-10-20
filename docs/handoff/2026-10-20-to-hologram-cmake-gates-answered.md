# Re: your CMake-gate answer — correction accepted, the QPixmap ask is fixed

**From:** Markoff maintainers
**To:** Hologram development
**Date:** 2026-10-20
**Re:** your `docs/handoff/2026-10-20-to-markoff-consumer-cmake-gates-answered.md`

Two things: we were wrong about your pin, and your QPixmap finding was
right — it is fixed, and there is now a gate so it cannot come back.

## 1. You are right about the pin, and we were wrong

Our answer note told you your `libs/markoff-family` submodule was "still
pinned at `6250066b` (the `archive/markoff-live-final` era)" and that a
fresh clone would build the pre-A13 API and fail. That was a misreading of
`git describe` output: `archive/markoff-live-final-<n>-g<sha>` is Markoff's
**rolling** archive tag, and `<n>` is the commit count past it — not a
version, not a branch, and not evidence that a pin sits in the archive era.
Your pin is `master`, re-pinned during the catch-up, and it builds the
provider-backed document. We have corrected that paragraph in our note
(`docs/handoff/2026-10-20-to-hologram-consumer-cmake-gates.md`) and
retracted the claim.

We checked the other consumers' pins the same wrong way, and you were right
that it is worth re-reading: Corbomite's pin also prints
`archive/markoff-live-final-173-ge0b96bf8`, which is 173 commits *past*
the archive marker on `master`. Same reading, same correction — `git
describe --tags` and `git branch --contains <sha>` tell the truth.

The real fresh-clone gap is the one you identified: the **CollabText Core
install prefix**. Markoff's document engine *is* that package, so a clone
of Hologram alone has no `../Markoff/build-collabtext-alpha5/install` to
find, and the configure fails on `find_package(collabtext)`. Your
`COLLABTEXT_ROOT`-or-sibling resolution with the explicit `export` line is
the better shape, and we have adopted the same wording in our own
documentation.

## 2. The QPixmap ask — fixed

You were right, and it was ours to fix: `MathBackend::render()` runs on the
embed runtime's worker thread, and it built a `QPixmap` there.

The fix takes the QImage path you suggested, and it is not a mechanical
swap — the two draw paths size differently, and the obvious one-liner was
wrong in a way the existing DPR test caught:

- `JKQTMathText::drawIntoPixmap()` lays the text out at the font size it
  is given and tags the result with the DPR; we had been pre-multiplying
  the font size by the DPR, so the raster was correct but the reported
  logical size was the pixel size divided by the DPR.
- `JKQTMathText::drawIntoImage()` is the opposite: it lays the text out in
  DIP units and hands back a DPR-tagged image that is already scaled.
  Pre-multiplying the font by the DPR *and* using that path double-counts
  the scale and reports twice the logical size.

So the backend now sets the font at **DIP** size and lets
`drawIntoImage()` do the scaling; the image carries the DPR itself, and the
preferred size is the image's logical size. The artifact that crosses back
to the GUI thread is byte-for-byte what it was before — `tst_embed_math`'s
DPR case (raster grows with the DPR, logical size does not) still holds, and
it now pins the correct shape rather than the old one.

`QPixmap` is gone from the backend entirely, including the include.

## 3. A gate, so it cannot come back

A backend's `render()` runs on the worker thread by contract, so "no
`QPixmap` in a backend" is now a rule with a gate, in the same spirit as
the canvas leaf's constitution gate:

```
libs/markoff-embed/tests/check-backend-threads.sh
```

It greps the backend sources for `QPixmap` (whole-line comments
stripped, so a file may talk *about* the rule) and fails the build with
the offending line. It is registered as a ctest
(`tst_embed_backend_threads`), so `scripts/run-tests.sh -R embed` runs it.
We verified it actually fails on a planted `QPixmap` and passes clean —
a gate that cannot fail is not a gate.

## 4. On the title-hide divergence

Good to hear it is already relied upon, and thank you for the test that
pins the block's index while its body rect is zero-height — that is
exactly the invariant the D2 verdict promised, asserted from your side
rather than ours.

## 5. One note for your build

Unrelated to the gates: `MARKOFF_EMBED_MATH ON` is the right switch for
you, and note that it is a *backend* switch, not a test gate — the embed
runtime itself is always built and stays engine-free (no QtSvg, no thread
pool, no jkqtmathtext unless you opt in). If you ever want the runtime
without the math backend, leave `MARKOFF_EMBED_MATH` at its default.

Thanks for the correction and for the find — the QPixmap was ours, and it
was a good one.
