# R1A — Foundation: parseInputEditSequence threading

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `MarkoffDocument::parseUpdated` to carry, as a fourth argument, the value of `editSequence` at the moment the parser captured its input bytes — so view-layer consumers can compute per-row freshness ("the parser's view of row N is current iff no edit to row N has arrived since this parse's input was captured").

**Architecture:** The plumbing chain is `applyLocalEdit / resetContent (capture editSequence)` → `ParsePool::schedule(utf8, inputEditSeq)` → `Private::pendingInputEditSeq` → `dispatch()` → `ParsePoolWorker::parseSnapshot/parseReset` → `worker::parsed(doc, gen, inputEditSeq)` → `ParsePool::parseReady(doc, inputEditSeq)` → `MarkoffDocument`'s lambda relay → `parseUpdated(doc, parseSeq, anchors, parseInputEditSeq)`. Single new field threaded through six sites; the existing coalescing logic adopts it.

**Tech stack:** C++20, Qt6.8, CMake 3.19+. `QTest` + `QSignalSpy` for tests. Builds in `build-dev/`.

**Reference spec:** `docs/specs/2026-05-02-live-render-restoration-design.md` §4.6 ("Foundation delta"). The companion docs in `docs/2026-05-02-live-view-architectural-audit.md` and `docs/specs/2026-05-02-d-evolution-proposal.md` provide context but aren't required reading for this plan.

**Out of scope for R1A:**
- View-layer consumers acting on the new value (R4 territory).
- The `markoff-parser` per-block inline span pre-bake (R1B, separate plan).
- The new `markoff-live-render` library (R1C, separate plan).

---

## File map

**Modified (foundation):**
- `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h` — add 4th arg to `parseUpdated` signal; documentation.
- `libs/markoff-foundation/src/MarkoffDocument.cpp` — capture editSequence at schedule time in `applyLocalEdit` and `resetContent`; thread into ParsePool calls; relay through to `parseUpdated`.
- `libs/markoff-foundation/src/ParsePool.h` — add `inputEditSeq` parameter to `schedule()` and `scheduleReset()`; add it to the `parseReady` signal.
- `libs/markoff-foundation/src/ParsePool.cpp` — store pending `inputEditSeq` alongside `pending`/`pendingGen`; thread through `dispatch()`; relay from worker.
- `libs/markoff-foundation/src/ParsePoolWorker.h` — add `inputEditSeq` to `parseSnapshot`/`parseReset` slots and the `parsed` signal.
- `libs/markoff-foundation/src/ParsePoolWorker.cpp` — pass `inputEditSeq` through the parse functions; emit on `parsed`.

**Modified (downstream consumer; ignore-the-new-arg):**
- `libs/markoff-view-qml/src/EditorBackend.cpp` — the existing connection to `MarkoffDocument::parseUpdated` adopts the 4-arg shape; the new arg is unused for now.

**New (test):**
- `libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq.cpp` — exercises the full path: applyLocalEdit → wait for parseUpdated → assert 4th arg equals the editSequence value captured at apply time.
- `libs/markoff-foundation/tests/CMakeLists.txt` — register the new test executable.

**No changes (verified, will pass after plumbing):**
- All other foundation tests, parser tests, view-qml tests.

---

## Tasks

### Task 1: Read context

- [ ] **Step 1: Read the design references**

Read these files in order. Don't skim — the relationships are subtle.

```
docs/specs/2026-05-02-live-render-restoration-design.md      §4.6 only (~30 lines)
libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h
libs/markoff-foundation/src/MarkoffDocument.cpp              lines 15–110
libs/markoff-foundation/src/ParsePool.h
libs/markoff-foundation/src/ParsePool.cpp
libs/markoff-foundation/src/ParsePoolWorker.h
libs/markoff-foundation/src/ParsePoolWorker.cpp
```

Confirm in your head the signal-relay chain: `worker::parsed` (worker thread, queued connection) → ParsePool's lambda receiver (main thread) → `ParsePool::parseReady` → MarkoffDocument's lambda receiver → `MarkoffDocument::parseUpdated`.

No code changes in this task.

---

### Task 2: Add the failing acceptance test

**Files:**
- Create: `libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq.cpp`
- Modify: `libs/markoff-foundation/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test file**

Create `libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq.cpp` with:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationParseInputEditSeq : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void parse_carries_input_edit_sequence() {
        MarkoffDocument d{/*replicaId=*/1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        // Apply one local edit; capture the editSequence afterwards.
        MarkoffEdit e;
        e.oldStart = 0; e.oldEnd = 0; e.newText = "hello";
        d.applyLocalEdit({e});
        const quint64 seqAfterApply = d.editSequence();

        // Wait for the asynchronous parse to deliver parseUpdated.
        QVERIFY(spy.wait(2000));

        // The signal carries (parsed*, parseSequence, blockAnchors,
        // parseInputEditSequence). The 4th argument is the editSequence
        // captured when the parse input was scheduled — it should equal
        // the value editSequence held immediately after the apply that
        // triggered this parse.
        QVERIFY(spy.count() >= 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.size(), 4);
        const quint64 carried = args.at(3).toULongLong();
        QCOMPARE(carried, seqAfterApply);
    }

    void parse_input_seq_increases_across_edits() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        // First edit + parse.
        MarkoffEdit e1;
        e1.oldStart = 0; e1.oldEnd = 0; e1.newText = "a";
        d.applyLocalEdit({e1});
        const quint64 seq1 = d.editSequence();
        QVERIFY(spy.wait(2000));
        // Drain in case multiple parses fired (coalesce + reset on first
        // load); the LAST one is what reflects current state.
        while (spy.wait(50)) { /* drain */ }
        const quint64 carried1 = spy.takeLast().at(3).toULongLong();
        QCOMPARE(carried1, seq1);

        // Second edit + parse.
        MarkoffEdit e2;
        e2.oldStart = 1; e2.oldEnd = 1; e2.newText = "b";
        d.applyLocalEdit({e2});
        const quint64 seq2 = d.editSequence();
        QVERIFY(seq2 > seq1);
        spy.clear();
        QVERIFY(spy.wait(2000));
        while (spy.wait(50)) { /* drain */ }
        const quint64 carried2 = spy.takeLast().at(3).toULongLong();
        QCOMPARE(carried2, seq2);
    }

    void reset_content_carries_input_edit_sequence() {
        MarkoffDocument d{1};
        QSignalSpy spy(&d, &MarkoffDocument::parseUpdated);

        d.resetContent("# heading\n\nbody\n", Origin::FirstOpen);
        const quint64 seqAfterReset = d.editSequence();
        QVERIFY(spy.wait(2000));
        while (spy.wait(50)) { /* drain */ }
        const quint64 carried = spy.takeLast().at(3).toULongLong();
        QCOMPARE(carried, seqAfterReset);
    }
};

QTEST_MAIN(TstFoundationParseInputEditSeq)
#include "tst_foundation_parse_input_edit_seq.moc"
```

- [ ] **Step 2: Register the test in the foundation tests' CMakeLists**

Open `libs/markoff-foundation/tests/CMakeLists.txt` and find the block where existing tests like `tst_foundation_edit_sequence` are registered. The pattern is repeated per test; add a new entry following the same pattern. The exact CMake helper macro will be `markoff_foundation_test(...)` or similar — read the file to confirm and copy the most-similar test's invocation, replacing the source filename with `tst_foundation_parse_input_edit_seq.cpp`.

```cmake
# (Pattern; verify against the existing entries before pasting verbatim.)
markoff_foundation_test(tst_foundation_parse_input_edit_seq
    SOURCES tst_foundation_parse_input_edit_seq.cpp
)
```

If the file uses a different macro or per-test boilerplate, follow the existing pattern *exactly*. The single-line source registration is the only change you should be making here.

- [ ] **Step 3: Configure and attempt to build the test, verify it fails to compile**

```bash
cmake --build build-dev --target tst_foundation_parse_input_edit_seq -j 8
```

Expected: build fails at the line `const QList<QVariant> args = spy.takeFirst();` followed by `QCOMPARE(args.size(), 4);`. The reason: the signal currently emits 3 arguments, so `spy.takeFirst()` produces a list of size 3 — the test asserts 4. Since `QSignalSpy` does runtime introspection, this is a *runtime* fail, not a compile fail. The test compiles fine; the `QCOMPARE(args.size(), 4)` assertion will fail at runtime.

If the test does compile, run it and confirm the runtime failure:

```bash
./build-dev/libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq
```

Expected: `parse_carries_input_edit_sequence` fails with `Compared values are not the same: Actual (args.size()): 3   Expected (4): 4`.

- [ ] **Step 4: Commit the failing test**

```bash
git add libs/markoff-foundation/tests/tst_foundation_parse_input_edit_seq.cpp \
        libs/markoff-foundation/tests/CMakeLists.txt
git commit -m "test(foundation): parseUpdated carries parseInputEditSequence (failing)"
```

The commit lands a deliberately-failing test — that's the TDD cycle. The next tasks make it pass.

---

### Task 3: Thread inputEditSeq through ParsePoolWorker

**Files:**
- Modify: `libs/markoff-foundation/src/ParsePoolWorker.h`
- Modify: `libs/markoff-foundation/src/ParsePoolWorker.cpp`

The worker is the lowest-level link in the chain. We add the new value as a parameter on its slots and on the `parsed` signal. The worker doesn't *use* the value — it just forwards it — but the signal-slot signatures must accept it for the upper layers to plumb it through.

- [ ] **Step 1: Update `ParsePoolWorker.h`**

Open `libs/markoff-foundation/src/ParsePoolWorker.h`. The current slot signatures are:

```cpp
void parseSnapshot(QByteArray utf8, quint64 generation);
void parseReset(QByteArray utf8, quint64 generation);
```

Change to:

```cpp
void parseSnapshot(QByteArray utf8, quint64 generation, quint64 inputEditSeq);
void parseReset(QByteArray utf8, quint64 generation, quint64 inputEditSeq);
```

The current signal is:

```cpp
Q_SIGNALS:
    void parsed(Markoff::Document *result, quint64 generation);
```

Change to:

```cpp
Q_SIGNALS:
    void parsed(Markoff::Document *result, quint64 generation, quint64 inputEditSeq);
```

- [ ] **Step 2: Update `ParsePoolWorker.cpp`**

Open `libs/markoff-foundation/src/ParsePoolWorker.cpp`. The `parseSnapshot` and `parseReset` function bodies need to:
1. Accept the new parameter (signature update mirrors the header).
2. Forward it through to `Q_EMIT parsed(...)`.

The exact source layout you'll find varies, but each function ends with a `Q_EMIT parsed(...)` call. Update each emit site:

```cpp
// before:
Q_EMIT parsed(result, generation);
// after:
Q_EMIT parsed(result, generation, inputEditSeq);
```

Update the function signatures to match the header.

- [ ] **Step 3: Build and verify the worker compiles in isolation**

```bash
cmake --build build-dev --target markoff_foundation -j 8
```

Expected: a chain of compile errors in `ParsePool.cpp` (where `worker->parseSnapshot(...)` and `worker->parseReset(...)` are invoked, plus the `connect(worker, &ParsePoolWorker::parsed, ...)` lambda), because their callers haven't been updated yet. **This is the expected intermediate state**; the next task fixes ParsePool.

If `markoff_foundation` builds cleanly without errors, you've miscounted — likely the worker emits weren't updated. Re-check.

---

### Task 4: Thread inputEditSeq through ParsePool

**Files:**
- Modify: `libs/markoff-foundation/src/ParsePool.h`
- Modify: `libs/markoff-foundation/src/ParsePool.cpp`

ParsePool is the middle link. It holds the value while parses are pending (coalescing) and re-emits to the main thread.

- [ ] **Step 1: Update `ParsePool.h`**

Open `libs/markoff-foundation/src/ParsePool.h`. The current public methods:

```cpp
void schedule(QByteArray utf8);
void scheduleReset(QByteArray utf8);
```

Change to:

```cpp
void schedule(QByteArray utf8, quint64 inputEditSeq);
void scheduleReset(QByteArray utf8, quint64 inputEditSeq);
```

The current signal:

```cpp
Q_SIGNALS:
    void parseReady(const Markoff::Document *parsed);
```

Change to:

```cpp
Q_SIGNALS:
    void parseReady(const Markoff::Document *parsed, quint64 inputEditSeq);
```

- [ ] **Step 2: Update `ParsePool.cpp` — the `Private` struct**

Open `libs/markoff-foundation/src/ParsePool.cpp`. Find the `Private` struct (~line 21). Add a new field alongside `pendingGen`:

```cpp
struct ParsePool::Private {
    QThread          *thread = nullptr;
    ParsePoolWorker  *worker = nullptr;
    mutable QMutex    mutex;
    quint64           generation = 0;
    bool              workerBusy = false;
    QByteArray        pending;
    quint64           pendingGen = 0;
    quint64           pendingInputEditSeq = 0;   // NEW
    ParseKind         pendingKind = ParseKind::Incremental;
    Markoff::Render::RenderPhaseTaps *taps = nullptr;
};
```

- [ ] **Step 3: Update the `dispatch()` helper**

Find the file-local `dispatch` helper (~line 35). Update it to accept and forward `inputEditSeq`:

```cpp
namespace {
void dispatch(ParsePoolWorker *worker, QByteArray utf8, quint64 gen,
              quint64 inputEditSeq, ParseKind kind)
{
    if (kind == ParseKind::Reset) {
        QMetaObject::invokeMethod(worker,
            [worker, b = std::move(utf8), gen, inputEditSeq]() mutable {
                worker->parseReset(std::move(b), gen, inputEditSeq);
            },
            Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(worker,
            [worker, b = std::move(utf8), gen, inputEditSeq]() mutable {
                worker->parseSnapshot(std::move(b), gen, inputEditSeq);
            },
            Qt::QueuedConnection);
    }
}
}
```

- [ ] **Step 4: Update `schedule()` and `scheduleReset()`**

Find `ParsePool::schedule` (~line 130) and `ParsePool::scheduleReset` (~line 157). Update each to accept the new parameter and update the coalesce-state to track it:

```cpp
void ParsePool::schedule(QByteArray utf8, quint64 inputEditSeq)
{
    quint64   gen;
    ParseKind kind = ParseKind::Incremental;
    bool      dispatchNow = false;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generation;
        if (d->workerBusy) {
            d->pending             = std::move(utf8);
            d->pendingGen          = gen;
            d->pendingInputEditSeq = inputEditSeq;   // overwrite-coalesce
            // pendingKind unchanged (Reset stays Reset).
        } else {
            d->workerBusy = true;
            dispatchNow   = true;
        }
    }
    if (dispatchNow) {
        dispatch(d->worker, std::move(utf8), gen, inputEditSeq, kind);
    }
}

void ParsePool::scheduleReset(QByteArray utf8, quint64 inputEditSeq)
{
    quint64 gen;
    bool    dispatchNow = false;
    {
        QMutexLocker lk(&d->mutex);
        gen = ++d->generation;
        if (d->workerBusy) {
            d->pending             = std::move(utf8);
            d->pendingGen          = gen;
            d->pendingInputEditSeq = inputEditSeq;
            d->pendingKind         = ParseKind::Reset;
        } else {
            d->workerBusy = true;
            dispatchNow   = true;
        }
    }
    if (dispatchNow) {
        dispatch(d->worker, std::move(utf8), gen, inputEditSeq, ParseKind::Reset);
    }
}
```

- [ ] **Step 5: Update the worker `parsed` connection lambda**

Find the connection in `ParsePool`'s constructor (~line 73). The current lambda:

```cpp
connect(d->worker, &ParsePoolWorker::parsed,
        this, [this](Markoff::Document *parsed, quint64 gen) { ... });
```

Update to take the third argument and propagate it. Also add `nextInputEditSeq` to the coalesce-state-pop block:

```cpp
connect(d->worker, &ParsePoolWorker::parsed,
        this, [this](Markoff::Document *parsed, quint64 gen, quint64 inputEditSeq) {
    Markoff::Render::RenderPhaseTaps *taps = d->taps;
    if (taps) taps->tMainSlotEntryNs.store(Markoff::Render::nowNs(),
                                           std::memory_order_release);

    QByteArray nextSnapshot;
    quint64    nextGen = 0;
    quint64    nextInputEditSeq = 0;
    ParseKind  nextKind = ParseKind::Incremental;
    {
        QMutexLocker lk(&d->mutex);
        if (gen != d->generation) {
            delete parsed;
            parsed = nullptr;
        }
        if (!d->pending.isEmpty() || d->pendingGen != 0) {
            nextSnapshot     = std::move(d->pending);
            nextGen          = d->pendingGen;
            nextInputEditSeq = d->pendingInputEditSeq;
            nextKind         = d->pendingKind;
            d->pending.clear();
            d->pendingGen          = 0;
            d->pendingInputEditSeq = 0;
            d->pendingKind         = ParseKind::Incremental;
        } else {
            d->workerBusy = false;
        }
    }
    if (parsed)
        Q_EMIT parseReady(static_cast<const Markoff::Document *>(parsed), inputEditSeq);
    if (taps) taps->tModelDoneNs.store(Markoff::Render::nowNs(),
                                       std::memory_order_release);

    if (nextGen != 0)
        dispatch(d->worker, std::move(nextSnapshot), nextGen, nextInputEditSeq, nextKind);
}, Qt::QueuedConnection);
```

- [ ] **Step 6: Build and verify ParsePool compiles**

```bash
cmake --build build-dev --target markoff_foundation -j 8
```

Expected: compile errors now move to `MarkoffDocument.cpp`, where `parsePool.schedule(utf8)` and `parsePool.scheduleReset(utf8)` are called, plus the connection lambda for `parseReady`. Next task fixes those.

---

### Task 5: Thread inputEditSeq through MarkoffDocument

**Files:**
- Modify: `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-foundation/src/MarkoffDocument.cpp`

The top of the chain. `MarkoffDocument` captures `editSequence` at scheduling time, threads through the pool, and re-emits as the 4th arg of `parseUpdated`.

- [ ] **Step 1: Update `MarkoffDocument.h`**

Open `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h`. Find the `parseUpdated` signal declaration (line 159):

```cpp
void parseUpdated(const Markoff::Document *parsed,
                  quint64 parseSequence,
                  QList<Markoff::BlockAnchor> blockAnchors);
```

Change to:

```cpp
void parseUpdated(const Markoff::Document *parsed,
                  quint64 parseSequence,
                  QList<Markoff::BlockAnchor> blockAnchors,
                  quint64 parseInputEditSequence);
```

Also update the doc comment for `parseSequence()` (around line 71) to add a brief note about `parseInputEditSequence`. Add a new sentence:

```
/// `parseUpdated` also carries `parseInputEditSequence` — the value of
/// `editSequence` at the moment this parse's input bytes were captured.
/// View consumers compare this against per-row last-edit sequence to
/// decide whether the parse output for a given row is stale relative
/// to user intent. See restoration spec §4.
```

(If you'd rather, place this on the signal declaration directly — wherever it lives in the codebase's existing doc-comment style. The point is to document the new semantics for future readers.)

- [ ] **Step 2: Update `MarkoffDocument.cpp` — the constructor lambda**

Open `libs/markoff-foundation/src/MarkoffDocument.cpp`. Find the constructor's `parseReady` connection (~line 25). Update the lambda signature to accept the new arg and pass it through to `parseUpdated`:

```cpp
QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady,
                 this, [this](const Markoff::Document *p, quint64 inputEditSeq) {
                     d->latestParse.reset(p);
                     ++d->parseSequence;

                     auto bundle = Markoff::Detail::computeBlockAnchors(*this, p);
                     d->latestBlockAnchors = std::move(bundle.anchors);
                     d->latestBlockRanges  = std::move(bundle.ranges);

                     Q_EMIT parseUpdated(p, d->parseSequence,
                                         d->latestBlockAnchors, inputEditSeq);
                 });
```

- [ ] **Step 3: Update `applyLocalEdit` and `resetContent` to pass editSequence to the pool**

Find every site in `MarkoffDocument.cpp` where `d->parsePool.schedule(...)` or `d->parsePool.scheduleReset(...)` is called. There are typically two: one inside `applyLocalEdit` (around line 145–155, after the buffer apply) and one inside `resetContent` (around line 215–235).

The pattern is: `editSequence` has *just* been bumped (by `++d->editSequence` near the top of each method), so its current value is the post-edit sequence number. Pass that to the pool.

For `applyLocalEdit`:

```cpp
// (Inside applyLocalEdit, after d->buffer.apply_local_edit and any other
// state updates, at the existing schedule call site:)
d->parsePool.schedule(toMarkdownUtf8(), d->editSequence);
```

For `resetContent`:

```cpp
// (Inside resetContent, at the existing scheduleReset call site:)
d->parsePool.scheduleReset(d->buffer.text_q(), d->editSequence);
//                          ^^^^^^^^^^^^^^^^^^^^
// Whatever the existing utf8-source expression is — leave it unchanged;
// only add the second argument.
```

Search the file for both `parsePool.schedule(` and `parsePool.scheduleReset(`. Update each call. There may also be similar calls in `undo`, `redo`, and `applyRemoteOps` — check each; if they call `schedule` or `scheduleReset`, add the `d->editSequence` argument identically. (`editSequence` is bumped at the top of each of these state-change methods.)

- [ ] **Step 4: Build the foundation**

```bash
cmake --build build-dev --target markoff_foundation -j 8
```

Expected: clean build of `markoff_foundation`. If errors remain in `markoff_foundation`, you've missed a `parsePool.schedule(` call site — search again.

Compile errors in `markoff_view_qml` are expected; the next task fixes them.

---

### Task 6: Update markoff-view-qml's parseUpdated consumer

**Files:**
- Modify: `libs/markoff-view-qml/src/EditorBackend.cpp`
- Possibly modify: `libs/markoff-view-qml/include/markoff/view/qml/EditorBackend.h` (if it forward-declares the signal arity)

The view library subscribes to `MarkoffDocument::parseUpdated` to relay it onward as `EditorBackend::parseUpdatedAt`. We need to accept the new 4-arg shape from the foundation but the view's own `parseUpdatedAt` signal **does not change** in this plan — view-layer consumers (currently only the source-mode editor and its tests) don't act on the new value yet. R4 will revisit this.

- [ ] **Step 1: Find the connection to `MarkoffDocument::parseUpdated`**

Search:

```bash
grep -n 'parseUpdated' libs/markoff-view-qml/src/EditorBackend.cpp \
                       libs/markoff-view-qml/include/markoff/view/qml/EditorBackend.h
```

You'll find a `connect(...)` invocation in `EditorBackend.cpp` linking `MarkoffDocument::parseUpdated` to a slot or lambda that re-emits `EditorBackend::parseUpdatedAt`.

- [ ] **Step 2: Update the connecting lambda to accept the 4th arg**

The current lambda is roughly:

```cpp
QObject::connect(m_document, &Markoff::MarkoffDocument::parseUpdated,
    this, [this](const Markoff::Document *parsed,
                 quint64 parseSeq,
                 QList<Markoff::BlockAnchor> anchors) {
        Q_EMIT parseUpdatedAt(QVariant::fromValue(parsed), parseSeq, anchors);
    });
```

Update to:

```cpp
QObject::connect(m_document, &Markoff::MarkoffDocument::parseUpdated,
    this, [this](const Markoff::Document *parsed,
                 quint64 parseSeq,
                 QList<Markoff::BlockAnchor> anchors,
                 quint64 /*parseInputEditSeq*/) {
        Q_EMIT parseUpdatedAt(QVariant::fromValue(parsed), parseSeq, anchors);
    });
```

The 4th parameter is named in a comment only (it's unused for now). The view's own `parseUpdatedAt` signal is unchanged.

- [ ] **Step 3: Build markoff-view-qml**

```bash
cmake --build build-dev --target markoff_view_qml -j 8
```

Expected: clean build. If there are other consumers of `parseUpdated` (e.g. another file connecting to it), update them similarly to accept-and-ignore the new arg.

- [ ] **Step 4: Build everything**

```bash
cmake --build build-dev -j 8
```

Expected: clean build of all targets including the test executables.

---

### Task 7: Run the test, verify pass

- [ ] **Step 1: Run the new test**

```bash
ctest --test-dir build-dev -R '^tst_foundation_parse_input_edit_seq$' --output-on-failure
```

Expected: pass — three test cases green.

If it fails, the most likely cause is one of:
- A `parsePool.schedule(...)` call site you missed in MarkoffDocument.cpp, so the new arg defaults to 0 → `QCOMPARE` against `seqAfterApply > 0` fails.
- An ordering error: `editSequence` captured before `++d->editSequence`. Re-check the `applyLocalEdit` / `resetContent` call sites — `++d->editSequence` runs *first*, then the value is passed to `schedule`.

- [ ] **Step 2: Run the full foundation+parser+view-qml regression suite**

```bash
ctest --test-dir build-dev -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml" --output-on-failure -j 8
```

Expected: all green. The pre-restoration baseline at branch tip is 107 fast-tier passing; this plan adds one more test, so 108 expected.

`tst_view_qml_live_view_qml` is excluded because its baseline is 9/3/2 (pre-existing, not the responsibility of this plan to fix).

---

### Task 8: Commit

- [ ] **Step 1: Review the diff**

```bash
git diff --stat
git diff
```

Expected files modified:
- `libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h`
- `libs/markoff-foundation/src/MarkoffDocument.cpp`
- `libs/markoff-foundation/src/ParsePool.h`
- `libs/markoff-foundation/src/ParsePool.cpp`
- `libs/markoff-foundation/src/ParsePoolWorker.h`
- `libs/markoff-foundation/src/ParsePoolWorker.cpp`
- `libs/markoff-view-qml/src/EditorBackend.cpp` (and possibly its header)
- `libs/markoff-foundation/tests/CMakeLists.txt` (already in the prior commit; should be no further change here)

- [ ] **Step 2: Commit**

```bash
git add libs/markoff-foundation/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-foundation/src/MarkoffDocument.cpp \
        libs/markoff-foundation/src/ParsePool.h \
        libs/markoff-foundation/src/ParsePool.cpp \
        libs/markoff-foundation/src/ParsePoolWorker.h \
        libs/markoff-foundation/src/ParsePoolWorker.cpp \
        libs/markoff-view-qml/src/EditorBackend.cpp
# and EditorBackend.h if you touched it

git commit -m "$(cat <<'EOF'
feat(foundation): parseUpdated carries parseInputEditSequence

Threads the editSequence captured at parse-input scheduling through
ParsePoolWorker → ParsePool → MarkoffDocument, exposing it as a fourth
argument on parseUpdated.

View-layer consumers (sequence-tagged freshness rule per restoration
spec §4) compute per-row staleness as
    parseFreshForRow(R) = (R.lastEditEditSequence <= parseInputEditSeq)
and apply parse output selectively. R1A lands the foundation surface;
view-layer adoption follows in R4.

The signal change is binding for MarkoffDocument::parseUpdated; the
view-qml relay is updated to accept-and-ignore the new arg pending R4.
EOF
)"
```

Verify with:

```bash
git log --oneline -2
git status
```

Expected: working tree clean; the new commit on tip plus the prior failing-test commit.

---

## Self-review

After completing all tasks:

- **Spec coverage.** §4.6 of the restoration spec asks for one foundation delta: `parseUpdated` carries `parseInputEditSequence`. R1A delivers exactly that. ✓
- **Type consistency.** `parseInputEditSequence` is `quint64` end-to-end. `editSequence()` returns `quint64`. ✓
- **Cycle-guard impact.** None expected — we're adding a parameter, not changing behaviour for existing consumers (except the view's existing relay, updated to accept-and-ignore).
- **Regression risk.** Bounded: every old call site to `parseUpdated`-listening code is updated to the new shape. The signal's previous shape is no longer reachable. Tests covered the previously-emitted args; they continue to pass because the args' semantics didn't change.

If `tst_view_qml_live_view_qml`'s baseline was 9/3/2 before R1A and is *worse* after, treat that as a real regression and diagnose. The new arg should not affect that test's behaviour; if it does, you've inadvertently changed a behavioural surface.

---

## Acceptance criterion

This plan is complete when:

1. `tst_foundation_parse_input_edit_seq` passes (3/3 green).
2. The full fast-tier suite (`ctest -E "tst_realistic|tst_benchmark|tst_view_qml_live_view_qml"`) passes — 108/108 expected.
3. `tst_view_qml_live_view_qml` baseline (9/3/2) is preserved or improves; not worse.
4. The two commits (failing-test commit + plumbing commit) are on the branch.

Hand off to R1B (`docs/plans/2026-05-02-live-render-r1b-inline-span-bake.md`) and R1C (`docs/plans/2026-05-02-live-render-r1c-library-scaffold.md`) — both can land in any order relative to R1A; the three together complete the R1 phase per the restoration spec §11.
