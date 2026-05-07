> **Status: completed.** Same as Part 1 — feature-complete, all tests green. Do not execute.

# Markoff Foundation Library Implementation Plan — Part 2

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**This is Part 2 of [`docs/plans/2026-04-28-foundation-library.md`](./2026-04-28-foundation-library.md).** It expands the previously-summarized Tasks 18–55 into TDD-detailed task blocks following the same template Part 1 used for Tasks 1–17.

**Pre-conditions:** Tasks 1–17 of Part 1 have landed (commits `90f659d` … `8a121bf`). The library `markoff_core` exists and the `tst_markoff_document`, `tst_markoff_edit`, `tst_anchor_json`, `tst_selection`, `tst_fold_ref` tests are green.

**Naming convention (this part only):** Foundation test executables and the source files that define them use a `tst_foundation_*` prefix (e.g. `tst_foundation_session`, `tst_foundation_session.cpp`) to avoid collision with `libs/markoff-core/tests/` test targets such as `tst_markoff_document`. This differs from Part 1, where the source files are `tst_markoff_edit.cpp` etc.; the existing Part 1 targets stay as-is (only the foundation `tst_markoff_document` was renamed to `tst_foundation_markoff_document` during Task 9 execution to break the collision).

**Refer to Part 1** for the spec / audit / branch / architecture context, file structure, and the explicit list of foundation files.

**Implementation lessons applied throughout this part:**

- `SessionParams` was forward-declared in Part 1's `MarkoffDocument.h`, forcing `createSession(const SessionParams &)` (no default arg). **Task 18 restores the `= {}` default after `SessionParams` becomes a complete type.**
- PIMPL pattern from `MarkoffDocument::Private` (in `src/MarkoffDocumentPrivate.h`) is mirrored for any new PIMPL classes (`Session`, `SearchEngine`, etc.).
- Foundation stays widget-free: any custom test `main()` uses `QCoreApplication`, not `QApplication`.
- `Q_DECLARE_METATYPE` lands at the test site unless a signal payload type needs to cross threads, in which case it goes in the public header next to the type.
- collabtext API uses `<crdt/Anchor.h>`, `<crdt/Buffer.h>`, `<crdt/Clock.h>`, `<crdt/Operations.h>` — public via collabtext's own `target_include_directories(... PUBLIC src)`.
- Empty-list defensive check on signal-emitting functions: only emit `contentsChanged` when the resulting edit list is non-empty.
- Clearing the undo stack uses `set_max_undo_depth(0); set_max_undo_depth(saved);` — see Part 1's Task 16 for the prescribed pattern.
- For each command, follow the spec's "edits-only function plus a thin convenience wrapper" pattern: `editsForX(...)` returns `QList<MarkoffEdit>`; `X(doc, sess, ...)` calls `doc->applyLocalEdit(editsForX(...))`. Tests assert on the edit-list shape first (no doc mutation), then assert on the doc state after the convenience wrapper.

---

## Phase 4 — Sessions (Tasks 18–23)

Implements spec §7.2. `Session` is a per-view ephemeral state container owned by `MarkoffDocument`. It carries the primary selection, kinded secondary selections, scroll state (anchor + fraction), and folded regions. Hot-swap is supported via `copyStateFrom(other)`. JSON round-trip is supported via `toJson()` / `fromJson()`.

### Task 18: SessionParams + Session header skeleton

This task introduces the `SessionParams` value struct and the `Session` Q_OBJECT skeleton (constructor, identity getters, no-op signal declarations). Once `SessionParams` is a complete type, the forward-declared default arg on `MarkoffDocument::createSession` is restored. Implements the `Session` shell from spec §7.2.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/SessionParams.h`
- Create: `libs/markoff-core/include/markoff-foundation/Session.h`
- Create: `libs/markoff-core/src/Session.cpp`
- Create: `libs/markoff-core/src/SessionPrivate.h`
- Create: `libs/markoff-core/tests/tst_foundation_session.cpp`
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h` (restore `= {}` default arg)
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test (identity only, this task)**

Create `libs/markoff-core/tests/tst_foundation_session.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>
#include <QString>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SessionParams.h>

using namespace Markoff;

class TstFoundationSession : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void session_carries_construction_params() {
        MarkoffDocument doc(1);
        SessionParams params;
        params.participantId    = QStringLiteral("alice");
        params.participantLabel = QStringLiteral("Alice");
        params.presenceColor    = QColor(Qt::magenta);

        // createSession is wired in Task 23; for the skeleton task we
        // construct a Session directly with a parent document.
        Session *s = new Session(&doc, params);
        QCOMPARE(s->participantId(),    QStringLiteral("alice"));
        QCOMPARE(s->participantLabel(), QStringLiteral("Alice"));
        QCOMPARE(s->presenceColor().name(), QColor(Qt::magenta).name());
        QVERIFY(!s->id().isEmpty());
        delete s;
    }

    void session_default_params_have_empty_identity() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        QVERIFY(s->participantId().isEmpty());
        QVERIFY(s->participantLabel().isEmpty());
        delete s;
    }
};

QTEST_APPLESS_MAIN(TstFoundationSession)
#include "tst_foundation_session.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_session tst_foundation_session.cpp)
add_test(NAME tst_foundation_session COMMAND tst_foundation_session)
target_link_libraries(tst_foundation_session PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_session PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Verify the test fails to build**

```bash
cmake --build build-dev --target tst_foundation_session -j 2>&1 | tail -10
```

Expected: error: `markoff-foundation/Session.h: No such file or directory`.

- [ ] **Step 4: Create SessionParams header**

Create `libs/markoff-core/include/markoff-foundation/SessionParams.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QString>

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

/// Construction-time parameters for a Session. Empty participantId means
/// the session is local-only (no CRDT presence broadcast).
struct MARKOFF_FOUNDATION_EXPORT SessionParams {
    QString participantId;
    QString participantLabel;
    QColor  presenceColor;
};

}  // namespace Markoff
```

- [ ] **Step 5: Create Session header skeleton**

Create `libs/markoff-core/include/markoff-foundation/Session.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

#include <crdt/Anchor.h>

#include <markoff/core/FoldRef.h>
#include <markoff/core/MarkoffFoundationExport.h>
#include <markoff/core/Selection.h>
#include <markoff/core/SessionParams.h>

namespace Markoff {

class MarkoffDocument;

/// A view's per-document state. Owned by the parent MarkoffDocument
/// (which calls createSession / destroySession). Tracks primary +
/// secondary selections, scroll, folded regions. Hot-swap via
/// copyStateFrom. Persistence via toJson / fromJson. See spec §7.2.
class MARKOFF_FOUNDATION_EXPORT Session : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Session)
public:
    explicit Session(MarkoffDocument *doc, const SessionParams &params);
    ~Session() override;

    QString id() const;
    QString participantId() const;
    QString participantLabel() const;
    QColor  presenceColor() const;

    // Filled in Tasks 19-22.
    Selection primarySelection() const;
    void      setPrimarySelection(const Selection &);

    const QList<Selection> &secondarySelections() const;
    void setSecondarySelections(QList<Selection>);
    void addSecondarySelection(Selection);
    void clearSecondarySelectionsOfKind(Selection::Kind);

    CollabText::Crdt::Anchor topVisibleAnchor() const;
    qreal                    topVisibleFraction() const;
    void setTopVisible(CollabText::Crdt::Anchor, qreal fraction);

    const QList<FoldRef> &foldedRegions() const;
    void                  setFoldedRegions(QList<FoldRef>);
    void                  toggleFold(const FoldRef &);

    void copyStateFrom(const Session &other);

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &);

Q_SIGNALS:
    void primarySelectionChanged(const Markoff::Selection &);
    void secondarySelectionsChanged();
    void scrollChanged(CollabText::Crdt::Anchor, qreal fraction);
    void foldedRegionsChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
```

- [ ] **Step 6: Create SessionPrivate.h**

Create `libs/markoff-core/src/SessionPrivate.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QtGlobal>

#include <crdt/Anchor.h>

#include <markoff/core/FoldRef.h>
#include <markoff/core/Selection.h>

namespace Markoff {

class MarkoffDocument;

struct Session::Private {
    MarkoffDocument *doc = nullptr;
    QString          id;
    QString          participantId;
    QString          participantLabel;
    QColor           presenceColor;

    Selection                primary;
    QList<Selection>         secondaries;
    CollabText::Crdt::Anchor topAnchor;
    qreal                    topFraction = 0.0;
    QList<FoldRef>           folds;
};

}  // namespace Markoff
```

- [ ] **Step 7: Create Session.cpp skeleton**

Create `libs/markoff-core/src/Session.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Session.h>

#include <QUuid>

#include "SessionPrivate.h"

namespace Markoff {

Session::Session(MarkoffDocument *doc, const SessionParams &params)
    : QObject(reinterpret_cast<QObject *>(doc))
    , d(std::make_unique<Private>())
{
    d->doc = doc;
    d->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    d->participantId    = params.participantId;
    d->participantLabel = params.participantLabel;
    d->presenceColor    = params.presenceColor;
}

Session::~Session() = default;

QString Session::id() const               { return d->id; }
QString Session::participantId() const    { return d->participantId; }
QString Session::participantLabel() const { return d->participantLabel; }
QColor  Session::presenceColor() const    { return d->presenceColor; }

// Selection / scroll / fold getters return defaults until Tasks 19-22.
Selection Session::primarySelection() const { return d->primary; }
void Session::setPrimarySelection(const Selection &) {}

const QList<Selection> &Session::secondarySelections() const { return d->secondaries; }
void Session::setSecondarySelections(QList<Selection>) {}
void Session::addSecondarySelection(Selection) {}
void Session::clearSecondarySelectionsOfKind(Selection::Kind) {}

CollabText::Crdt::Anchor Session::topVisibleAnchor() const { return d->topAnchor; }
qreal                    Session::topVisibleFraction() const { return d->topFraction; }
void Session::setTopVisible(CollabText::Crdt::Anchor, qreal) {}

const QList<FoldRef> &Session::foldedRegions() const { return d->folds; }
void Session::setFoldedRegions(QList<FoldRef>) {}
void Session::toggleFold(const FoldRef &) {}

void Session::copyStateFrom(const Session &) {}
QJsonObject Session::toJson() const { return {}; }
void        Session::fromJson(const QJsonObject &) {}

}  // namespace Markoff
```

- [ ] **Step 8: Restore the `= {}` default arg on MarkoffDocument::createSession**

Edit `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`. Replace:

```cpp
struct SessionParams; // forward
```

with:

```cpp
#include <markoff/core/SessionParams.h>
```

(`SessionParams` is now a complete type and can be included directly.) Also drop the surrounding NOTE comment about the forward declaration, and restore the `= {}`:

```cpp
    Session *createSession(const SessionParams &params = {});
```

- [ ] **Step 9: Add to library target**

Edit `libs/markoff-core/CMakeLists.txt`. Append (after the existing `MarkoffDocument.h`, `MarkoffDocumentPrivate.h`, etc.):

```cmake
add_library(markoff_core STATIC
    include/markoff-foundation/MarkoffFoundationExport.h
    include/markoff-foundation/Origin.h
    include/markoff-foundation/MarkoffEdit.h
    include/markoff-foundation/AnchorJson.h
    include/markoff-foundation/Selection.h
    include/markoff-foundation/FoldRef.h
    include/markoff-foundation/MarkoffDocument.h
    include/markoff-foundation/SessionParams.h
    include/markoff-foundation/Session.h
    src/MarkoffDocumentPrivate.h
    src/SessionPrivate.h
    src/MarkoffEdit.cpp
    src/AnchorJson.cpp
    src/Selection.cpp
    src/FoldRef.cpp
    src/MarkoffDocument.cpp
    src/Session.cpp
)
```

- [ ] **Step 10: Build + run**

```bash
cmake --build build-dev --target tst_foundation_session -j 2>&1 | tail -5
ctest --test-dir build-dev -R '^tst_foundation_session$' --output-on-failure
```

Expected: 2 cases pass. Also rebuild `tst_foundation_markoff_document` to confirm the header restoration didn't regress:

```bash
cmake --build build-dev --target tst_foundation_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_markoff_document$' --output-on-failure
```

Expected: still passes.

- [ ] **Step 11: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/SessionParams.h \
        libs/markoff-core/include/markoff-foundation/Session.h \
        libs/markoff-core/src/Session.cpp \
        libs/markoff-core/src/SessionPrivate.h \
        libs/markoff-core/tests/tst_foundation_session.cpp \
        libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): SessionParams + Session skeleton; restore createSession default arg"
```

---

### Task 19: Session primary selection

Adds the primary selection getter / setter / `primarySelectionChanged` signal. Builds on Task 18's skeleton.

**Files:**
- Modify: `libs/markoff-core/src/Session.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_session.cpp`

- [ ] **Step 1: Write the failing test**

Append to the test class in `tst_foundation_session.cpp`:

```cpp
    void primary_selection_setter_round_trips() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sel;
        sel.anchor = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        sel.active = CollabText::Crdt::Anchor(1, 12, CollabText::Crdt::Bias::Right);
        sel.kind   = Selection::Kind::Primary;

        QSignalSpy spy(s, &Session::primarySelectionChanged);
        s->setPrimarySelection(sel);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->primarySelection().anchor.char_value, sel.anchor.char_value);
        QCOMPARE(s->primarySelection().active.char_value, sel.active.char_value);
        delete s;
    }

    void primary_selection_idempotent_set_does_not_emit() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sel;
        sel.anchor = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        sel.active = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        s->setPrimarySelection(sel);

        QSignalSpy spy(s, &Session::primarySelectionChanged);
        s->setPrimarySelection(sel);   // identical assignment
        QCOMPARE(spy.count(), 0);
        delete s;
    }
```

Add to the includes at the top of the test file:

```cpp
#include <QSignalSpy>
#include <crdt/Anchor.h>
```

- [ ] **Step 2: Run, verify fail**

```bash
ctest --test-dir build-dev -R '^tst_foundation_session$' --output-on-failure 2>&1 | tail -10
```

Expected: setter is a no-op stub; both new tests fail.

- [ ] **Step 3: Implement setPrimarySelection**

Edit `libs/markoff-core/src/Session.cpp`. Replace the stub:

```cpp
void Session::setPrimarySelection(const Selection &sel)
{
    const Selection &cur = d->primary;
    if (cur.anchor.replica_id == sel.anchor.replica_id
        && cur.anchor.char_value == sel.anchor.char_value
        && cur.anchor.bias       == sel.anchor.bias
        && cur.active.replica_id == sel.active.replica_id
        && cur.active.char_value == sel.active.char_value
        && cur.active.bias       == sel.active.bias
        && cur.kind              == sel.kind)
    {
        return;
    }
    d->primary = sel;
    Q_EMIT primarySelectionChanged(d->primary);
}
```

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev --target tst_foundation_session -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_session$' --output-on-failure
```

Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Session.cpp \
        libs/markoff-core/tests/tst_foundation_session.cpp
git commit -m "feat(foundation): Session::setPrimarySelection with idempotence guard"
```

---

### Task 20: Session secondary selections

Adds `setSecondarySelections / addSecondarySelection / clearSecondarySelectionsOfKind` plus `secondarySelectionsChanged` signal.

**Files:**
- Modify: `libs/markoff-core/src/Session.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_session.cpp`

- [ ] **Step 1: Write the failing test**

Append to the test class:

```cpp
    void set_secondary_selections_replaces_list() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection a; a.kind = Selection::Kind::Secondary;
        a.anchor = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        a.active = CollabText::Crdt::Anchor(1, 11, CollabText::Crdt::Bias::Right);
        Selection b; b.kind = Selection::Kind::SearchMatch;
        b.anchor = CollabText::Crdt::Anchor(1, 20, CollabText::Crdt::Bias::Left);
        b.active = CollabText::Crdt::Anchor(1, 23, CollabText::Crdt::Bias::Right);

        QSignalSpy spy(s, &Session::secondarySelectionsChanged);
        s->setSecondarySelections({ a, b });
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->secondarySelections().size(), 2);
        delete s;
    }

    void add_secondary_selection_appends() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection a; a.kind = Selection::Kind::Secondary;
        a.anchor = CollabText::Crdt::Anchor(1, 10, CollabText::Crdt::Bias::Left);
        a.active = CollabText::Crdt::Anchor(1, 11, CollabText::Crdt::Bias::Right);
        s->addSecondarySelection(a);
        QCOMPARE(s->secondarySelections().size(), 1);
        s->addSecondarySelection(a);
        QCOMPARE(s->secondarySelections().size(), 2);
        delete s;
    }

    void clear_of_kind_preserves_other_kinds() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        Selection sec; sec.kind = Selection::Kind::Secondary;
        sec.anchor = CollabText::Crdt::Anchor(1, 1, CollabText::Crdt::Bias::Left);
        Selection sm;  sm.kind  = Selection::Kind::SearchMatch;
        sm.anchor  = CollabText::Crdt::Anchor(1, 5, CollabText::Crdt::Bias::Left);
        Selection pres; pres.kind = Selection::Kind::Presence;
        pres.anchor = CollabText::Crdt::Anchor(1, 9, CollabText::Crdt::Bias::Left);
        s->setSecondarySelections({ sec, sm, pres });

        s->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
        QCOMPARE(s->secondarySelections().size(), 2);
        for (const Selection &x : s->secondarySelections())
            QVERIFY(x.kind != Selection::Kind::SearchMatch);
        delete s;
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement the setters**

Edit `libs/markoff-core/src/Session.cpp`:

```cpp
void Session::setSecondarySelections(QList<Selection> sels)
{
    d->secondaries = std::move(sels);
    Q_EMIT secondarySelectionsChanged();
}

void Session::addSecondarySelection(Selection sel)
{
    d->secondaries.append(std::move(sel));
    Q_EMIT secondarySelectionsChanged();
}

void Session::clearSecondarySelectionsOfKind(Selection::Kind kind)
{
    QList<Selection> kept;
    kept.reserve(d->secondaries.size());
    for (const Selection &s : d->secondaries) {
        if (s.kind != kind) kept << s;
    }
    if (kept.size() == d->secondaries.size()) return;
    d->secondaries = std::move(kept);
    Q_EMIT secondarySelectionsChanged();
}
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Session.cpp \
        libs/markoff-core/tests/tst_foundation_session.cpp
git commit -m "feat(foundation): Session secondary selections with kinded clear"
```

---

### Task 21: Session scroll + folds

Adds `setTopVisible / topVisibleAnchor / topVisibleFraction / scrollChanged` and `setFoldedRegions / toggleFold / foldedRegionsChanged`. Implements spec §7.2 scroll and fold APIs.

**Files:**
- Modify: `libs/markoff-core/src/Session.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_session.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void set_top_visible_emits_scroll_changed() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        QSignalSpy spy(s, &Session::scrollChanged);
        const auto a = CollabText::Crdt::Anchor(1, 100, CollabText::Crdt::Bias::Left);
        s->setTopVisible(a, 0.25);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->topVisibleAnchor().char_value, quint32(100));
        QCOMPARE(s->topVisibleFraction(), 0.25);
        delete s;
    }

    void set_folded_regions_replaces_list() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        FoldRef f; f.kind = FoldRef::Kind::Heading;
        f.start = CollabText::Crdt::Anchor(1, 50, CollabText::Crdt::Bias::Left);
        f.headingPath << QStringLiteral("Intro");
        f.headingLevel = 1;

        QSignalSpy spy(s, &Session::foldedRegionsChanged);
        s->setFoldedRegions({ f });
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s->foldedRegions().size(), 1);
        delete s;
    }

    void toggle_fold_adds_then_removes() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{});
        FoldRef f; f.kind = FoldRef::Kind::Heading;
        f.start = CollabText::Crdt::Anchor(1, 50, CollabText::Crdt::Bias::Left);
        f.headingPath << QStringLiteral("Intro");
        f.headingLevel = 1;

        s->toggleFold(f);
        QCOMPARE(s->foldedRegions().size(), 1);
        s->toggleFold(f);
        QCOMPARE(s->foldedRegions().size(), 0);
        delete s;
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement scroll + fold**

```cpp
void Session::setTopVisible(CollabText::Crdt::Anchor a, qreal fraction)
{
    d->topAnchor   = a;
    d->topFraction = fraction;
    Q_EMIT scrollChanged(a, fraction);
}

void Session::setFoldedRegions(QList<FoldRef> folds)
{
    d->folds = std::move(folds);
    Q_EMIT foldedRegionsChanged();
}

void Session::toggleFold(const FoldRef &f)
{
    // Match by start anchor identity (replica + char_value) — ignores
    // ephemeral heading-path drift across parses.
    const auto matches = [&](const FoldRef &x) {
        return x.start.replica_id == f.start.replica_id
            && x.start.char_value == f.start.char_value
            && x.kind             == f.kind;
    };
    const auto it = std::find_if(d->folds.begin(), d->folds.end(), matches);
    if (it == d->folds.end()) {
        d->folds.append(f);
    } else {
        d->folds.erase(it);
    }
    Q_EMIT foldedRegionsChanged();
}
```

Add `#include <algorithm>` to `Session.cpp` if not already present.

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Session.cpp \
        libs/markoff-core/tests/tst_foundation_session.cpp
git commit -m "feat(foundation): Session scroll + folds with toggleFold by anchor identity"
```

---

### Task 22: Session copyStateFrom + JSON roundtrip

`copyStateFrom(other)` copies primary, secondaries, scroll, folds — but NOT identity (id, participantId, etc.). `toJson` / `fromJson` serialize all ephemeral state for host-side persistence.

**Files:**
- Modify: `libs/markoff-core/src/Session.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_session.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void copy_state_from_transfers_ephemeral_state() {
        MarkoffDocument doc(1);
        Session *src = new Session(&doc, SessionParams{
            .participantId = QStringLiteral("alice")});
        Selection p;
        p.anchor = CollabText::Crdt::Anchor(1, 5, CollabText::Crdt::Bias::Left);
        p.active = CollabText::Crdt::Anchor(1, 8, CollabText::Crdt::Bias::Right);
        src->setPrimarySelection(p);
        src->setTopVisible(CollabText::Crdt::Anchor(1, 100,
                           CollabText::Crdt::Bias::Left), 0.5);

        Session *dst = new Session(&doc, SessionParams{
            .participantId = QStringLiteral("bob")});
        dst->copyStateFrom(*src);

        QCOMPARE(dst->primarySelection().anchor.char_value, quint32(5));
        QCOMPARE(dst->topVisibleAnchor().char_value,        quint32(100));
        QCOMPARE(dst->topVisibleFraction(),                 0.5);
        // Identity is NOT copied.
        QCOMPARE(dst->participantId(), QStringLiteral("bob"));
        QVERIFY(dst->id() != src->id());
        delete src; delete dst;
    }

    void session_json_roundtrip() {
        MarkoffDocument doc(1);
        Session *s = new Session(&doc, SessionParams{
            .participantId    = QStringLiteral("alice"),
            .participantLabel = QStringLiteral("Alice"),
            .presenceColor    = QColor(Qt::cyan)});
        Selection p;
        p.anchor = CollabText::Crdt::Anchor(1, 5, CollabText::Crdt::Bias::Left);
        p.active = CollabText::Crdt::Anchor(1, 8, CollabText::Crdt::Bias::Right);
        s->setPrimarySelection(p);

        const QJsonObject json = s->toJson();
        Session *t = new Session(&doc, SessionParams{});
        t->fromJson(json);
        QCOMPARE(t->primarySelection().anchor.char_value, quint32(5));
        QCOMPARE(t->participantId(), QStringLiteral("alice"));
        delete s; delete t;
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement copyStateFrom + toJson + fromJson**

```cpp
void Session::copyStateFrom(const Session &other)
{
    d->primary     = other.d->primary;
    d->secondaries = other.d->secondaries;
    d->topAnchor   = other.d->topAnchor;
    d->topFraction = other.d->topFraction;
    d->folds       = other.d->folds;
    Q_EMIT primarySelectionChanged(d->primary);
    Q_EMIT secondarySelectionsChanged();
    Q_EMIT scrollChanged(d->topAnchor, d->topFraction);
    Q_EMIT foldedRegionsChanged();
}

QJsonObject Session::toJson() const
{
    QJsonObject obj;
    obj.insert("id",               d->id);
    obj.insert("participantId",    d->participantId);
    obj.insert("participantLabel", d->participantLabel);
    obj.insert("presenceColor",    d->presenceColor.name(QColor::HexArgb));
    obj.insert("primary",          d->primary.toJson());

    QJsonArray sec;
    for (const Selection &s : d->secondaries) sec.append(s.toJson());
    obj.insert("secondaries", sec);

    obj.insert("topAnchor",   anchorToJson(d->topAnchor));
    obj.insert("topFraction", d->topFraction);

    QJsonArray folds;
    for (const FoldRef &f : d->folds) folds.append(f.toJson());
    obj.insert("folds", folds);
    return obj;
}

void Session::fromJson(const QJsonObject &obj)
{
    d->id               = obj.value("id").toString();
    d->participantId    = obj.value("participantId").toString();
    d->participantLabel = obj.value("participantLabel").toString();
    d->presenceColor    = QColor(obj.value("presenceColor").toString());
    d->primary          = Selection::fromJson(obj.value("primary").toObject());

    d->secondaries.clear();
    for (const QJsonValue &v : obj.value("secondaries").toArray())
        d->secondaries << Selection::fromJson(v.toObject());

    d->topAnchor   = anchorFromJson(obj.value("topAnchor").toObject());
    d->topFraction = obj.value("topFraction").toDouble();

    d->folds.clear();
    for (const QJsonValue &v : obj.value("folds").toArray())
        d->folds << FoldRef::fromJson(v.toObject());

    Q_EMIT primarySelectionChanged(d->primary);
    Q_EMIT secondarySelectionsChanged();
    Q_EMIT scrollChanged(d->topAnchor, d->topFraction);
    Q_EMIT foldedRegionsChanged();
}
```

Add the includes at the top of `Session.cpp`:

```cpp
#include <QJsonArray>
#include <markoff/core/AnchorJson.h>
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Session.cpp \
        libs/markoff-core/tests/tst_foundation_session.cpp
git commit -m "feat(foundation): Session::copyStateFrom + JSON roundtrip"
```

---

### Task 23: MarkoffDocument session lifecycle

Wires `MarkoffDocument::createSession / destroySession / sessions / sessionForParticipant` through to a `QList<Session*>` owned by `Private`. Emits `sessionCreated` / `sessionDestroyed`. Depends on Task 18 (Session is a complete type).

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h` (already has `QList<Session*>` from Task 9 — verify)
- Modify: `libs/markoff-core/tests/tst_foundation_markoff_document.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tst_foundation_markoff_document.cpp`:

```cpp
#include <markoff/core/Session.h>
// ... inside the test class ...

    void create_session_returns_owned_session() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::sessionCreated);
        Session *s = doc.createSession();
        QVERIFY(s != nullptr);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(doc.sessions().size(), 1);
    }

    void create_two_sessions_with_distinct_participants() {
        MarkoffDocument doc(1);
        SessionParams pa; pa.participantId = QStringLiteral("alice");
        SessionParams pb; pb.participantId = QStringLiteral("bob");
        Session *a = doc.createSession(pa);
        Session *b = doc.createSession(pb);
        QCOMPARE(doc.sessions().size(), 2);
        QCOMPARE(doc.sessionForParticipant("alice"), a);
        QCOMPARE(doc.sessionForParticipant("bob"),   b);
    }

    void destroy_session_removes_from_list() {
        MarkoffDocument doc(1);
        Session *s = doc.createSession();
        QSignalSpy spy(&doc, &MarkoffDocument::sessionDestroyed);
        doc.destroySession(s);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(doc.sessions().size(), 0);
    }
```

- [ ] **Step 2: Run, verify fail.** Expected: createSession returns nullptr (current stub).

- [ ] **Step 3: Implement the lifecycle**

Edit `libs/markoff-core/src/MarkoffDocument.cpp`. Add at the top:

```cpp
#include <markoff/core/Session.h>
```

Replace the stubs:

```cpp
Session *MarkoffDocument::createSession(const SessionParams &params)
{
    auto *s = new Session(this, params);
    d->sessions.append(s);
    Q_EMIT sessionCreated(s);
    return s;
}

void MarkoffDocument::destroySession(Session *s)
{
    if (!s) return;
    if (!d->sessions.removeOne(s)) return;
    Q_EMIT sessionDestroyed(s);
    s->deleteLater();
}

QList<Session *> MarkoffDocument::sessions() const
{
    return d->sessions;
}

Session *MarkoffDocument::sessionForParticipant(const QString &participantId) const
{
    for (Session *s : d->sessions)
        if (s->participantId() == participantId) return s;
    return nullptr;
}
```

- [ ] **Step 4: Build + run**

```bash
cmake --build build-dev --target tst_foundation_markoff_document -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_markoff_document$' --output-on-failure
```

Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/tst_foundation_markoff_document.cpp
git commit -m "feat(foundation): MarkoffDocument session lifecycle (create/destroy/lookup)"
```

---


## Phase 5 — ParsePool integration (Task 24)

Implements spec §6.1 (ParsePool salvage). The existing `markoff-core` ParsePool / ParsePoolWorker is copied verbatim, with the input changed from QString to UTF-8 QByteArray. `MarkoffDocument` schedules a parse on every content mutation (applyLocalEdit / applyRemoteOps / resetContent) and emits `parseUpdated` on completion.

### Task 24: ParsePool + MarkoffDocument parseUpdated wiring

**Files:**
- Create: `libs/markoff-core/src/ParsePool.h`
- Create: `libs/markoff-core/src/ParsePool.cpp`
- Create: `libs/markoff-core/src/ParsePoolWorker.h`
- Create: `libs/markoff-core/src/ParsePoolWorker.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocumentPrivate.h`
- Create: `libs/markoff-core/tests/tst_foundation_parse_pool.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

<!-- AMBIGUITY: spec §6.1 says ParsePool is salvaged from existing markoff-core "verbatim, adjust for UTF-8 input"; the precise debounce interval and worker thread shape are picked up from the existing libs/markoff-core/src/ParsePool.cpp. The test below treats those as black-box behaviors and only asserts: (a) parseUpdated fires after a debounce; (b) parsedDocument() becomes non-null thereafter. -->

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_parse_pool.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffEdit.h>

using namespace Markoff;

class TstFoundationParsePool : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsed_document_initially_null() {
        MarkoffDocument doc(1);
        QVERIFY(doc.parsedDocument() == nullptr);
    }

    void parse_updated_fires_after_local_edit() {
        MarkoffDocument doc(1);
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);

        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "# Hello\n\nWorld\n";
        ed << i;
        doc.applyLocalEdit(ed);

        QVERIFY(spy.wait(2000));
        QVERIFY(doc.parsedDocument() != nullptr);
    }
};

QTEST_MAIN(TstFoundationParsePool)
#include "tst_foundation_parse_pool.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_parse_pool tst_foundation_parse_pool.cpp)
add_test(NAME tst_foundation_parse_pool COMMAND tst_foundation_parse_pool)
target_link_libraries(tst_foundation_parse_pool PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_parse_pool PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Verify the test fails**

```bash
cmake --build build-dev --target tst_foundation_parse_pool -j 2>&1 | tail -5
ctest --test-dir build-dev -R '^tst_foundation_parse_pool$' --output-on-failure 2>&1 | tail -10
```

Expected: parseUpdated never fires (current stub returns nullptr).

- [ ] **Step 4: Salvage ParsePool/Worker from markoff-core**

Copy the existing `libs/markoff-core/src/ParsePool.{h,cpp}` and `libs/markoff-core/src/ParsePoolWorker.{h,cpp}` into `libs/markoff-core/src/`. Adjust:

1. Namespace: change `Markoff::Core` → `Markoff` (or a `Markoff::Core::Detail` sub-namespace).
2. Input: change the worker's input from `QString` to `QByteArray` (UTF-8).
3. The output type is `MarkoffParser::Document *` (heap-owned by the pool); no other code touches it.
4. Strip any `markoff-core`-specific includes; replace with foundation includes.

Public API of the salvaged `ParsePool` (header-only as much as possible):

```cpp
// libs/markoff-core/src/ParsePool.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>

#include <memory>

namespace Markoff { class Document; }  // markoff-parser

namespace Markoff::Core {

class ParsePool : public QObject {
    Q_OBJECT
public:
    explicit ParsePool(QObject *parent = nullptr);
    ~ParsePool() override;

    void schedule(QByteArray utf8);   ///< coalesces; runs on worker thread
    bool isPending() const;

Q_SIGNALS:
    void parseReady(const Markoff::Document *parsed);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff::Core
```

- [ ] **Step 5: Wire ParsePool into MarkoffDocument::Private**

Edit `libs/markoff-core/src/MarkoffDocumentPrivate.h`:

```cpp
#include "ParsePool.h"

struct MarkoffDocument::Private {
    explicit Private(uint16_t replicaId)
        : buffer(replicaId)
        , replicaId(replicaId)
    {}

    CollabText::Crdt::Buffer    buffer;
    quint16                     replicaId;
    int                         coalescingIdleMs = 250;
    QList<Session *>            sessions;
    Markoff::Core::ParsePool parsePool;
    const Markoff::Document    *latestParse = nullptr;
};
```

- [ ] **Step 6: Schedule parses + replace stubs in MarkoffDocument.cpp**

In the constructor, connect `parsePool.parseReady` → cache + emit:

```cpp
MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId))
{
    QObject::connect(&d->parsePool, &Markoff::Core::ParsePool::parseReady,
                     this, [this](const Markoff::Document *p) {
                         d->latestParse = p;
                         Q_EMIT parseUpdated(p);
                     });
}
```

Replace the two stubs:

```cpp
const Markoff::Document *MarkoffDocument::parsedDocument() const
{
    return d->latestParse;
}

bool MarkoffDocument::parseIsPending() const
{
    return d->parsePool.isPending();
}
```

In `applyLocalEdit`, `applyRemoteOps`, and `resetContent` — at the end, before / after `Q_EMIT contentsChanged(...)` — add:

```cpp
d->parsePool.schedule(toMarkdownUtf8());
```

(Place it after the emit so views see new text first.)

- [ ] **Step 7: Add to library target**

Add to `libs/markoff-core/CMakeLists.txt`'s `add_library` block:

```cmake
src/ParsePool.h
src/ParsePool.cpp
src/ParsePoolWorker.h
src/ParsePoolWorker.cpp
```

- [ ] **Step 8: Build + run**

```bash
cmake --build build-dev --target tst_foundation_parse_pool -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_parse_pool$' --output-on-failure
```

Expected: pass.

- [ ] **Step 9: Commit**

```bash
git add libs/markoff-core/src/ParsePool.h \
        libs/markoff-core/src/ParsePool.cpp \
        libs/markoff-core/src/ParsePoolWorker.h \
        libs/markoff-core/src/ParsePoolWorker.cpp \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/MarkoffDocumentPrivate.h \
        libs/markoff-core/tests/tst_foundation_parse_pool.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): salvage ParsePool + wire parseUpdated into MarkoffDocument"
```

---

## Phase 6 — Theme (Tasks 25–28)

Implements spec §7.5. `Theme` is a Q_GADGET value type with semantic slots, font roles, and convenience mapping from `CodeTokenKind` to color slots. The full enum list is fixed by the spec.

### Task 25: Theme slot enum + color/setColor

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Theme.h`
- Create: `libs/markoff-core/src/Theme.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_theme.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`
- Modify: `libs/markoff-core/tests/CMakeLists.txt`

> Note: `colorForCodeToken(CodeTokenKind)` referenced in spec §7.5 needs `CodeTokenKind` from §7.9. To avoid a circular dependency, this task forward-declares `enum class CodeTokenKind` (declared as a complete enum in Task 43). The `colorForCodeToken` body is added in Task 28 once the enum is complete.

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_theme.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QColor>

#include <markoff/core/Theme.h>

using namespace Markoff;

class TstFoundationTheme : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void color_set_round_trips() {
        Theme t;
        t.setColor(Theme::Slot::Heading1, QColor(Qt::red));
        QCOMPARE(t.color(Theme::Slot::Heading1).name(), QColor(Qt::red).name());
    }

    void unset_color_falls_back_to_text_default() {
        Theme t;
        t.setColor(Theme::Slot::TextDefault, QColor("#101010"));
        // An unset slot returns a sentinel-equivalent (TextDefault) color
        // rather than an invalid QColor. View code can rely on this.
        QVERIFY(t.color(Theme::Slot::Heading6).isValid());
    }
};

QTEST_APPLESS_MAIN(TstFoundationTheme)
#include "tst_foundation_theme.moc"
```

- [ ] **Step 2: Add test target**

Append to `libs/markoff-core/tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_theme tst_foundation_theme.cpp)
add_test(NAME tst_foundation_theme COMMAND tst_foundation_theme)
target_link_libraries(tst_foundation_theme PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_theme PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Verify fail**

```bash
cmake --build build-dev --target tst_foundation_theme -j 2>&1 | tail -5
```

Expected: header missing.

- [ ] **Step 4: Create Theme header**

Create `libs/markoff-core/include/markoff-foundation/Theme.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QtCore/qmetatype.h>

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

enum class CodeTokenKind;  // forward (defined in CodeTokenKind.h, Task 43)

class MARKOFF_FOUNDATION_EXPORT Theme {
    Q_GADGET
public:
    enum class Slot {
        TextDefault,
        Heading1, Heading2, Heading3, Heading4, Heading5, Heading6,
        InlineCode, CodeBlock,
        Link, WikiLink, Tag, Math,
        Quote,
        BoldEmphasis, ItalicEmphasis, StrikeEmphasis,
        Highlight,
        SelectionBackground,
        CursorPrimary, CursorSecondary, CursorPresence,
        SearchMatchBackground, SearchActiveMatchBackground,
        EditorBackground, GutterBackground,
        CodeBlockBackground, QuoteBackground,
        CalloutNote, CalloutWarning, CalloutTip,
        CalloutImportant, CalloutCaution,
        CodeKeyword, CodeControlFlow, CodeBuiltin,
        CodeType, CodeFunction, CodeVariable, CodeConstant,
        CodeOperator, CodePunctuation,
        CodeString, CodeNumber, CodeBoolean,
        CodeComment, CodeDocumentation,
        CodePreprocessor, CodeAnnotation,
        FoldArrow, ScrollbarThumb,
    };
    Q_ENUM(Slot)

    enum class FontRole { Body, Monospace, Heading };
    Q_ENUM(FontRole)

    QColor color(Slot) const;
    void   setColor(Slot, QColor);

    QFont  font(FontRole) const;
    void   setFont(FontRole, QFont);

    bool   isBold(Slot) const;
    void   setBold(Slot, bool);
    bool   isItalic(Slot) const;
    void   setItalic(Slot, bool);
    qreal  fontSizeMultiplier(Slot) const;
    void   setFontSizeMultiplier(Slot, qreal);

    QColor colorForCodeToken(CodeTokenKind) const;

    static Theme defaultLight();
    static Theme defaultDark();

    QJsonObject toJson() const;
    static Theme fromJson(const QJsonObject &);

private:
    QHash<int, QColor>    m_colors;
    QHash<int, QFont>     m_fonts;
    QHash<int, bool>      m_bolds;
    QHash<int, bool>      m_italics;
    QHash<int, qreal>     m_sizeMul;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::Theme)
```

- [ ] **Step 5: Create Theme.cpp (this task: color/setColor only)**

Create `libs/markoff-core/src/Theme.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Theme.h>

namespace Markoff {

QColor Theme::color(Slot s) const
{
    const auto it = m_colors.constFind(static_cast<int>(s));
    if (it != m_colors.constEnd()) return it.value();
    // Fallback to TextDefault.
    const auto td = m_colors.constFind(static_cast<int>(Slot::TextDefault));
    if (td != m_colors.constEnd()) return td.value();
    return QColor(Qt::black);
}

void Theme::setColor(Slot s, QColor c) { m_colors[static_cast<int>(s)] = std::move(c); }

// Stubs — filled in subsequent tasks.
QFont Theme::font(FontRole) const { return {}; }
void  Theme::setFont(FontRole, QFont) {}
bool  Theme::isBold(Slot) const { return false; }
void  Theme::setBold(Slot, bool) {}
bool  Theme::isItalic(Slot) const { return false; }
void  Theme::setItalic(Slot, bool) {}
qreal Theme::fontSizeMultiplier(Slot) const { return 1.0; }
void  Theme::setFontSizeMultiplier(Slot, qreal) {}

QColor Theme::colorForCodeToken(CodeTokenKind) const { return color(Slot::CodeBlock); }

Theme Theme::defaultLight() { return Theme{}; }
Theme Theme::defaultDark()  { return Theme{}; }

QJsonObject Theme::toJson() const { return {}; }
Theme Theme::fromJson(const QJsonObject &) { return Theme{}; }

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target**

Add `include/markoff-foundation/Theme.h` and `src/Theme.cpp` to the `add_library` block.

- [ ] **Step 7: Build + run**

```bash
cmake --build build-dev --target tst_foundation_theme -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_theme$' --output-on-failure
```

Expected: pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Theme.h \
        libs/markoff-core/src/Theme.cpp \
        libs/markoff-core/tests/tst_foundation_theme.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Theme value type with Slot enum + color/setColor"
```

---

### Task 26: Theme fonts + bold/italic/sizeMultiplier

Replace the stubs from Task 25 with real implementations and add tests covering font/bold/italic/sizeMul.

- [ ] **Step 1: Write the failing test**

Append to `tst_foundation_theme.cpp`:

```cpp
    void font_set_round_trips() {
        Theme t;
        QFont f("Monaco", 14);
        t.setFont(Theme::FontRole::Monospace, f);
        QCOMPARE(t.font(Theme::FontRole::Monospace).family(), QStringLiteral("Monaco"));
    }

    void bold_italic_size_round_trip() {
        Theme t;
        t.setBold(Theme::Slot::Heading1, true);
        t.setItalic(Theme::Slot::Quote, true);
        t.setFontSizeMultiplier(Theme::Slot::Heading1, 2.0);
        QVERIFY(t.isBold(Theme::Slot::Heading1));
        QVERIFY(t.isItalic(Theme::Slot::Quote));
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::Heading1), 2.0);
        // Defaults.
        QVERIFY(!t.isBold(Theme::Slot::TextDefault));
        QCOMPARE(t.fontSizeMultiplier(Theme::Slot::TextDefault), 1.0);
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement font/bold/italic/sizeMul**

Replace the stubs in `Theme.cpp`:

```cpp
QFont Theme::font(FontRole r) const
{
    const auto it = m_fonts.constFind(static_cast<int>(r));
    return it != m_fonts.constEnd() ? it.value() : QFont{};
}
void Theme::setFont(FontRole r, QFont f) { m_fonts[static_cast<int>(r)] = std::move(f); }

bool Theme::isBold(Slot s) const { return m_bolds.value(static_cast<int>(s), false); }
void Theme::setBold(Slot s, bool b) { m_bolds[static_cast<int>(s)] = b; }

bool Theme::isItalic(Slot s) const { return m_italics.value(static_cast<int>(s), false); }
void Theme::setItalic(Slot s, bool b) { m_italics[static_cast<int>(s)] = b; }

qreal Theme::fontSizeMultiplier(Slot s) const
{
    return m_sizeMul.value(static_cast<int>(s), 1.0);
}
void Theme::setFontSizeMultiplier(Slot s, qreal m)
{
    m_sizeMul[static_cast<int>(s)] = m;
}
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Theme.cpp \
        libs/markoff-core/tests/tst_foundation_theme.cpp
git commit -m "feat(foundation): Theme fonts + bold/italic/sizeMul"
```

---

### Task 27: Theme defaultLight / defaultDark + JSON roundtrip

`defaultLight()` / `defaultDark()` produce reasonable defaults for every Slot. JSON serialization round-trips colors, fonts, bold/italic flags, and size multipliers.

- [ ] **Step 1: Write the failing test**

```cpp
    void default_light_has_dark_text_on_light_background() {
        const Theme t = Theme::defaultLight();
        const QColor bg = t.color(Theme::Slot::EditorBackground);
        const QColor fg = t.color(Theme::Slot::TextDefault);
        QVERIFY(bg.lightness() > fg.lightness());
    }

    void default_dark_has_light_text_on_dark_background() {
        const Theme t = Theme::defaultDark();
        const QColor bg = t.color(Theme::Slot::EditorBackground);
        const QColor fg = t.color(Theme::Slot::TextDefault);
        QVERIFY(bg.lightness() < fg.lightness());
    }

    void theme_json_roundtrip() {
        Theme a = Theme::defaultLight();
        a.setColor(Theme::Slot::Heading1, QColor(0xff, 0x00, 0x00));
        a.setBold(Theme::Slot::Heading1, true);
        a.setFontSizeMultiplier(Theme::Slot::Heading1, 1.5);

        const QJsonObject json = a.toJson();
        const Theme b = Theme::fromJson(json);
        QCOMPARE(b.color(Theme::Slot::Heading1).name(),
                 a.color(Theme::Slot::Heading1).name());
        QVERIFY(b.isBold(Theme::Slot::Heading1));
        QCOMPARE(b.fontSizeMultiplier(Theme::Slot::Heading1), 1.5);
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement defaults + JSON**

In `Theme.cpp`:

```cpp
Theme Theme::defaultLight()
{
    Theme t;
    t.setColor(Slot::EditorBackground, QColor("#ffffff"));
    t.setColor(Slot::TextDefault,      QColor("#222222"));
    t.setColor(Slot::Heading1,         QColor("#1a1a1a"));
    t.setColor(Slot::Heading2,         QColor("#1f1f1f"));
    t.setColor(Slot::Heading3,         QColor("#262626"));
    t.setColor(Slot::Heading4,         QColor("#333333"));
    t.setColor(Slot::Heading5,         QColor("#404040"));
    t.setColor(Slot::Heading6,         QColor("#4d4d4d"));
    t.setColor(Slot::Link,             QColor("#0066cc"));
    t.setColor(Slot::WikiLink,         QColor("#5050cc"));
    t.setColor(Slot::Tag,              QColor("#a04080"));
    t.setColor(Slot::Quote,            QColor("#666666"));
    t.setColor(Slot::InlineCode,       QColor("#882020"));
    t.setColor(Slot::CodeBlock,        QColor("#222222"));
    t.setColor(Slot::CodeBlockBackground, QColor("#f4f4f4"));
    t.setColor(Slot::SelectionBackground, QColor("#b0d0ff"));
    t.setColor(Slot::SearchMatchBackground, QColor("#ffe080"));
    t.setColor(Slot::SearchActiveMatchBackground, QColor("#ffb050"));
    t.setBold(Slot::Heading1, true);
    t.setBold(Slot::Heading2, true);
    t.setBold(Slot::BoldEmphasis, true);
    t.setItalic(Slot::ItalicEmphasis, true);
    t.setFontSizeMultiplier(Slot::Heading1, 1.8);
    t.setFontSizeMultiplier(Slot::Heading2, 1.5);
    t.setFontSizeMultiplier(Slot::Heading3, 1.3);
    t.setFont(FontRole::Body, QFont("sans-serif", 11));
    t.setFont(FontRole::Monospace, QFont("monospace", 11));
    t.setFont(FontRole::Heading, QFont("sans-serif", 11));
    return t;
}

Theme Theme::defaultDark()
{
    Theme t = defaultLight();
    t.setColor(Slot::EditorBackground, QColor("#1e1e1e"));
    t.setColor(Slot::TextDefault,      QColor("#e0e0e0"));
    t.setColor(Slot::Heading1,         QColor("#ffffff"));
    t.setColor(Slot::Heading2,         QColor("#f0f0f0"));
    t.setColor(Slot::Heading3,         QColor("#dcdcdc"));
    t.setColor(Slot::Quote,            QColor("#aaaaaa"));
    t.setColor(Slot::CodeBlockBackground, QColor("#2d2d2d"));
    t.setColor(Slot::SelectionBackground, QColor("#264070"));
    return t;
}

QJsonObject Theme::toJson() const
{
    QJsonObject obj;
    QJsonObject colors;
    for (auto it = m_colors.constBegin(); it != m_colors.constEnd(); ++it)
        colors.insert(QString::number(it.key()), it.value().name(QColor::HexArgb));
    obj.insert("colors", colors);

    QJsonObject bolds;
    for (auto it = m_bolds.constBegin(); it != m_bolds.constEnd(); ++it)
        bolds.insert(QString::number(it.key()), it.value());
    obj.insert("bolds", bolds);

    QJsonObject italics;
    for (auto it = m_italics.constBegin(); it != m_italics.constEnd(); ++it)
        italics.insert(QString::number(it.key()), it.value());
    obj.insert("italics", italics);

    QJsonObject sizes;
    for (auto it = m_sizeMul.constBegin(); it != m_sizeMul.constEnd(); ++it)
        sizes.insert(QString::number(it.key()), it.value());
    obj.insert("sizeMul", sizes);

    QJsonObject fonts;
    for (auto it = m_fonts.constBegin(); it != m_fonts.constEnd(); ++it)
        fonts.insert(QString::number(it.key()), it.value().toString());
    obj.insert("fonts", fonts);

    return obj;
}

Theme Theme::fromJson(const QJsonObject &obj)
{
    Theme t;
    const QJsonObject colors = obj.value("colors").toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it)
        t.m_colors[it.key().toInt()] = QColor(it.value().toString());

    const QJsonObject bolds = obj.value("bolds").toObject();
    for (auto it = bolds.begin(); it != bolds.end(); ++it)
        t.m_bolds[it.key().toInt()] = it.value().toBool();

    const QJsonObject italics = obj.value("italics").toObject();
    for (auto it = italics.begin(); it != italics.end(); ++it)
        t.m_italics[it.key().toInt()] = it.value().toBool();

    const QJsonObject sizes = obj.value("sizeMul").toObject();
    for (auto it = sizes.begin(); it != sizes.end(); ++it)
        t.m_sizeMul[it.key().toInt()] = it.value().toDouble();

    const QJsonObject fonts = obj.value("fonts").toObject();
    for (auto it = fonts.begin(); it != fonts.end(); ++it) {
        QFont f; f.fromString(it.value().toString());
        t.m_fonts[it.key().toInt()] = f;
    }
    return t;
}
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Theme.cpp \
        libs/markoff-core/tests/tst_foundation_theme.cpp
git commit -m "feat(foundation): Theme defaults (light/dark) + JSON roundtrip"
```

---

### Task 28: Theme code-token color mapping

Implements `colorForCodeToken(CodeTokenKind)` — depends on Task 43 having defined the `CodeTokenKind` enum. **Reorder note:** if Task 43 has not landed yet at this point, defer this task until after Phase 11; the `colorForCodeToken` body can stay returning `color(Slot::CodeBlock)` until then. This block assumes Task 43's `CodeTokenKind` is available; otherwise complete Task 43 first.

**Files:**
- Modify: `libs/markoff-core/src/Theme.cpp` (add `#include <markoff/core/CodeTokenKind.h>` and replace the stub body)
- Modify: `libs/markoff-core/tests/tst_foundation_theme.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <markoff/core/CodeTokenKind.h>
// ... inside the test class ...

    void colorForCodeToken_maps_to_code_slots() {
        Theme t;
        t.setColor(Theme::Slot::CodeKeyword, QColor("#ff0000"));
        t.setColor(Theme::Slot::CodeString,  QColor("#00ff00"));
        QCOMPARE(t.colorForCodeToken(CodeTokenKind::Keyword).name(),
                 QColor("#ff0000").name());
        QCOMPARE(t.colorForCodeToken(CodeTokenKind::String).name(),
                 QColor("#00ff00").name());
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement the mapping**

Replace the stub body in `Theme.cpp`:

```cpp
QColor Theme::colorForCodeToken(CodeTokenKind k) const
{
    using K = CodeTokenKind;
    switch (k) {
    case K::Keyword:        return color(Slot::CodeKeyword);
    case K::ControlFlow:    return color(Slot::CodeControlFlow);
    case K::Builtin:        return color(Slot::CodeBuiltin);
    case K::Type:           return color(Slot::CodeType);
    case K::Function:       return color(Slot::CodeFunction);
    case K::Variable:       return color(Slot::CodeVariable);
    case K::Constant:       return color(Slot::CodeConstant);
    case K::Operator:       return color(Slot::CodeOperator);
    case K::Punctuation:    return color(Slot::CodePunctuation);
    case K::String:         return color(Slot::CodeString);
    case K::Number:         return color(Slot::CodeNumber);
    case K::Boolean:        return color(Slot::CodeBoolean);
    case K::Comment:        return color(Slot::CodeComment);
    case K::Documentation:  return color(Slot::CodeDocumentation);
    case K::Preprocessor:   return color(Slot::CodePreprocessor);
    case K::Annotation:     return color(Slot::CodeAnnotation);
    case K::Default:        break;
    }
    return color(Slot::CodeBlock);
}
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Theme.cpp \
        libs/markoff-core/tests/tst_foundation_theme.cpp
git commit -m "feat(foundation): Theme::colorForCodeToken slot mapping"
```

---

## Phase 7 — LinkService (Tasks 29–30)

Implements spec §7.6.

### Task 29: LinkKind + LinkActivation + abstract LinkService

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/LinkKind.h`
- Create: `libs/markoff-core/include/markoff-foundation/LinkActivation.h`
- Create: `libs/markoff-core/include/markoff-foundation/LinkService.h`
- Create: `libs/markoff-core/src/LinkService.cpp`
- Modify: `libs/markoff-core/CMakeLists.txt`

(No test target for the abstract base alone; tested transitively via `DefaultLinkService` in Task 30.)

- [ ] **Step 1: Create LinkKind.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtCore/qmetatype.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

enum class LinkKind {
    Unknown,
    External,    ///< http://, https://, mailto:, etc.
    File,        ///< local file path or file://
    WikiLink,    ///< [[note title]] or [[note#anchor]]
    Tag,         ///< #tag
    Anchor,      ///< in-document #heading-id
};

}  // namespace Markoff
```

- [ ] **Step 2: Create LinkActivation.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUrl>

#include <markoff/core/LinkKind.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT LinkActivation {
    QString  rawText;
    QUrl     resolvedTarget;
    LinkKind kind = LinkKind::Unknown;
    QString  anchorHint;
    QString  fromContext;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::LinkActivation)
```

- [ ] **Step 3: Create LinkService.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QUrl>

#include <markoff/core/LinkActivation.h>
#include <markoff/core/LinkKind.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT LinkService : public QObject {
    Q_OBJECT
public:
    explicit LinkService(QObject *parent = nullptr);
    ~LinkService() override;

    virtual LinkKind classify(const QString &linkText) const = 0;
    virtual QUrl resolve(const QString &linkText,
                         const QString &fromContext = {}) const = 0;
    virtual void activate(const LinkActivation &);
    virtual void notifyHover(const LinkActivation &, const QPoint &globalPos);
    virtual void notifyHoverLeft(const QString &linkText);

Q_SIGNALS:
    void linkActivated(const Markoff::LinkActivation &);
    void linkHovered(const Markoff::LinkActivation &, const QPoint &globalPos);
    void linkHoverLeft(const QString &linkText);
};

}  // namespace Markoff
```

- [ ] **Step 4: Create LinkService.cpp (default-impl bodies)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/LinkService.h>

namespace Markoff {

LinkService::LinkService(QObject *parent) : QObject(parent) {}
LinkService::~LinkService() = default;

void LinkService::activate(const LinkActivation &a)
{
    Q_EMIT linkActivated(a);
}

void LinkService::notifyHover(const LinkActivation &a, const QPoint &p)
{
    Q_EMIT linkHovered(a, p);
}

void LinkService::notifyHoverLeft(const QString &t)
{
    Q_EMIT linkHoverLeft(t);
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target**

Add the three new headers + `src/LinkService.cpp` to `add_library`.

- [ ] **Step 6: Build**

```bash
cmake --build build-dev --target markoff_core -j 2>&1 | tail -3
```

Expected: builds clean.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/LinkKind.h \
        libs/markoff-core/include/markoff-foundation/LinkActivation.h \
        libs/markoff-core/include/markoff-foundation/LinkService.h \
        libs/markoff-core/src/LinkService.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): LinkKind + LinkActivation + abstract LinkService"
```

---

### Task 30: DefaultLinkService implementation

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/DefaultLinkService.h`
- Create: `libs/markoff-core/src/DefaultLinkService.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_link_service.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_link_service.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/DefaultLinkService.h>

using namespace Markoff;

class TstFoundationLinkService : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void classify_external_https() {
        DefaultLinkService s;
        QCOMPARE(s.classify("https://example.com"), LinkKind::External);
    }

    void classify_external_http() {
        DefaultLinkService s;
        QCOMPARE(s.classify("http://example.com"), LinkKind::External);
    }

    void classify_external_mailto() {
        DefaultLinkService s;
        QCOMPARE(s.classify("mailto:foo@example.com"), LinkKind::External);
    }

    void classify_unknown_for_unresolved_text() {
        DefaultLinkService s;
        QCOMPARE(s.classify("note title"), LinkKind::Unknown);
    }

    void resolve_returns_literal_qurl() {
        DefaultLinkService s;
        QCOMPARE(s.resolve("https://example.com").toString(),
                 QStringLiteral("https://example.com"));
    }

    void activate_emits_link_activated() {
        DefaultLinkService s;
        QSignalSpy spy(&s, &LinkService::linkActivated);
        LinkActivation a;
        a.rawText = "https://x";
        a.resolvedTarget = QUrl("https://x");
        a.kind = LinkKind::External;
        s.activate(a);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_APPLESS_MAIN(TstFoundationLinkService)
#include "tst_foundation_link_service.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_link_service tst_foundation_link_service.cpp)
add_test(NAME tst_foundation_link_service COMMAND tst_foundation_link_service)
target_link_libraries(tst_foundation_link_service PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_link_service PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create DefaultLinkService.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/LinkService.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT DefaultLinkService : public LinkService {
    Q_OBJECT
public:
    explicit DefaultLinkService(QObject *parent = nullptr);
    LinkKind classify(const QString &linkText) const override;
    QUrl resolve(const QString &linkText, const QString &fromContext = {}) const override;
};

}  // namespace Markoff
```

- [ ] **Step 4: Create DefaultLinkService.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/DefaultLinkService.h>

namespace Markoff {

DefaultLinkService::DefaultLinkService(QObject *parent) : LinkService(parent) {}

LinkKind DefaultLinkService::classify(const QString &t) const
{
    if (t.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))   return LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))  return LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"), Qt::CaseInsensitive))   return LinkKind::External;
    return LinkKind::Unknown;
}

QUrl DefaultLinkService::resolve(const QString &t, const QString &) const
{
    return QUrl(t);
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target**

Add `include/markoff-foundation/DefaultLinkService.h` and `src/DefaultLinkService.cpp` to `add_library`.

- [ ] **Step 6: Build + run**

```bash
cmake --build build-dev --target tst_foundation_link_service -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_link_service$' --output-on-failure
```

Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/DefaultLinkService.h \
        libs/markoff-core/src/DefaultLinkService.cpp \
        libs/markoff-core/tests/tst_foundation_link_service.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): DefaultLinkService classify/resolve"
```

---

## Phase 8 — Commands (Tasks 31–37)

Implements spec §7.7. The pattern: each command is split into a pure `editsForX(...)` function (returns `QList<MarkoffEdit>`, testable without a doc mutation) plus a thin `X(doc, ...)` convenience wrapper that calls `applyLocalEdit`. Headers under `include/markoff-foundation/Cmd/`; impls under `src/Cmd/`.

For all Phase 8 tasks, command lookups use the buffer's UTF-8 byte text. Selection ranges in tests are seeded by first inserting a known string, then computing `oldStart`/`oldEnd` against UTF-8 byte offsets. Where a command needs to "unwrap" pre-existing markup, it uses the parsed AST (`MarkoffDocument::parsedDocument()`) when available; otherwise it falls back to byte-wise pattern matching against the buffer's surrounding text. The latter is sufficient for the unit-test coverage in this plan.

### Helpers shared across Phase 8

- `Cmd/Helpers.cpp` (created in Task 31, extended as needed): `selectionByteRange(doc, sel)` returns `(start, end)` UTF-8 byte offsets; `applyLocalEditOrEmpty(doc, edits)` returns an empty `Operation` when `edits` is empty.

### Task 31: Cmd::Edit (undo / redo wrappers) + helpers

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Cmd/Edit.h`
- Create: `libs/markoff-core/src/Cmd/Edit.cpp`
- Create: `libs/markoff-core/src/Cmd/Helpers.h` (impl-side)
- Create: `libs/markoff-core/src/Cmd/Helpers.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_cmd_edit.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_cmd_edit.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Cmd/Edit.h>
#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;

class TstFoundationCmdEdit : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_wrapper_reverts_last_local_edit() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "ab";
        ed << i;
        doc.applyLocalEdit(ed);
        Cmd::undo(&doc);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }

    void redo_wrapper_reapplies() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "ab";
        ed << i;
        doc.applyLocalEdit(ed);
        Cmd::undo(&doc);
        Cmd::redo(&doc);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("ab"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdEdit)
#include "tst_foundation_cmd_edit.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_cmd_edit tst_foundation_cmd_edit.cpp)
add_test(NAME tst_foundation_cmd_edit COMMAND tst_foundation_cmd_edit)
target_link_libraries(tst_foundation_cmd_edit PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_cmd_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create Cmd/Edit.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
namespace Cmd {

MARKOFF_FOUNDATION_EXPORT void undo(MarkoffDocument *);
MARKOFF_FOUNDATION_EXPORT void redo(MarkoffDocument *);

}}  // namespace Markoff::Cmd
```

- [ ] **Step 4: Create src/Cmd/Edit.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/Edit.h>
#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Cmd {

void undo(MarkoffDocument *d) { if (d) (void)d->undo(); }
void redo(MarkoffDocument *d) { if (d) (void)d->redo(); }

}  // namespace Markoff::Cmd
```

- [ ] **Step 5: Create Helpers.h + Helpers.cpp (skeletons; bodies grow over Phase 8)**

`libs/markoff-core/src/Cmd/Helpers.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <utility>

namespace Markoff {
class MarkoffDocument;
struct Selection;
namespace Cmd::Detail {

/// Returns (startByte, endByte) of the selection in OLD-text coords,
/// resolved against the document's current Buffer. start <= end.
std::pair<quint32, quint32>
    selectionByteRange(const MarkoffDocument *doc, const Selection &sel);

}}  // namespace Markoff::Cmd::Detail
```

`libs/markoff-core/src/Cmd/Helpers.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Helpers.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>

namespace Markoff::Cmd::Detail {

std::pair<quint32, quint32>
selectionByteRange(const MarkoffDocument *doc, const Selection &sel)
{
    if (!doc) return {0u, 0u};
    const auto a = doc->resolveAnchor(sel.anchor);
    const auto b = doc->resolveAnchor(sel.active);
    return a <= b ? std::pair{a, b} : std::pair{b, a};
}

}  // namespace Markoff::Cmd::Detail
```

- [ ] **Step 6: Add to library target**

Add to `add_library`:

```cmake
include/markoff-foundation/Cmd/Edit.h
src/Cmd/Edit.cpp
src/Cmd/Helpers.h
src/Cmd/Helpers.cpp
```

- [ ] **Step 7: Build + run**

```bash
cmake --build build-dev --target tst_foundation_cmd_edit -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_cmd_edit$' --output-on-failure
```

Expected: pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Cmd/Edit.h \
        libs/markoff-core/src/Cmd/Edit.cpp \
        libs/markoff-core/src/Cmd/Helpers.h \
        libs/markoff-core/src/Cmd/Helpers.cpp \
        libs/markoff-core/tests/tst_foundation_cmd_edit.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Cmd::undo/redo wrappers + Cmd::Detail helpers"
```

---

### Task 32: Cmd inline format — toggleBold

Pure `editsForToggleBold(doc, sel)` returns the edits to wrap or unwrap `**...**` around the selection. Convenience `toggleBold(doc, sel)` calls `applyLocalEdit`. Test cases: wrap unstyled, unwrap already-styled, partial overlap, empty selection (no-op).

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Cmd/InlineFormat.h`
- Create: `libs/markoff-core/src/Cmd/InlineFormat.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_cmd_inline_format.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_cmd_inline_format.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Cmd/InlineFormat.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

namespace {
Selection rangeSel(const MarkoffDocument *doc, quint32 start, quint32 end) {
    Selection s;
    s.anchor = doc->anchorAt(start, Bias::Left);
    s.active = doc->anchorAt(end,   Bias::Right);
    s.kind = Selection::Kind::Primary;
    return s;
}

void seed(MarkoffDocument &doc, const QByteArray &text) {
    QList<MarkoffEdit> ed;
    MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = text;
    ed << i;
    doc.applyLocalEdit(ed);
}
}

class TstFoundationCmdInlineFormat : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void toggle_bold_wraps_unstyled() {
        MarkoffDocument doc(1);
        seed(doc, "hello world");
        const Selection sel = rangeSel(&doc, 0, 5);   // "hello"
        const auto edits = Cmd::editsForToggleBold(&doc, sel);
        QVERIFY(!edits.isEmpty());
        Cmd::toggleBold(&doc, sel);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("**hello** world"));
    }

    void toggle_bold_unwraps_styled() {
        MarkoffDocument doc(1);
        seed(doc, "**hello** world");
        // Inner range covers "hello"
        const Selection sel = rangeSel(&doc, 2, 7);
        Cmd::toggleBold(&doc, sel);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("hello world"));
    }

    void toggle_bold_empty_selection_noop() {
        MarkoffDocument doc(1);
        seed(doc, "hello");
        const Selection sel = rangeSel(&doc, 3, 3);
        const auto edits = Cmd::editsForToggleBold(&doc, sel);
        QVERIFY(edits.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdInlineFormat)
#include "tst_foundation_cmd_inline_format.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_cmd_inline_format tst_foundation_cmd_inline_format.cpp)
add_test(NAME tst_foundation_cmd_inline_format COMMAND tst_foundation_cmd_inline_format)
target_link_libraries(tst_foundation_cmd_inline_format PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_cmd_inline_format PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run, verify fail (header missing).**

- [ ] **Step 3: Create Cmd/InlineFormat.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <crdt/Operations.h>

#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
struct Selection;

namespace Cmd {

// Pure functions
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleBold(const MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleItalic(const MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleStrikethrough(const MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleInlineCode(const MarkoffDocument *, const Selection &);

// Convenience wrappers
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleBold(MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleItalic(MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleStrikethrough(MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleInlineCode(MarkoffDocument *, const Selection &);

}}  // namespace Markoff::Cmd
```

- [ ] **Step 4: Create src/Cmd/InlineFormat.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/InlineFormat.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>

#include "Helpers.h"

namespace Markoff::Cmd {

namespace {

/// Common toggler. `delim` is the markdown delimiter (e.g. "**", "*", "~~", "`").
QList<MarkoffEdit> toggleDelim(const MarkoffDocument *doc, const Selection &sel,
                                const QByteArray &delim)
{
    if (!doc || sel.isEmpty()) return {};
    const auto [start, end] = Detail::selectionByteRange(doc, sel);
    if (start == end) return {};

    const QByteArray buf = doc->toMarkdownUtf8();
    const int dlen = delim.size();
    const bool wrapped =
        start >= static_cast<quint32>(dlen)
        && end + dlen <= static_cast<quint32>(buf.size())
        && buf.mid(static_cast<int>(start) - dlen, dlen) == delim
        && buf.mid(static_cast<int>(end), dlen) == delim;

    QList<MarkoffEdit> out;
    if (wrapped) {
        // Strip both delimiters.
        MarkoffEdit r1; r1.oldStart = start - dlen; r1.oldEnd = start;       r1.newText.clear();
        MarkoffEdit r2; r2.oldStart = end;          r2.oldEnd = end + dlen;  r2.newText.clear();
        out << r1 << r2;
    } else {
        // Wrap.
        MarkoffEdit r1; r1.oldStart = start; r1.oldEnd = start; r1.newText = delim;
        MarkoffEdit r2; r2.oldStart = end;   r2.oldEnd = end;   r2.newText = delim;
        out << r1 << r2;
    }
    return out;
}

}  // namespace

QList<MarkoffEdit> editsForToggleBold(const MarkoffDocument *d, const Selection &s)
{ return toggleDelim(d, s, "**"); }
QList<MarkoffEdit> editsForToggleItalic(const MarkoffDocument *d, const Selection &s)
{ return toggleDelim(d, s, "*"); }
QList<MarkoffEdit> editsForToggleStrikethrough(const MarkoffDocument *d, const Selection &s)
{ return toggleDelim(d, s, "~~"); }
QList<MarkoffEdit> editsForToggleInlineCode(const MarkoffDocument *d, const Selection &s)
{ return toggleDelim(d, s, "`"); }

CollabText::Crdt::Operation toggleBold(MarkoffDocument *d, const Selection &s)
{ return d->applyLocalEdit(editsForToggleBold(d, s)); }
CollabText::Crdt::Operation toggleItalic(MarkoffDocument *d, const Selection &s)
{ return d->applyLocalEdit(editsForToggleItalic(d, s)); }
CollabText::Crdt::Operation toggleStrikethrough(MarkoffDocument *d, const Selection &s)
{ return d->applyLocalEdit(editsForToggleStrikethrough(d, s)); }
CollabText::Crdt::Operation toggleInlineCode(MarkoffDocument *d, const Selection &s)
{ return d->applyLocalEdit(editsForToggleInlineCode(d, s)); }

}  // namespace Markoff::Cmd
```

- [ ] **Step 5: Add to library target.** Add `include/markoff-foundation/Cmd/InlineFormat.h` and `src/Cmd/InlineFormat.cpp`.

- [ ] **Step 6: Build + run.** Expected: 3 toggle-bold cases pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Cmd/InlineFormat.h \
        libs/markoff-core/src/Cmd/InlineFormat.cpp \
        libs/markoff-core/tests/tst_foundation_cmd_inline_format.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Cmd::toggleBold + shared inline-format toggler"
```

---

### Task 33: Cmd inline format — italic, strikethrough, inline code tests

The implementations already exist (Task 32 wrote `toggleDelim` parametrically). This task adds explicit test coverage for each delimiter.

**Files:**
- Modify: `libs/markoff-core/tests/tst_foundation_cmd_inline_format.cpp`

- [ ] **Step 1: Write the failing test**

Append to the test class:

```cpp
    void toggle_italic_wraps() {
        MarkoffDocument doc(1);
        seed(doc, "abc");
        Cmd::toggleItalic(&doc, rangeSel(&doc, 0, 3));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("*abc*"));
    }

    void toggle_strikethrough_wraps() {
        MarkoffDocument doc(1);
        seed(doc, "abc");
        Cmd::toggleStrikethrough(&doc, rangeSel(&doc, 0, 3));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("~~abc~~"));
    }

    void toggle_inline_code_wraps_and_unwraps() {
        MarkoffDocument doc(1);
        seed(doc, "abc");
        Cmd::toggleInlineCode(&doc, rangeSel(&doc, 0, 3));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("`abc`"));
        Cmd::toggleInlineCode(&doc, rangeSel(&doc, 1, 4));  // inside backticks
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("abc"));
    }
```

- [ ] **Step 2: Build + run.** Expected: pass.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/tests/tst_foundation_cmd_inline_format.cpp
git commit -m "test(foundation): Cmd italic/strikethrough/inlineCode coverage"
```

---

### Task 34: Cmd::setHeading

`editsForSetHeading(doc, sel, level)` replaces the heading prefix (`#` … `######`) on each affected block with the new level (or strips it for level 0).

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Cmd/Block.h`
- Create: `libs/markoff-core/src/Cmd/Block.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_cmd_block.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_cmd_block.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Cmd/Block.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

namespace {
Selection rangeSel(const MarkoffDocument *doc, quint32 start, quint32 end) {
    Selection s;
    s.anchor = doc->anchorAt(start, Bias::Left);
    s.active = doc->anchorAt(end,   Bias::Right);
    return s;
}
void seed(MarkoffDocument &doc, const QByteArray &text) {
    QList<MarkoffEdit> ed;
    MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = text;
    ed << i;
    doc.applyLocalEdit(ed);
}
}

class TstFoundationCmdBlock : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void paragraph_to_h1() {
        MarkoffDocument doc(1);
        seed(doc, "title\nbody\n");
        Cmd::setHeading(&doc, rangeSel(&doc, 0, 5), 1);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("# title\nbody\n"));
    }

    void h2_to_h3() {
        MarkoffDocument doc(1);
        seed(doc, "## title\n");
        Cmd::setHeading(&doc, rangeSel(&doc, 3, 8), 3);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("### title\n"));
    }

    void h4_to_paragraph() {
        MarkoffDocument doc(1);
        seed(doc, "#### title\n");
        Cmd::setHeading(&doc, rangeSel(&doc, 5, 10), 0);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("title\n"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdBlock)
#include "tst_foundation_cmd_block.moc"
```

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(tst_foundation_cmd_block tst_foundation_cmd_block.cpp)
add_test(NAME tst_foundation_cmd_block COMMAND tst_foundation_cmd_block)
target_link_libraries(tst_foundation_cmd_block PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_cmd_block PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create Cmd/Block.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include <crdt/Operations.h>

#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
struct Selection;

namespace Cmd {

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForSetHeading(const MarkoffDocument *, const Selection &, int level);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    setHeading(MarkoffDocument *, const Selection &, int level);

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForToggleCheckbox(const MarkoffDocument *,
                            const CollabText::Crdt::Anchor &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    toggleCheckbox(MarkoffDocument *, const CollabText::Crdt::Anchor &);

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForBlockQuote(const MarkoffDocument *, const Selection &);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    blockQuote(MarkoffDocument *, const Selection &);

}}  // namespace Markoff::Cmd
```

- [ ] **Step 4: Create src/Cmd/Block.cpp (setHeading body only — checkbox/blockQuote stubs)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/Block.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>

#include "Helpers.h"

namespace Markoff::Cmd {

namespace {
/// Returns the byte offset of the start of the line containing `byte`.
quint32 lineStart(const QByteArray &buf, quint32 byte) {
    if (byte > static_cast<quint32>(buf.size())) byte = buf.size();
    int b = static_cast<int>(byte);
    while (b > 0 && buf.at(b - 1) != '\n') --b;
    return static_cast<quint32>(b);
}
/// Returns the count of leading '#' chars (capped at 6) at offset `start`.
int existingHashes(const QByteArray &buf, quint32 start) {
    int n = 0;
    int b = static_cast<int>(start);
    while (n < 6 && b < buf.size() && buf.at(b) == '#') { ++n; ++b; }
    // A heading marker is `# ` not `#tag`. Require trailing space (or EOL).
    if (n > 0 && b < buf.size() && buf.at(b) != ' ' && buf.at(b) != '\n')
        return 0;
    return n;
}
}

QList<MarkoffEdit> editsForSetHeading(const MarkoffDocument *doc,
                                       const Selection &sel, int level)
{
    if (!doc) return {};
    const auto [start, end] = Detail::selectionByteRange(doc, sel);
    const QByteArray buf = doc->toMarkdownUtf8();
    const quint32 ls = lineStart(buf, start);
    const int existing = existingHashes(buf, ls);

    // Compute current prefix length (existing hashes + trailing space if any).
    int curPrefixLen = existing;
    if (existing > 0 && ls + existing < static_cast<quint32>(buf.size())
        && buf.at(static_cast<int>(ls + existing)) == ' ')
        ++curPrefixLen;

    QByteArray newPrefix;
    if (level >= 1 && level <= 6) {
        newPrefix = QByteArray(level, '#') + " ";
    }

    if (newPrefix.isEmpty() && curPrefixLen == 0) return {};
    if (newPrefix == buf.mid(static_cast<int>(ls), curPrefixLen)) return {};

    MarkoffEdit r;
    r.oldStart = ls;
    r.oldEnd   = ls + static_cast<quint32>(curPrefixLen);
    r.newText  = newPrefix;
    return { r };
}

CollabText::Crdt::Operation setHeading(MarkoffDocument *d, const Selection &s, int level)
{ return d->applyLocalEdit(editsForSetHeading(d, s, level)); }

// toggleCheckbox / blockQuote — stubs filled in Task 35.
QList<MarkoffEdit> editsForToggleCheckbox(const MarkoffDocument *,
                                           const CollabText::Crdt::Anchor &)
{ return {}; }
CollabText::Crdt::Operation toggleCheckbox(MarkoffDocument *d,
                                            const CollabText::Crdt::Anchor &a)
{ return d->applyLocalEdit(editsForToggleCheckbox(d, a)); }
QList<MarkoffEdit> editsForBlockQuote(const MarkoffDocument *, const Selection &)
{ return {}; }
CollabText::Crdt::Operation blockQuote(MarkoffDocument *d, const Selection &s)
{ return d->applyLocalEdit(editsForBlockQuote(d, s)); }

}  // namespace Markoff::Cmd
```

- [ ] **Step 5: Add to library target.** Add `include/markoff-foundation/Cmd/Block.h` and `src/Cmd/Block.cpp`.

- [ ] **Step 6: Build + run.** Expected: 3 setHeading cases pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Cmd/Block.h \
        libs/markoff-core/src/Cmd/Block.cpp \
        libs/markoff-core/tests/tst_foundation_cmd_block.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Cmd::setHeading with line-prefix replacement"
```

---

### Task 35: Cmd::toggleCheckbox + blockQuote

`toggleCheckbox` cycles between `[ ]` ↔ `[x]` ↔ no-checkbox on a list item containing the given anchor. `blockQuote` wraps each affected line with a `> ` prefix (or strips it if all selected lines already have one).

**Files:**
- Modify: `libs/markoff-core/src/Cmd/Block.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_cmd_block.cpp`

- [ ] **Step 1: Write the failing test**

Append to the test class:

```cpp
    void toggle_checkbox_unchecked_to_checked() {
        MarkoffDocument doc(1);
        seed(doc, "- [ ] task\n");
        Cmd::toggleCheckbox(&doc, doc.anchorAt(7, Bias::Left));   // inside "task"
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("- [x] task\n"));
    }

    void toggle_checkbox_checked_to_none() {
        MarkoffDocument doc(1);
        seed(doc, "- [x] task\n");
        Cmd::toggleCheckbox(&doc, doc.anchorAt(7, Bias::Left));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("- task\n"));
    }

    void block_quote_wraps_each_line() {
        MarkoffDocument doc(1);
        seed(doc, "one\ntwo\n");
        Cmd::blockQuote(&doc, rangeSel(&doc, 0, 7));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("> one\n> two\n"));
    }

    void block_quote_unwraps_when_all_lines_quoted() {
        MarkoffDocument doc(1);
        seed(doc, "> one\n> two\n");
        Cmd::blockQuote(&doc, rangeSel(&doc, 0, 11));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("one\ntwo\n"));
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Replace the stubs**

Replace `editsForToggleCheckbox` and `editsForBlockQuote` in `Cmd/Block.cpp`:

```cpp
QList<MarkoffEdit> editsForToggleCheckbox(const MarkoffDocument *doc,
                                           const CollabText::Crdt::Anchor &a)
{
    if (!doc) return {};
    const QByteArray buf = doc->toMarkdownUtf8();
    const quint32 byte = doc->resolveAnchor(a);
    const quint32 ls = lineStart(buf, byte);

    // Look for "- [ ] " or "- [x] " or "- " prefix.
    int b = static_cast<int>(ls);
    if (b + 1 >= buf.size()) return {};
    // Step over leading "- " or "* " or "+ " bullet.
    if (buf.at(b) != '-' && buf.at(b) != '*' && buf.at(b) != '+') return {};
    if (b + 1 >= buf.size() || buf.at(b + 1) != ' ') return {};
    int after = b + 2;

    if (after + 3 < buf.size() && buf.at(after) == '[' && buf.at(after + 2) == ']'
        && buf.at(after + 3) == ' ')
    {
        const char inside = buf.at(after + 1);
        MarkoffEdit r; r.oldStart = static_cast<quint32>(after);
        if (inside == ' ') {
            // [ ] -> [x]
            r.oldEnd = static_cast<quint32>(after + 4);
            r.newText = "[x] ";
        } else {
            // [x] -> none (strip "[x] ")
            r.oldEnd = static_cast<quint32>(after + 4);
            r.newText.clear();
        }
        return { r };
    }
    // No checkbox -> add "[ ] "
    MarkoffEdit r;
    r.oldStart = static_cast<quint32>(after);
    r.oldEnd   = static_cast<quint32>(after);
    r.newText  = "[ ] ";
    return { r };
}

QList<MarkoffEdit> editsForBlockQuote(const MarkoffDocument *doc, const Selection &sel)
{
    if (!doc) return {};
    const auto [start, end] = Detail::selectionByteRange(doc, sel);
    const QByteArray buf = doc->toMarkdownUtf8();

    // Collect line starts in [start, end].
    QList<quint32> lineStarts;
    quint32 ls = lineStart(buf, start);
    while (ls < end || (ls == end && lineStarts.isEmpty())) {
        lineStarts << ls;
        int next = static_cast<int>(ls);
        while (next < buf.size() && buf.at(next) != '\n') ++next;
        if (next >= buf.size()) break;
        ls = static_cast<quint32>(next + 1);
    }

    bool allQuoted = !lineStarts.isEmpty();
    for (quint32 l : lineStarts) {
        if (l + 1 >= static_cast<quint32>(buf.size())
            || buf.at(static_cast<int>(l)) != '>'
            || buf.at(static_cast<int>(l) + 1) != ' ')
        { allQuoted = false; break; }
    }

    QList<MarkoffEdit> out;
    out.reserve(lineStarts.size());
    if (allQuoted) {
        for (quint32 l : lineStarts) {
            MarkoffEdit r;
            r.oldStart = l; r.oldEnd = l + 2; r.newText.clear();
            out << r;
        }
    } else {
        for (quint32 l : lineStarts) {
            MarkoffEdit r;
            r.oldStart = l; r.oldEnd = l; r.newText = "> ";
            out << r;
        }
    }
    return out;
}
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/Cmd/Block.cpp \
        libs/markoff-core/tests/tst_foundation_cmd_block.cpp
git commit -m "feat(foundation): Cmd::toggleCheckbox + blockQuote"
```

---

### Task 36: Cmd::insertTable / insertLink / insertImage / insertHorizontalRule

Insert at a given anchor.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Cmd/Insert.h`
- Create: `libs/markoff-core/src/Cmd/Insert.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_cmd_insert.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Cmd/Insert.h>
#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

class TstFoundationCmdInsert : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insert_link() {
        MarkoffDocument doc(1);
        Cmd::insertLink(&doc, doc.anchorAt(0, Bias::Left), "click", "https://x");
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("[click](https://x)"));
    }

    void insert_image() {
        MarkoffDocument doc(1);
        Cmd::insertImage(&doc, doc.anchorAt(0, Bias::Left), "alt", "img.png");
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("![alt](img.png)"));
    }

    void insert_horizontal_rule() {
        MarkoffDocument doc(1);
        Cmd::insertHorizontalRule(&doc, doc.anchorAt(0, Bias::Left));
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\n---\n"));
    }

    void insert_table_2x2_with_header() {
        MarkoffDocument doc(1);
        Cmd::insertTable(&doc, doc.anchorAt(0, Bias::Left), 2, 2, true);
        const QByteArray expected = "|  |  |\n|---|---|\n|  |  |\n";
        QCOMPARE(doc.toMarkdownUtf8(), expected);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdInsert)
#include "tst_foundation_cmd_insert.moc"
```

Append target to `tests/CMakeLists.txt` mirroring the prior pattern.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create Cmd/Insert.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <crdt/Anchor.h>
#include <crdt/Operations.h>

#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;

namespace Cmd {

MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertTable(const MarkoffDocument *, const CollabText::Crdt::Anchor &,
                         int rows, int cols, bool hasHeader);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertLink(const MarkoffDocument *, const CollabText::Crdt::Anchor &,
                        const QString &linkText, const QString &target);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertImage(const MarkoffDocument *, const CollabText::Crdt::Anchor &,
                         const QString &alt, const QString &target);
MARKOFF_FOUNDATION_EXPORT QList<MarkoffEdit>
    editsForInsertHorizontalRule(const MarkoffDocument *,
                                  const CollabText::Crdt::Anchor &);

MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertTable(MarkoffDocument *, const CollabText::Crdt::Anchor &,
                 int rows, int cols, bool hasHeader = true);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertLink(MarkoffDocument *, const CollabText::Crdt::Anchor &,
                const QString &linkText, const QString &target);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertImage(MarkoffDocument *, const CollabText::Crdt::Anchor &,
                 const QString &alt, const QString &target);
MARKOFF_FOUNDATION_EXPORT CollabText::Crdt::Operation
    insertHorizontalRule(MarkoffDocument *, const CollabText::Crdt::Anchor &);

}}  // namespace Markoff::Cmd
```

- [ ] **Step 4: Create src/Cmd/Insert.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Cmd/Insert.h>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff::Cmd {

namespace {
QList<MarkoffEdit> insertOne(const MarkoffDocument *doc,
                              const CollabText::Crdt::Anchor &a,
                              const QByteArray &text)
{
    if (!doc) return {};
    MarkoffEdit r;
    r.oldStart = doc->resolveAnchor(a);
    r.oldEnd   = r.oldStart;
    r.newText  = text;
    return { r };
}
}

QList<MarkoffEdit> editsForInsertLink(const MarkoffDocument *d,
                                       const CollabText::Crdt::Anchor &a,
                                       const QString &t, const QString &u)
{
    return insertOne(d, a, QStringLiteral("[%1](%2)").arg(t, u).toUtf8());
}

QList<MarkoffEdit> editsForInsertImage(const MarkoffDocument *d,
                                        const CollabText::Crdt::Anchor &a,
                                        const QString &alt, const QString &u)
{
    return insertOne(d, a, QStringLiteral("![%1](%2)").arg(alt, u).toUtf8());
}

QList<MarkoffEdit> editsForInsertHorizontalRule(const MarkoffDocument *d,
                                                 const CollabText::Crdt::Anchor &a)
{
    return insertOne(d, a, QByteArray("\n---\n"));
}

QList<MarkoffEdit> editsForInsertTable(const MarkoffDocument *d,
                                        const CollabText::Crdt::Anchor &a,
                                        int rows, int cols, bool hasHeader)
{
    if (rows < 1 || cols < 1) return {};
    QByteArray out;
    auto emitRow = [&](){
        out += '|';
        for (int c = 0; c < cols; ++c) out += "  |";
        out += '\n';
    };
    auto emitSep = [&](){
        out += '|';
        for (int c = 0; c < cols; ++c) out += "---|";
        out += '\n';
    };
    emitRow();
    if (hasHeader) emitSep();
    for (int r = 1; r < rows; ++r) emitRow();
    return insertOne(d, a, out);
}

CollabText::Crdt::Operation insertLink(MarkoffDocument *d,
                                        const CollabText::Crdt::Anchor &a,
                                        const QString &t, const QString &u)
{ return d->applyLocalEdit(editsForInsertLink(d, a, t, u)); }
CollabText::Crdt::Operation insertImage(MarkoffDocument *d,
                                         const CollabText::Crdt::Anchor &a,
                                         const QString &alt, const QString &u)
{ return d->applyLocalEdit(editsForInsertImage(d, a, alt, u)); }
CollabText::Crdt::Operation insertHorizontalRule(MarkoffDocument *d,
                                                  const CollabText::Crdt::Anchor &a)
{ return d->applyLocalEdit(editsForInsertHorizontalRule(d, a)); }
CollabText::Crdt::Operation insertTable(MarkoffDocument *d,
                                         const CollabText::Crdt::Anchor &a,
                                         int rows, int cols, bool hasHeader)
{ return d->applyLocalEdit(editsForInsertTable(d, a, rows, cols, hasHeader)); }

}  // namespace Markoff::Cmd
```

- [ ] **Step 5: Add to library target.**

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Cmd/Insert.h \
        libs/markoff-core/src/Cmd/Insert.cpp \
        libs/markoff-core/tests/tst_foundation_cmd_insert.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Cmd insert family (table/link/image/HR)"
```

---

### Task 37: Cmd::applyToAllPrimaryAndSecondaries helper

Iterates `session->primarySelection()` plus `session->secondarySelections()` of `Kind::Secondary`, applies the given `editsFn` to each, batches into a single `applyLocalEdit`.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Cmd.h` (aggregate include)
- Modify: `libs/markoff-core/src/Cmd/Helpers.cpp` (add the multi-cursor helper)
- Modify: `libs/markoff-core/src/Cmd/Helpers.h` (declare it)
- Create: `libs/markoff-core/tests/tst_foundation_cmd_multi.cpp`

- [ ] **Step 1: Write the failing test**

Create `libs/markoff-core/tests/tst_foundation_cmd_multi.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Cmd.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

class TstFoundationCmdMulti : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void multi_cursor_toggle_bold_each_secondary() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "abc def ghi";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        Selection p; p.anchor = doc.anchorAt(0, Bias::Left);
        p.active = doc.anchorAt(3, Bias::Right); p.kind = Selection::Kind::Primary;
        sess->setPrimarySelection(p);

        Selection s2; s2.anchor = doc.anchorAt(8, Bias::Left);
        s2.active = doc.anchorAt(11, Bias::Right); s2.kind = Selection::Kind::Secondary;
        sess->addSecondarySelection(s2);

        Cmd::applyToAllPrimaryAndSecondaries(&doc, sess, &Cmd::editsForToggleBold);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("**abc** def **ghi**"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCmdMulti)
#include "tst_foundation_cmd_multi.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail (header missing).**

- [ ] **Step 3: Add applyToAllPrimaryAndSecondaries to Cmd**

Create `libs/markoff-core/include/markoff-foundation/Cmd.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include <markoff/core/Cmd/Block.h>
#include <markoff/core/Cmd/Edit.h>
#include <markoff/core/Cmd/InlineFormat.h>
#include <markoff/core/Cmd/Insert.h>
#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
class Session;
struct Selection;

namespace Cmd {

using EditsFn = std::function<QList<MarkoffEdit>(const MarkoffDocument *,
                                                  const Selection &)>;

MARKOFF_FOUNDATION_EXPORT void applyToAllPrimaryAndSecondaries(
    MarkoffDocument *doc, Session *session, const EditsFn &fn);

}}  // namespace Markoff::Cmd
```

Add the impl in `Cmd/Helpers.cpp`:

```cpp
#include <markoff/core/Cmd.h>
#include <markoff/core/Session.h>

namespace Markoff::Cmd {

void applyToAllPrimaryAndSecondaries(MarkoffDocument *doc, Session *session,
                                      const EditsFn &fn)
{
    if (!doc || !session || !fn) return;
    QList<MarkoffEdit> all;
    all << fn(doc, session->primarySelection());
    for (const Selection &s : session->secondarySelections()) {
        if (s.kind != Selection::Kind::Secondary) continue;
        all << fn(doc, s);
    }
    if (all.isEmpty()) return;
    // The edits per-selection are produced in OLD-text byte coordinates
    // independently. Sort by oldStart ascending to satisfy applyLocalEdit's
    // ordering precondition.
    std::sort(all.begin(), all.end(),
              [](const MarkoffEdit &a, const MarkoffEdit &b) {
                  return a.oldStart < b.oldStart;
              });
    doc->applyLocalEdit(all);
}

}  // namespace Markoff::Cmd
```

(Add `#include <algorithm>` if missing.)

- [ ] **Step 4: Add to library target.** Add `include/markoff-foundation/Cmd.h`.

- [ ] **Step 5: Build + run.** Expected: pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Cmd.h \
        libs/markoff-core/src/Cmd/Helpers.cpp \
        libs/markoff-core/tests/tst_foundation_cmd_multi.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Cmd::applyToAllPrimaryAndSecondaries multi-cursor helper"
```

---

## Phase 9 — CommandFacade (Task 38)

Implements spec §7.7's QML-facing facade.

### Task 38: CommandFacade Q_OBJECT for QML

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CommandFacade.h`
- Create: `libs/markoff-core/src/CommandFacade.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_command_facade.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/CommandFacade.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

class TstFoundationCommandFacade : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void toggle_bold_via_facade() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "hello";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        Selection p; p.anchor = doc.anchorAt(0, Bias::Left);
        p.active = doc.anchorAt(5, Bias::Right);
        sess->setPrimarySelection(p);

        CommandFacade facade;
        facade.setDocument(&doc);
        facade.setSession(sess);
        facade.toggleBold();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("**hello**"));
    }

    void undo_via_facade() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "x";
        ed << i;
        doc.applyLocalEdit(ed);

        CommandFacade facade;
        facade.setDocument(&doc);
        facade.undo();
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }
};

QTEST_APPLESS_MAIN(TstFoundationCommandFacade)
#include "tst_foundation_command_facade.moc"
```

Append the test target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create CommandFacade.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MarkoffDocument;
class Session;

class MARKOFF_FOUNDATION_EXPORT CommandFacade : public QObject {
    Q_OBJECT
    Q_PROPERTY(Markoff::MarkoffDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(Markoff::Session *session READ session WRITE setSession NOTIFY sessionChanged)
public:
    explicit CommandFacade(QObject *parent = nullptr);
    ~CommandFacade() override;

    MarkoffDocument *document() const;
    void             setDocument(MarkoffDocument *);

    Session *session() const;
    void     setSession(Session *);

    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleStrikethrough();
    Q_INVOKABLE void toggleInlineCode();
    Q_INVOKABLE void setHeading(int level);
    Q_INVOKABLE void toggleCheckbox();
    Q_INVOKABLE void blockQuote();
    Q_INVOKABLE void insertTable(int rows, int cols, bool hasHeader = true);
    Q_INVOKABLE void insertLink(const QString &linkText, const QString &target);
    Q_INVOKABLE void insertImage(const QString &alt, const QString &target);
    Q_INVOKABLE void insertHorizontalRule();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

Q_SIGNALS:
    void documentChanged();
    void sessionChanged();

private:
    MarkoffDocument *m_doc = nullptr;
    Session         *m_sess = nullptr;
};

}  // namespace Markoff
```

- [ ] **Step 4: Create CommandFacade.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CommandFacade.h>

#include <markoff/core/Cmd.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

namespace Markoff {

CommandFacade::CommandFacade(QObject *parent) : QObject(parent) {}
CommandFacade::~CommandFacade() = default;

MarkoffDocument *CommandFacade::document() const { return m_doc; }
void CommandFacade::setDocument(MarkoffDocument *d)
{ if (m_doc != d) { m_doc = d; Q_EMIT documentChanged(); } }

Session *CommandFacade::session() const { return m_sess; }
void CommandFacade::setSession(Session *s)
{ if (m_sess != s) { m_sess = s; Q_EMIT sessionChanged(); } }

void CommandFacade::toggleBold()
{ if (m_doc && m_sess) Cmd::toggleBold(m_doc, m_sess->primarySelection()); }
void CommandFacade::toggleItalic()
{ if (m_doc && m_sess) Cmd::toggleItalic(m_doc, m_sess->primarySelection()); }
void CommandFacade::toggleStrikethrough()
{ if (m_doc && m_sess) Cmd::toggleStrikethrough(m_doc, m_sess->primarySelection()); }
void CommandFacade::toggleInlineCode()
{ if (m_doc && m_sess) Cmd::toggleInlineCode(m_doc, m_sess->primarySelection()); }
void CommandFacade::setHeading(int level)
{ if (m_doc && m_sess) Cmd::setHeading(m_doc, m_sess->primarySelection(), level); }
void CommandFacade::toggleCheckbox()
{ if (m_doc && m_sess) Cmd::toggleCheckbox(m_doc, m_sess->primarySelection().active); }
void CommandFacade::blockQuote()
{ if (m_doc && m_sess) Cmd::blockQuote(m_doc, m_sess->primarySelection()); }
void CommandFacade::insertTable(int rows, int cols, bool hasHeader)
{ if (m_doc && m_sess) Cmd::insertTable(m_doc, m_sess->primarySelection().active,
                                          rows, cols, hasHeader); }
void CommandFacade::insertLink(const QString &t, const QString &u)
{ if (m_doc && m_sess) Cmd::insertLink(m_doc, m_sess->primarySelection().active, t, u); }
void CommandFacade::insertImage(const QString &a, const QString &u)
{ if (m_doc && m_sess) Cmd::insertImage(m_doc, m_sess->primarySelection().active, a, u); }
void CommandFacade::insertHorizontalRule()
{ if (m_doc && m_sess) Cmd::insertHorizontalRule(m_doc,
                                                   m_sess->primarySelection().active); }
void CommandFacade::undo() { if (m_doc) Cmd::undo(m_doc); }
void CommandFacade::redo() { if (m_doc) Cmd::redo(m_doc); }

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target.** Add `include/markoff-foundation/CommandFacade.h` and `src/CommandFacade.cpp`.

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CommandFacade.h \
        libs/markoff-core/src/CommandFacade.cpp \
        libs/markoff-core/tests/tst_foundation_command_facade.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): CommandFacade Q_OBJECT for QML"
```

---

## Phase 10 — Search + Replace (Tasks 39–42)

Implements spec §7.8.

### Task 39: SearchEngine FindFlags + findAll

`findAll(doc, session, needle, flags)` populates `session->secondarySelections()` with `Kind::SearchMatch` entries (anchor-bound) and returns the count.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/SearchEngine.h`
- Create: `libs/markoff-core/src/SearchEngine.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_search_engine.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/core/Session.h>

using namespace Markoff;

class TstFoundationSearchEngine : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void find_all_populates_secondary_search_matches() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "foo bar foo baz foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        const int n = s.findAll(&doc, sess, "foo", {});
        QCOMPARE(n, 3);

        int matches = 0;
        for (const Selection &x : sess->secondarySelections())
            if (x.kind == Selection::Kind::SearchMatch) ++matches;
        QCOMPARE(matches, 3);
    }

    void find_all_case_insensitive_default() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "Foo FOO foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        QCOMPARE(s.findAll(&doc, sess, "foo", {}), 3);
    }

    void find_all_case_sensitive() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "Foo FOO foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        QCOMPARE(s.findAll(&doc, sess, "foo",
                           SearchEngine::FindFlag::CaseSensitive), 1);
    }
};

QTEST_APPLESS_MAIN(TstFoundationSearchEngine)
#include "tst_foundation_search_engine.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create SearchEngine.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFlags>
#include <QObject>
#include <QString>

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MarkoffDocument;
class Session;

class MARKOFF_FOUNDATION_EXPORT SearchEngine : public QObject {
    Q_OBJECT
public:
    enum FindFlag {
        NoFlags        = 0x00,
        CaseSensitive  = 0x01,
        WholeWords     = 0x02,
        Regex          = 0x04,
        Backwards      = 0x08,
    };
    Q_DECLARE_FLAGS(FindFlags, FindFlag)
    Q_FLAG(FindFlags)

    explicit SearchEngine(QObject *parent = nullptr);
    ~SearchEngine() override;

    int  findAll(MarkoffDocument *, Session *, const QString &needle, FindFlags = NoFlags);
    bool findNext(MarkoffDocument *, Session *);
    bool findPrevious(MarkoffDocument *, Session *);
    void clearMatches(Session *);
};

}  // namespace Markoff

Q_DECLARE_OPERATORS_FOR_FLAGS(Markoff::SearchEngine::FindFlags)
```

- [ ] **Step 4: Create SearchEngine.cpp (this task: findAll only)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SearchEngine.h>

#include <QRegularExpression>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

#include <crdt/Anchor.h>

namespace Markoff {

SearchEngine::SearchEngine(QObject *parent) : QObject(parent) {}
SearchEngine::~SearchEngine() = default;

int SearchEngine::findAll(MarkoffDocument *doc, Session *sess,
                           const QString &needle, FindFlags flags)
{
    if (!doc || !sess || needle.isEmpty()) {
        if (sess) sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
        return 0;
    }
    sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);

    const QString hay = doc->toMarkdown();
    const Qt::CaseSensitivity cs = (flags & CaseSensitive)
        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QList<Selection> matches;
    if (flags & Regex) {
        QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
        if (cs == Qt::CaseInsensitive) opt |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(needle, opt);
        if (!re.isValid()) return 0;
        auto it = re.globalMatch(hay);
        while (it.hasNext()) {
            const auto m = it.next();
            const int u16start = m.capturedStart();
            const int u16end   = m.capturedEnd();
            const quint32 bs = static_cast<quint32>(
                hay.left(u16start).toUtf8().size());
            const quint32 be = static_cast<quint32>(
                hay.left(u16end).toUtf8().size());
            Selection x;
            x.kind = Selection::Kind::SearchMatch;
            x.anchor = doc->anchorAt(bs, CollabText::Crdt::Bias::Left);
            x.active = doc->anchorAt(be, CollabText::Crdt::Bias::Right);
            matches << x;
        }
    } else {
        int from = 0;
        for (;;) {
            const int idx = hay.indexOf(needle, from, cs);
            if (idx < 0) break;
            const int u16end = idx + needle.size();
            const quint32 bs = static_cast<quint32>(
                hay.left(idx).toUtf8().size());
            const quint32 be = static_cast<quint32>(
                hay.left(u16end).toUtf8().size());
            Selection x;
            x.kind = Selection::Kind::SearchMatch;
            x.anchor = doc->anchorAt(bs, CollabText::Crdt::Bias::Left);
            x.active = doc->anchorAt(be, CollabText::Crdt::Bias::Right);
            matches << x;
            from = u16end;
            if (needle.isEmpty()) ++from;
        }
    }
    for (const Selection &x : matches) sess->addSecondarySelection(x);
    return matches.size();
}

bool SearchEngine::findNext(MarkoffDocument *, Session *) { return false; }
bool SearchEngine::findPrevious(MarkoffDocument *, Session *) { return false; }
void SearchEngine::clearMatches(Session *sess)
{
    if (sess) sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target.** Add header + source.

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/SearchEngine.h \
        libs/markoff-core/src/SearchEngine.cpp \
        libs/markoff-core/tests/tst_foundation_search_engine.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): SearchEngine::findAll with FindFlags"
```

---

### Task 40: SearchEngine findNext / findPrevious / clearMatches

`findNext` / `findPrevious` cycle through `Kind::SearchMatch` selections in document order; on each call, set `primarySelection` to the next/prev match.

**Files:**
- Modify: `libs/markoff-core/src/SearchEngine.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_search_engine.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void find_next_advances_primary_selection() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "ab cd ef";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        s.findAll(&doc, sess, "cd", {});
        QVERIFY(s.findNext(&doc, sess));
        const auto p = sess->primarySelection();
        QCOMPARE(doc.resolveAnchor(p.anchor), quint32(3));
    }

    void clear_matches_removes_search_kind() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "abc";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine s;
        s.findAll(&doc, sess, "a", {});
        s.clearMatches(sess);
        for (const Selection &x : sess->secondarySelections())
            QVERIFY(x.kind != Selection::Kind::SearchMatch);
    }
```

- [ ] **Step 2: Run, verify fail (findNext returns false).**

- [ ] **Step 3: Implement findNext / findPrevious**

```cpp
namespace {
QList<Selection> matchesInOrder(Session *sess, const MarkoffDocument *doc) {
    QList<Selection> ms;
    for (const Selection &x : sess->secondarySelections())
        if (x.kind == Selection::Kind::SearchMatch) ms << x;
    std::sort(ms.begin(), ms.end(),
              [doc](const Selection &a, const Selection &b) {
                  return doc->resolveAnchor(a.anchor) < doc->resolveAnchor(b.anchor);
              });
    return ms;
}
}

bool SearchEngine::findNext(MarkoffDocument *doc, Session *sess)
{
    if (!doc || !sess) return false;
    const auto ms = matchesInOrder(sess, doc);
    if (ms.isEmpty()) return false;
    const quint32 cur = doc->resolveAnchor(sess->primarySelection().active);
    for (const Selection &x : ms) {
        if (doc->resolveAnchor(x.anchor) > cur) {
            Selection p = x; p.kind = Selection::Kind::Primary;
            sess->setPrimarySelection(p);
            return true;
        }
    }
    Selection p = ms.first(); p.kind = Selection::Kind::Primary;
    sess->setPrimarySelection(p);
    return true;
}

bool SearchEngine::findPrevious(MarkoffDocument *doc, Session *sess)
{
    if (!doc || !sess) return false;
    const auto ms = matchesInOrder(sess, doc);
    if (ms.isEmpty()) return false;
    const quint32 cur = doc->resolveAnchor(sess->primarySelection().anchor);
    for (auto it = ms.crbegin(); it != ms.crend(); ++it) {
        if (doc->resolveAnchor(it->active) < cur) {
            Selection p = *it; p.kind = Selection::Kind::Primary;
            sess->setPrimarySelection(p);
            return true;
        }
    }
    Selection p = ms.last(); p.kind = Selection::Kind::Primary;
    sess->setPrimarySelection(p);
    return true;
}
```

Add `#include <algorithm>`.

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/SearchEngine.cpp \
        libs/markoff-core/tests/tst_foundation_search_engine.cpp
git commit -m "feat(foundation): SearchEngine::findNext / findPrevious / clearMatches"
```

---

### Task 41: ReplaceController::replaceCurrent

Replace the active match (the one whose range matches `primarySelection`); advance primary to the next match.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/ReplaceController.h`
- Create: `libs/markoff-core/src/ReplaceController.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_replace_controller.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/ReplaceController.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/core/Session.h>

using namespace Markoff;

class TstFoundationReplaceController : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void replace_current_replaces_primary_match_and_advances() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = "foo foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine se;
        se.findAll(&doc, sess, "foo", {});
        se.findNext(&doc, sess);  // primary -> first match

        ReplaceController rc;
        QVERIFY(rc.replaceCurrent(&doc, sess, "bar").has_value());
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("bar foo"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationReplaceController)
#include "tst_foundation_replace_controller.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create ReplaceController.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <optional>

#include <crdt/Operations.h>

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {
class MarkoffDocument;
class Session;

class MARKOFF_FOUNDATION_EXPORT ReplaceController : public QObject {
    Q_OBJECT
public:
    explicit ReplaceController(QObject *parent = nullptr);
    ~ReplaceController() override;

    std::optional<CollabText::Crdt::Operation>
        replaceCurrent(MarkoffDocument *, Session *, const QString &replacement);

    struct ReplaceAllResult {
        int count = 0;
        std::optional<CollabText::Crdt::Operation> op;
    };
    ReplaceAllResult
        replaceAll(MarkoffDocument *, Session *, const QString &replacement);
};

}  // namespace Markoff
```

- [ ] **Step 4: Create ReplaceController.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/ReplaceController.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffEdit.h>
#include <markoff/core/SearchEngine.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

namespace Markoff {

ReplaceController::ReplaceController(QObject *parent) : QObject(parent) {}
ReplaceController::~ReplaceController() = default;

std::optional<CollabText::Crdt::Operation>
ReplaceController::replaceCurrent(MarkoffDocument *doc, Session *sess,
                                   const QString &replacement)
{
    if (!doc || !sess) return std::nullopt;
    const Selection p = sess->primarySelection();
    const quint32 a = doc->resolveAnchor(p.anchor);
    const quint32 b = doc->resolveAnchor(p.active);
    if (a == b) return std::nullopt;

    MarkoffEdit r;
    r.oldStart = std::min(a, b);
    r.oldEnd   = std::max(a, b);
    r.newText  = replacement.toUtf8();
    const auto op = doc->applyLocalEdit({ r });

    SearchEngine().findNext(doc, sess);
    return op;
}

ReplaceController::ReplaceAllResult
ReplaceController::replaceAll(MarkoffDocument *, Session *, const QString &)
{
    return {};   // filled in Task 42
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target.**

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/ReplaceController.h \
        libs/markoff-core/src/ReplaceController.cpp \
        libs/markoff-core/tests/tst_foundation_replace_controller.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): ReplaceController::replaceCurrent"
```

---

### Task 42: ReplaceController::replaceAll

Batched replace of all matches in document order. Returns count + Operation.

**Files:**
- Modify: `libs/markoff-core/src/ReplaceController.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_replace_controller.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void replace_all_replaces_every_match() {
        MarkoffDocument doc(1);
        QList<MarkoffEdit> ed;
        MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0;
        i.newText = "foo bar foo baz foo";
        ed << i;
        doc.applyLocalEdit(ed);

        Session *sess = doc.createSession();
        SearchEngine().findAll(&doc, sess, "foo", {});

        ReplaceController rc;
        const auto r = rc.replaceAll(&doc, sess, "X");
        QCOMPARE(r.count, 3);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("X bar X baz X"));
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Implement replaceAll**

```cpp
ReplaceController::ReplaceAllResult
ReplaceController::replaceAll(MarkoffDocument *doc, Session *sess,
                               const QString &replacement)
{
    ReplaceAllResult res;
    if (!doc || !sess) return res;

    QList<MarkoffEdit> edits;
    for (const Selection &x : sess->secondarySelections()) {
        if (x.kind != Selection::Kind::SearchMatch) continue;
        const quint32 a = doc->resolveAnchor(x.anchor);
        const quint32 b = doc->resolveAnchor(x.active);
        MarkoffEdit r;
        r.oldStart = std::min(a, b);
        r.oldEnd   = std::max(a, b);
        r.newText  = replacement.toUtf8();
        edits << r;
    }
    if (edits.isEmpty()) return res;
    std::sort(edits.begin(), edits.end(),
              [](const MarkoffEdit &a, const MarkoffEdit &b) {
                  return a.oldStart < b.oldStart;
              });
    res.op = doc->applyLocalEdit(edits);
    res.count = edits.size();
    SearchEngine().clearMatches(sess);
    return res;
}
```

Add `#include <algorithm>`.

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/ReplaceController.cpp \
        libs/markoff-core/tests/tst_foundation_replace_controller.cpp
git commit -m "feat(foundation): ReplaceController::replaceAll batched"
```

---

## Phase 11 — Code blocks (Tasks 43–47)

Implements spec §7.9.

### Task 43: CodeTokenKind + CodeSpan

Plain enum + value struct.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CodeTokenKind.h`
- Create: `libs/markoff-core/include/markoff-foundation/CodeSpan.h`
- Create: `libs/markoff-core/tests/tst_foundation_code_token.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/CodeTokenKind.h>

using namespace Markoff;

class TstFoundationCodeToken : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void code_span_default_kind_is_default() {
        CodeSpan s;
        QCOMPARE(s.kind, CodeTokenKind::Default);
        QCOMPARE(s.offset, quint32(0));
    }

    void code_span_carries_range_and_kind() {
        CodeSpan s;
        s.offset = 5; s.length = 7; s.kind = CodeTokenKind::Keyword;
        QCOMPARE(s.offset, quint32(5));
        QCOMPARE(s.length, quint32(7));
        QCOMPARE(s.kind, CodeTokenKind::Keyword);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCodeToken)
#include "tst_foundation_code_token.moc"
```

Append target.

- [ ] **Step 2: Verify fail (header missing).**

- [ ] **Step 3: Create CodeTokenKind.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

enum class CodeTokenKind {
    Default,
    Keyword, ControlFlow, Builtin,
    Type, Function, Variable, Constant,
    Operator, Punctuation,
    String, Number, Boolean,
    Comment, Documentation,
    Preprocessor, Annotation,
};

}  // namespace Markoff
```

- [ ] **Step 4: Create CodeSpan.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#include <markoff/core/CodeTokenKind.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CodeSpan {
    quint32       offset = 0;     // UTF-8 byte offset within the code-block content
    quint32       length = 0;     // UTF-8 byte length
    CodeTokenKind kind = CodeTokenKind::Default;
};

}  // namespace Markoff
```

- [ ] **Step 5: Add headers to library target.**

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CodeTokenKind.h \
        libs/markoff-core/include/markoff-foundation/CodeSpan.h \
        libs/markoff-core/tests/tst_foundation_code_token.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): CodeTokenKind + CodeSpan value types"
```

---

### Task 44: SyntaxHighlightService interface

Abstract base.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/SyntaxHighlightService.h`
- Create: `libs/markoff-core/src/SyntaxHighlightService.cpp` (vtable anchor)

- [ ] **Step 1: Create header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT SyntaxHighlightService : public QObject {
    Q_OBJECT
public:
    explicit SyntaxHighlightService(QObject *parent = nullptr);
    ~SyntaxHighlightService() override;

    virtual QList<CodeSpan> highlight(const QString &language,
                                      const QByteArray &contentUtf8) const = 0;
    virtual QStringList     availableLanguages() const = 0;
    virtual bool            supportsLanguage(const QString &lang) const = 0;
};

}  // namespace Markoff
```

- [ ] **Step 2: Create cpp (vtable anchor only)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SyntaxHighlightService.h>

namespace Markoff {

SyntaxHighlightService::SyntaxHighlightService(QObject *parent) : QObject(parent) {}
SyntaxHighlightService::~SyntaxHighlightService() = default;

}  // namespace Markoff
```

- [ ] **Step 3: Add to library target.**

- [ ] **Step 4: Build.**

```bash
cmake --build build-dev --target markoff_core -j 2>&1 | tail -3
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/SyntaxHighlightService.h \
        libs/markoff-core/src/SyntaxHighlightService.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): SyntaxHighlightService abstract interface"
```

---

### Task 45: Kf6SyntaxHighlightService implementation

KF6::SyntaxHighlighting backend; translate KF6 token classes → `CodeTokenKind`.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/Kf6SyntaxHighlightService.h`
- Create: `libs/markoff-core/src/Kf6SyntaxHighlightService.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_syntax_highlight_service.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/Kf6SyntaxHighlightService.h>

using namespace Markoff;

class TstFoundationSyntaxHighlightService : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void available_languages_includes_cpp() {
        Kf6SyntaxHighlightService s;
        const QStringList langs = s.availableLanguages();
        bool found = false;
        for (const QString &l : langs)
            if (l.compare("c++", Qt::CaseInsensitive) == 0
                || l.compare("cpp", Qt::CaseInsensitive) == 0)
            { found = true; break; }
        QVERIFY(found);
    }

    void supports_language_returns_true_for_known() {
        Kf6SyntaxHighlightService s;
        QVERIFY(s.supportsLanguage("c++") || s.supportsLanguage("cpp"));
    }

    void highlight_yields_some_spans() {
        Kf6SyntaxHighlightService s;
        const QString lang = s.supportsLanguage("c++") ? "c++" : "cpp";
        const auto spans = s.highlight(lang,
            QByteArray("int main() { return 0; }"));
        QVERIFY(!spans.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstFoundationSyntaxHighlightService)
#include "tst_foundation_syntax_highlight_service.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create Kf6SyntaxHighlightService.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/SyntaxHighlightService.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT Kf6SyntaxHighlightService
    : public SyntaxHighlightService
{
    Q_OBJECT
public:
    explicit Kf6SyntaxHighlightService(QObject *parent = nullptr);
    ~Kf6SyntaxHighlightService() override;

    QList<CodeSpan> highlight(const QString &language,
                                const QByteArray &contentUtf8) const override;
    QStringList     availableLanguages() const override;
    bool            supportsLanguage(const QString &lang) const override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
```

Add `#include <memory>`.

- [ ] **Step 4: Create Kf6SyntaxHighlightService.cpp**

<!-- AMBIGUITY: KF6::SyntaxHighlighting's AbstractHighlighter API requires subclassing and per-token callbacks. The implementation below sketches the integration; concrete details (handling continuation states across line breaks, mapping every Theme::TextStyle to CodeTokenKind) follow the existing markoff-source / markoff-live code patterns. The test only asserts spans non-empty. -->

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/Kf6SyntaxHighlightService.h>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

namespace Markoff {

namespace {
CodeTokenKind mapStyle(KSyntaxHighlighting::Theme::TextStyle s)
{
    using TS = KSyntaxHighlighting::Theme::TextStyle;
    switch (s) {
    case TS::Keyword:        return CodeTokenKind::Keyword;
    case TS::ControlFlow:    return CodeTokenKind::ControlFlow;
    case TS::BuiltIn:        return CodeTokenKind::Builtin;
    case TS::DataType:       return CodeTokenKind::Type;
    case TS::Function:       return CodeTokenKind::Function;
    case TS::Variable:       return CodeTokenKind::Variable;
    case TS::Constant:       return CodeTokenKind::Constant;
    case TS::Operator:       return CodeTokenKind::Operator;
    case TS::String:         return CodeTokenKind::String;
    case TS::DecVal:
    case TS::BaseN:
    case TS::Float:          return CodeTokenKind::Number;
    case TS::Comment:        return CodeTokenKind::Comment;
    case TS::Documentation:  return CodeTokenKind::Documentation;
    case TS::Preprocessor:   return CodeTokenKind::Preprocessor;
    case TS::Annotation:     return CodeTokenKind::Annotation;
    default:                 return CodeTokenKind::Default;
    }
}

class CollectingHighlighter : public KSyntaxHighlighting::AbstractHighlighter {
public:
    QList<CodeSpan> spans;
    quint32         lineByteBase = 0;
protected:
    void applyFormat(int offset, int length,
                     const KSyntaxHighlighting::Format &fmt) override
    {
        CodeSpan sp;
        sp.offset = lineByteBase + static_cast<quint32>(offset);
        sp.length = static_cast<quint32>(length);
        sp.kind   = mapStyle(fmt.textStyle());
        spans << sp;
    }
};
}

struct Kf6SyntaxHighlightService::Private {
    KSyntaxHighlighting::Repository repo;
};

Kf6SyntaxHighlightService::Kf6SyntaxHighlightService(QObject *parent)
    : SyntaxHighlightService(parent)
    , d(std::make_unique<Private>())
{
}
Kf6SyntaxHighlightService::~Kf6SyntaxHighlightService() = default;

QList<CodeSpan> Kf6SyntaxHighlightService::highlight(const QString &language,
                                                       const QByteArray &contentUtf8) const
{
    auto def = d->repo.definitionForName(language);
    if (!def.isValid()) return {};

    CollectingHighlighter h;
    h.setDefinition(def);
    h.setTheme(d->repo.defaultTheme(KSyntaxHighlighting::Repository::LightTheme));

    KSyntaxHighlighting::State state;
    const QString text = QString::fromUtf8(contentUtf8);
    quint32 byteBase = 0;
    for (const QString &line : text.split('\n')) {
        h.lineByteBase = byteBase;
        state = h.highlightLine(line, state);
        byteBase += static_cast<quint32>(line.toUtf8().size()) + 1;
    }
    return h.spans;
}

QStringList Kf6SyntaxHighlightService::availableLanguages() const
{
    QStringList out;
    for (const auto &def : d->repo.definitions()) out << def.name();
    return out;
}

bool Kf6SyntaxHighlightService::supportsLanguage(const QString &lang) const
{
    return d->repo.definitionForName(lang).isValid();
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target.**

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/Kf6SyntaxHighlightService.h \
        libs/markoff-core/src/Kf6SyntaxHighlightService.cpp \
        libs/markoff-core/tests/tst_foundation_syntax_highlight_service.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): Kf6SyntaxHighlightService"
```

---

### Task 46: CodeBlockProcessor + RenderedBlock

Abstract processor base + value struct for output.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/RenderedBlock.h`
- Create: `libs/markoff-core/include/markoff-foundation/CodeBlockProcessor.h`

(No tests; tested transitively via the registry in Task 47.)

- [ ] **Step 1: Create RenderedBlock.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <markoff/core/CodeSpan.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT RenderedBlock {
    enum class Kind { Image, Svg, Highlighted, Empty };

    Kind             kind = Kind::Empty;
    QImage           image;
    QString          svg;
    QList<CodeSpan>  spans;
    QSize            preferredSize;
    QString          fallbackText;
};

}  // namespace Markoff
```

- [ ] **Step 2: Create CodeBlockProcessor.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <markoff/core/MarkoffFoundationExport.h>
#include <markoff/core/RenderedBlock.h>

namespace Markoff {

class Theme;

class MARKOFF_FOUNDATION_EXPORT CodeBlockProcessor {
public:
    virtual ~CodeBlockProcessor() = default;
    virtual QString       language() const = 0;
    virtual RenderedBlock render(const QByteArray &contentUtf8,
                                  const Theme &theme) = 0;
};

}  // namespace Markoff
```

- [ ] **Step 3: Add headers to library target.**

- [ ] **Step 4: Build.**

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/RenderedBlock.h \
        libs/markoff-core/include/markoff-foundation/CodeBlockProcessor.h \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): CodeBlockProcessor + RenderedBlock"
```

---

### Task 47: CodeBlockProcessorRegistry

Register / unregister / lookup.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CodeBlockProcessorRegistry.h`
- Create: `libs/markoff-core/src/CodeBlockProcessorRegistry.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_code_block_processor_registry.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/CodeBlockProcessor.h>
#include <markoff/core/CodeBlockProcessorRegistry.h>
#include <markoff/core/Theme.h>

using namespace Markoff;

namespace {
class FakeMermaidProcessor : public CodeBlockProcessor {
public:
    QString language() const override { return "mermaid"; }
    RenderedBlock render(const QByteArray &, const Theme &) override
    { RenderedBlock r; r.kind = RenderedBlock::Kind::Empty; return r; }
};
}

class TstFoundationCodeBlockRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void register_then_lookup() {
        CodeBlockProcessorRegistry r;
        QSignalSpy spy(&r, &CodeBlockProcessorRegistry::processorRegistered);
        r.registerProcessor(std::make_shared<FakeMermaidProcessor>());
        QCOMPARE(spy.count(), 1);
        QVERIFY(r.processorFor("mermaid") != nullptr);
        QVERIFY(r.processorFor("plantuml") == nullptr);
    }

    void unregister_removes_processor() {
        CodeBlockProcessorRegistry r;
        r.registerProcessor(std::make_shared<FakeMermaidProcessor>());
        QSignalSpy spy(&r, &CodeBlockProcessorRegistry::processorUnregistered);
        r.unregisterProcessor("mermaid");
        QCOMPARE(spy.count(), 1);
        QVERIFY(r.processorFor("mermaid") == nullptr);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCodeBlockRegistry)
#include "tst_foundation_code_block_processor_registry.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create CodeBlockProcessorRegistry.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include <memory>

#include <markoff/core/CodeBlockProcessor.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT CodeBlockProcessorRegistry : public QObject {
    Q_OBJECT
public:
    explicit CodeBlockProcessorRegistry(QObject *parent = nullptr);
    ~CodeBlockProcessorRegistry() override;

    void registerProcessor(std::shared_ptr<CodeBlockProcessor>);
    void unregisterProcessor(const QString &language);
    std::shared_ptr<CodeBlockProcessor> processorFor(const QString &language) const;
    QStringList registeredLanguages() const;

Q_SIGNALS:
    void processorRegistered(const QString &);
    void processorUnregistered(const QString &);

private:
    QHash<QString, std::shared_ptr<CodeBlockProcessor>> m_byLang;
};

}  // namespace Markoff
```

- [ ] **Step 4: Create CodeBlockProcessorRegistry.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CodeBlockProcessorRegistry.h>

namespace Markoff {

CodeBlockProcessorRegistry::CodeBlockProcessorRegistry(QObject *parent)
    : QObject(parent) {}
CodeBlockProcessorRegistry::~CodeBlockProcessorRegistry() = default;

void CodeBlockProcessorRegistry::registerProcessor(
    std::shared_ptr<CodeBlockProcessor> p)
{
    if (!p) return;
    const QString lang = p->language();
    m_byLang.insert(lang, std::move(p));
    Q_EMIT processorRegistered(lang);
}

void CodeBlockProcessorRegistry::unregisterProcessor(const QString &lang)
{
    if (m_byLang.remove(lang) > 0) Q_EMIT processorUnregistered(lang);
}

std::shared_ptr<CodeBlockProcessor>
CodeBlockProcessorRegistry::processorFor(const QString &lang) const
{
    return m_byLang.value(lang);
}

QStringList CodeBlockProcessorRegistry::registeredLanguages() const
{
    return m_byLang.keys();
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target.**

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CodeBlockProcessorRegistry.h \
        libs/markoff-core/src/CodeBlockProcessorRegistry.cpp \
        libs/markoff-core/tests/tst_foundation_code_block_processor_registry.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): CodeBlockProcessorRegistry"
```

---

## Phase 12 — Completion (Tasks 48–52)

Implements spec §7.10.

### Task 48: CompletionTrigger + CompletionContext + CompletionCandidate

Plain types.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CompletionTrigger.h`
- Create: `libs/markoff-core/include/markoff-foundation/CompletionContext.h`
- Create: `libs/markoff-core/include/markoff-foundation/CompletionCandidate.h`
- Create: `libs/markoff-core/tests/tst_foundation_completion_types.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/CompletionCandidate.h>
#include <markoff/core/CompletionContext.h>
#include <markoff/core/CompletionTrigger.h>

using namespace Markoff;

class TstFoundationCompletionTypes : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void context_default_is_inactive() {
        CompletionContext ctx;
        QVERIFY(!ctx.isActive());
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void context_active_when_trigger_set() {
        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::WikiLink;
        QVERIFY(ctx.isActive());
    }

    void candidate_carries_fields() {
        CompletionCandidate c;
        c.display = ":smile:";
        c.insertion = ":smile:";
        c.detail = "smiling face";
        QCOMPARE(c.priority, 0);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionTypes)
#include "tst_foundation_completion_types.moc"
```

Append target.

- [ ] **Step 2: Verify fail.**

- [ ] **Step 3: Create CompletionTrigger.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

enum class CompletionTrigger {
    None,
    WikiLink,
    WikiLinkAnchor,
    Tag,
    Footnote,
    Emoji,
    Mention,
    SlashCommand,
    LinkPath,
    ImagePath,
};

}  // namespace Markoff
```

- [ ] **Step 4: Create CompletionContext.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <crdt/Anchor.h>

#include <markoff/core/CompletionTrigger.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CompletionContext {
    CompletionTrigger        trigger = CompletionTrigger::None;
    QString                  prefix;
    CollabText::Crdt::Anchor triggerStart;
    CollabText::Crdt::Anchor cursorAnchor;
    QString                  anchorContext;
    bool isActive() const { return trigger != CompletionTrigger::None; }
};

}  // namespace Markoff
```

- [ ] **Step 5: Create CompletionCandidate.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

struct MARKOFF_FOUNDATION_EXPORT CompletionCandidate {
    QString display;
    QString insertion;
    QString detail;
    QString iconName;
    int     priority = 0;
};

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target.**

- [ ] **Step 7: Build + run.** Expected: pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CompletionTrigger.h \
        libs/markoff-core/include/markoff-foundation/CompletionContext.h \
        libs/markoff-core/include/markoff-foundation/CompletionCandidate.h \
        libs/markoff-core/tests/tst_foundation_completion_types.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): CompletionTrigger + Context + Candidate value types"
```

---

### Task 49: CompletionDetector::detect (basic triggers)

Static `CompletionDetector::detect(doc, cursor)` returns a `CompletionContext` describing the active trigger (if any). Basic cases: `[[` → WikiLink, `:` → Emoji, `#` in body context → Tag.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CompletionDetector.h`
- Create: `libs/markoff-core/src/CompletionDetector.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_completion_detector.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/CompletionDetector.h>
#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;
using CollabText::Crdt::Bias;

namespace {
void seed(MarkoffDocument &doc, const QByteArray &text) {
    QList<MarkoffEdit> ed;
    MarkoffEdit i; i.oldStart = 0; i.oldEnd = 0; i.newText = text;
    ed << i;
    doc.applyLocalEdit(ed);
}
}

class TstFoundationCompletionDetector : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void wikilink_after_double_open_bracket() {
        MarkoffDocument doc(1);
        seed(doc, "see [[no");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(8, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::WikiLink);
        QCOMPARE(ctx.prefix, QStringLiteral("no"));
    }

    void emoji_after_colon() {
        MarkoffDocument doc(1);
        seed(doc, "hi :smi");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(7, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::Emoji);
        QCOMPARE(ctx.prefix, QStringLiteral("smi"));
    }

    void tag_after_hash_in_body() {
        MarkoffDocument doc(1);
        seed(doc, "body #ta");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(8, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::Tag);
        QCOMPARE(ctx.prefix, QStringLiteral("ta"));
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionDetector)
#include "tst_foundation_completion_detector.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create CompletionDetector.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <crdt/Anchor.h>

#include <markoff/core/CompletionContext.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_FOUNDATION_EXPORT CompletionDetector {
public:
    static CompletionContext
        detect(const MarkoffDocument *, const CollabText::Crdt::Anchor &cursor);
};

}  // namespace Markoff
```

- [ ] **Step 4: Create CompletionDetector.cpp (basic triggers)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CompletionDetector.h>

#include <markoff/core/MarkoffDocument.h>

namespace Markoff {

namespace {
bool isIdentChar(QChar c) {
    return c.isLetterOrNumber() || c == '_' || c == '-';
}
}

CompletionContext
CompletionDetector::detect(const MarkoffDocument *doc,
                            const CollabText::Crdt::Anchor &cursor)
{
    CompletionContext ctx;
    ctx.cursorAnchor = cursor;
    if (!doc) return ctx;

    const QString text = doc->toMarkdown();
    const quint32 byteOff = doc->resolveAnchor(cursor);
    // Convert byte offset to UTF-16 index.
    const QByteArray prefixBytes = doc->toMarkdownUtf8().left(byteOff);
    const int u16cur = QString::fromUtf8(prefixBytes).size();

    // Scan back from u16cur to a trigger char or whitespace.
    int i = u16cur;
    while (i > 0 && isIdentChar(text.at(i - 1))) --i;
    const QString prefix = text.mid(i, u16cur - i);
    if (i == 0) return ctx;
    const QChar trig = text.at(i - 1);

    if (trig == ':') {
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = prefix;
    } else if (trig == '[' && i >= 2 && text.at(i - 2) == '[') {
        ctx.trigger = CompletionTrigger::WikiLink;
        ctx.prefix  = prefix;
    } else if (trig == '#') {
        // Heading marker if at line start; tag otherwise.
        bool atLineStart = (i - 1 == 0) || text.at(i - 2) == '\n';
        if (!atLineStart) {
            ctx.trigger = CompletionTrigger::Tag;
            ctx.prefix  = prefix;
        }
    }
    return ctx;
}

}  // namespace Markoff
```

- [ ] **Step 5: Add to library target.**

- [ ] **Step 6: Build + run.** Expected: pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CompletionDetector.h \
        libs/markoff-core/src/CompletionDetector.cpp \
        libs/markoff-core/tests/tst_foundation_completion_detector.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): CompletionDetector basic triggers (wikilink/emoji/tag)"
```

---

### Task 50: CompletionDetector edge cases

Heading marker `#` at line start is NOT a tag. Code blocks (inside ``` fences) suppress all triggers. Escaped `\[[` is not a wikilink. `[^foo` is a footnote, not a regular link path.

**Files:**
- Modify: `libs/markoff-core/src/CompletionDetector.cpp`
- Modify: `libs/markoff-core/tests/tst_foundation_completion_detector.cpp`

- [ ] **Step 1: Write the failing test**

Append to the test class:

```cpp
    void heading_marker_not_a_tag() {
        MarkoffDocument doc(1);
        seed(doc, "#h");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(2, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void inside_fenced_code_block_suppresses_triggers() {
        MarkoffDocument doc(1);
        seed(doc, "```\n[[no");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(8, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void escaped_double_bracket_not_a_wikilink() {
        MarkoffDocument doc(1);
        seed(doc, "see \\[[no");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(9, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::None);
    }

    void footnote_marker_recognized() {
        MarkoffDocument doc(1);
        seed(doc, "see [^fn");
        const auto ctx = CompletionDetector::detect(&doc,
            doc.anchorAt(8, Bias::Left));
        QCOMPARE(ctx.trigger, CompletionTrigger::Footnote);
        QCOMPARE(ctx.prefix, QStringLiteral("fn"));
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Extend CompletionDetector.cpp**

Replace the body of `detect`:

```cpp
namespace {
bool insideFencedCodeBlock(const QString &text, int u16cur) {
    int fences = 0;
    int i = 0;
    while (i + 2 < u16cur) {
        if (text.at(i) == '`' && text.at(i + 1) == '`' && text.at(i + 2) == '`'
            && (i == 0 || text.at(i - 1) == '\n'))
            ++fences;
        ++i;
    }
    return fences % 2 == 1;
}

bool isEscaped(const QString &text, int idx) {
    int n = 0;
    for (int j = idx - 1; j >= 0 && text.at(j) == '\\'; --j) ++n;
    return n % 2 == 1;
}
}

CompletionContext
CompletionDetector::detect(const MarkoffDocument *doc,
                            const CollabText::Crdt::Anchor &cursor)
{
    CompletionContext ctx;
    ctx.cursorAnchor = cursor;
    if (!doc) return ctx;

    const QString text = doc->toMarkdown();
    const QByteArray prefixBytes = doc->toMarkdownUtf8().left(doc->resolveAnchor(cursor));
    const int u16cur = QString::fromUtf8(prefixBytes).size();

    if (insideFencedCodeBlock(text, u16cur)) return ctx;

    int i = u16cur;
    while (i > 0 && isIdentChar(text.at(i - 1))) --i;
    const QString prefix = text.mid(i, u16cur - i);
    if (i == 0) return ctx;
    const QChar trig = text.at(i - 1);

    if (trig == ':') {
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = prefix;
    } else if (trig == '[' && i >= 2 && text.at(i - 2) == '['
               && !isEscaped(text, i - 2))
    {
        ctx.trigger = CompletionTrigger::WikiLink;
        ctx.prefix  = prefix;
    } else if (trig == '^' && i >= 2 && text.at(i - 2) == '['
               && !isEscaped(text, i - 2))
    {
        ctx.trigger = CompletionTrigger::Footnote;
        ctx.prefix  = prefix;
    } else if (trig == '#') {
        const bool atLineStart = (i - 1 == 0) || text.at(i - 2) == '\n';
        if (!atLineStart) {
            ctx.trigger = CompletionTrigger::Tag;
            ctx.prefix  = prefix;
        }
    }
    return ctx;
}
```

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/CompletionDetector.cpp \
        libs/markoff-core/tests/tst_foundation_completion_detector.cpp
git commit -m "feat(foundation): CompletionDetector edge cases (heading/fence/escape/footnote)"
```

---

### Task 51: CompletionProvider + CompletionRegistry

Abstract provider; registry aggregates synchronous candidates and forwards async via `candidatesReady`.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/CompletionProvider.h`
- Create: `libs/markoff-core/include/markoff-foundation/CompletionRegistry.h`
- Create: `libs/markoff-core/src/CompletionRegistry.cpp`
- Create: `libs/markoff-core/tests/tst_foundation_completion_registry.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/CompletionContext.h>
#include <markoff/core/CompletionProvider.h>
#include <markoff/core/CompletionRegistry.h>

using namespace Markoff;

namespace {
class FakeProvider : public CompletionProvider {
public:
    QSet<CompletionTrigger> handledTriggers() const override
    { return { CompletionTrigger::Emoji }; }
    QList<CompletionCandidate> candidatesFor(const CompletionContext &c, quint64) override
    {
        QList<CompletionCandidate> out;
        if (c.trigger == CompletionTrigger::Emoji) {
            CompletionCandidate cc;
            cc.display = ":" + c.prefix + ":";
            cc.insertion = cc.display;
            out << cc;
        }
        return out;
    }
};
}

class TstFoundationCompletionRegistry : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void gather_returns_synchronous_candidates() {
        CompletionRegistry r;
        r.registerProvider(std::make_shared<FakeProvider>());

        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = "smile";
        const auto cands = r.gather(ctx, 1);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands.first().display, QStringLiteral(":smile:"));
    }

    void gather_filters_by_handled_triggers() {
        CompletionRegistry r;
        r.registerProvider(std::make_shared<FakeProvider>());

        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::Tag;
        ctx.prefix  = "x";
        QCOMPARE(r.gather(ctx, 1).size(), 0);
    }
};

QTEST_APPLESS_MAIN(TstFoundationCompletionRegistry)
#include "tst_foundation_completion_registry.moc"
```

Append target.

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create CompletionProvider.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QSet>

#include <markoff/core/CompletionCandidate.h>
#include <markoff/core/CompletionContext.h>
#include <markoff/core/CompletionTrigger.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT CompletionProvider : public QObject {
    Q_OBJECT
public:
    explicit CompletionProvider(QObject *parent = nullptr);
    ~CompletionProvider() override;

    virtual QSet<CompletionTrigger> handledTriggers() const = 0;
    virtual QList<CompletionCandidate>
        candidatesFor(const CompletionContext &, quint64 requestId) = 0;

Q_SIGNALS:
    void candidatesReady(quint64 requestId, QList<Markoff::CompletionCandidate>);
};

}  // namespace Markoff
```

- [ ] **Step 4: Create CompletionRegistry.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>

#include <memory>
#include <vector>

#include <markoff/core/CompletionCandidate.h>
#include <markoff/core/CompletionContext.h>
#include <markoff/core/CompletionProvider.h>
#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT CompletionRegistry : public QObject {
    Q_OBJECT
public:
    explicit CompletionRegistry(QObject *parent = nullptr);
    ~CompletionRegistry() override;

    void registerProvider(std::shared_ptr<CompletionProvider>);
    void unregisterProvider(CompletionProvider *);
    QList<CompletionCandidate>
        gather(const CompletionContext &, quint64 requestId);

Q_SIGNALS:
    void candidatesReady(quint64 requestId, QList<Markoff::CompletionCandidate>);

private:
    std::vector<std::shared_ptr<CompletionProvider>> m_providers;
};

}  // namespace Markoff
```

- [ ] **Step 5: Create the impl bodies**

Add a `CompletionProvider.cpp` (vtable anchor) — or place the trivial bodies inline. For consistency with `LinkService`, write a small cpp:

`libs/markoff-core/src/CompletionProvider.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CompletionProvider.h>

namespace Markoff {

CompletionProvider::CompletionProvider(QObject *parent) : QObject(parent) {}
CompletionProvider::~CompletionProvider() = default;

}  // namespace Markoff
```

`libs/markoff-core/src/CompletionRegistry.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/CompletionRegistry.h>

namespace Markoff {

CompletionRegistry::CompletionRegistry(QObject *parent) : QObject(parent) {}
CompletionRegistry::~CompletionRegistry() = default;

void CompletionRegistry::registerProvider(std::shared_ptr<CompletionProvider> p)
{
    if (!p) return;
    QObject::connect(p.get(), &CompletionProvider::candidatesReady,
                     this,    &CompletionRegistry::candidatesReady);
    m_providers.push_back(std::move(p));
}

void CompletionRegistry::unregisterProvider(CompletionProvider *raw)
{
    for (auto it = m_providers.begin(); it != m_providers.end(); ++it) {
        if (it->get() == raw) { m_providers.erase(it); break; }
    }
}

QList<CompletionCandidate>
CompletionRegistry::gather(const CompletionContext &ctx, quint64 reqId)
{
    QList<CompletionCandidate> out;
    for (auto &p : m_providers) {
        if (!p->handledTriggers().contains(ctx.trigger)) continue;
        out << p->candidatesFor(ctx, reqId);
    }
    return out;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target.**

- [ ] **Step 7: Build + run.** Expected: pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/CompletionProvider.h \
        libs/markoff-core/include/markoff-foundation/CompletionRegistry.h \
        libs/markoff-core/src/CompletionProvider.cpp \
        libs/markoff-core/src/CompletionRegistry.cpp \
        libs/markoff-core/tests/tst_foundation_completion_registry.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): CompletionProvider + CompletionRegistry"
```

---

### Task 52: EmojiCompletionProvider

Default provider with a baked-in emoji table. Test asserts that prefix `smi` returns at least one candidate matching `:smile:` (or similar).

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/EmojiCompletionProvider.h`
- Create: `libs/markoff-core/src/EmojiCompletionProvider.cpp`
- Create: `libs/markoff-core/src/EmojiData.h`
- Modify: `libs/markoff-core/tests/tst_foundation_completion_registry.cpp`

<!-- AMBIGUITY: spec §7.10 says "~600-emoji table baked in (or a curated subset)". This task ships a curated subset (~50 entries) sufficient for unit tests; expanding to a fuller set is a follow-on. -->

- [ ] **Step 1: Write the failing test**

Append to `tst_foundation_completion_registry.cpp`:

```cpp
#include <markoff/core/EmojiCompletionProvider.h>
// ... inside the class ...

    void emoji_provider_returns_smile_for_smi_prefix() {
        CompletionRegistry r;
        r.registerProvider(std::make_shared<EmojiCompletionProvider>());
        CompletionContext ctx;
        ctx.trigger = CompletionTrigger::Emoji;
        ctx.prefix  = "smi";
        const auto cands = r.gather(ctx, 1);
        bool foundSmile = false;
        for (const auto &c : cands)
            if (c.display.contains("smile", Qt::CaseInsensitive)) {
                foundSmile = true; break;
            }
        QVERIFY(foundSmile);
    }
```

- [ ] **Step 2: Run, verify fail.**

- [ ] **Step 3: Create EmojiData.h (curated subset)**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <array>

namespace Markoff::Detail {

struct EmojiEntry { const char *shortcode; const char *glyph; };

constexpr std::array<EmojiEntry, 50> kEmojis = {{
    { "smile",        "\xF0\x9F\x98\x83" },  // U+1F603
    { "smiley",       "\xF0\x9F\x98\x80" },
    { "grinning",     "\xF0\x9F\x98\x80" },
    { "joy",          "\xF0\x9F\x98\x82" },
    { "heart_eyes",   "\xF0\x9F\x98\x8D" },
    { "wink",         "\xF0\x9F\x98\x89" },
    { "thinking",     "\xF0\x9F\xA4\x94" },
    { "thumbsup",     "\xF0\x9F\x91\x8D" },
    { "thumbsdown",   "\xF0\x9F\x91\x8E" },
    { "ok_hand",      "\xF0\x9F\x91\x8C" },
    { "clap",         "\xF0\x9F\x91\x8F" },
    { "wave",         "\xF0\x9F\x91\x8B" },
    { "pray",         "\xF0\x9F\x99\x8F" },
    { "muscle",       "\xF0\x9F\x92\xAA" },
    { "fire",         "\xF0\x9F\x94\xA5" },
    { "sparkles",     "\xE2\x9C\xA8" },
    { "star",         "\xE2\xAD\x90" },
    { "heart",        "\xE2\x9D\xA4" },
    { "broken_heart", "\xF0\x9F\x92\x94" },
    { "tada",         "\xF0\x9F\x8E\x89" },
    { "rocket",       "\xF0\x9F\x9A\x80" },
    { "warning",      "\xE2\x9A\xA0" },
    { "x",            "\xE2\x9D\x8C" },
    { "white_check_mark", "\xE2\x9C\x85" },
    { "check",        "\xE2\x9C\x94" },
    { "eyes",         "\xF0\x9F\x91\x80" },
    { "see_no_evil",  "\xF0\x9F\x99\x88" },
    { "robot",        "\xF0\x9F\xA4\x96" },
    { "ghost",        "\xF0\x9F\x91\xBB" },
    { "skull",        "\xF0\x9F\x92\x80" },
    { "poop",         "\xF0\x9F\x92\xA9" },
    { "cake",         "\xF0\x9F\x8E\x82" },
    { "pizza",        "\xF0\x9F\x8D\x95" },
    { "coffee",       "\xE2\x98\x95" },
    { "tea",          "\xF0\x9F\xA7\x8B" },
    { "beer",         "\xF0\x9F\x8D\xBA" },
    { "computer",     "\xF0\x9F\x92\xBB" },
    { "phone",        "\xF0\x9F\x93\xB1" },
    { "book",         "\xF0\x9F\x93\x96" },
    { "pencil",       "\xE2\x9C\x8F" },
    { "memo",         "\xF0\x9F\x93\x9D" },
    { "bug",          "\xF0\x9F\x90\x9B" },
    { "lock",         "\xF0\x9F\x94\x92" },
    { "key",          "\xF0\x9F\x94\x91" },
    { "bulb",         "\xF0\x9F\x92\xA1" },
    { "calendar",     "\xF0\x9F\x93\x85" },
    { "moon",         "\xF0\x9F\x8C\x99" },
    { "sun_with_face","\xF0\x9F\x8C\x9E" },
    { "rainbow",      "\xF0\x9F\x8C\x88" },
    { "snowflake",    "\xE2\x9D\x84" },
}};

}  // namespace Markoff::Detail
```

- [ ] **Step 4: Create EmojiCompletionProvider.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/CompletionProvider.h>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT EmojiCompletionProvider : public CompletionProvider {
    Q_OBJECT
public:
    explicit EmojiCompletionProvider(QObject *parent = nullptr);

    QSet<CompletionTrigger> handledTriggers() const override;
    QList<CompletionCandidate>
        candidatesFor(const CompletionContext &, quint64 requestId) override;
};

}  // namespace Markoff
```

- [ ] **Step 5: Create EmojiCompletionProvider.cpp**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/EmojiCompletionProvider.h>

#include "EmojiData.h"

namespace Markoff {

EmojiCompletionProvider::EmojiCompletionProvider(QObject *parent)
    : CompletionProvider(parent) {}

QSet<CompletionTrigger> EmojiCompletionProvider::handledTriggers() const
{
    return { CompletionTrigger::Emoji };
}

QList<CompletionCandidate>
EmojiCompletionProvider::candidatesFor(const CompletionContext &ctx, quint64)
{
    QList<CompletionCandidate> out;
    if (ctx.trigger != CompletionTrigger::Emoji) return out;
    const QString prefix = ctx.prefix;
    for (const auto &e : Detail::kEmojis) {
        const QString sc = QString::fromLatin1(e.shortcode);
        if (sc.startsWith(prefix, Qt::CaseInsensitive)) {
            CompletionCandidate c;
            c.display   = QStringLiteral(":%1: %2").arg(sc,
                                              QString::fromUtf8(e.glyph));
            c.insertion = QStringLiteral(":%1:").arg(sc);
            c.detail    = sc;
            out << c;
        }
    }
    return out;
}

}  // namespace Markoff
```

- [ ] **Step 6: Add to library target.**

- [ ] **Step 7: Build + run.** Expected: pass.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/EmojiCompletionProvider.h \
        libs/markoff-core/src/EmojiCompletionProvider.cpp \
        libs/markoff-core/src/EmojiData.h \
        libs/markoff-core/tests/tst_foundation_completion_registry.cpp \
        libs/markoff-core/CMakeLists.txt
git commit -m "feat(foundation): EmojiCompletionProvider with curated subset"
```

---

## Phase 13 — Services bundle + property tests + acceptance (Tasks 53–55)

### Task 53: MarkoffServices struct

A plain struct bundling non-owning service pointers. Tested transitively via integration; this task adds a trivial smoke test and the public header.

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/MarkoffServices.h`
- Create: `libs/markoff-core/tests/tst_foundation_services_bundle.cpp`

- [ ] **Step 1: Create MarkoffServices.h**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffFoundationExport.h>

namespace Markoff {

class CodeBlockProcessorRegistry;
class CompletionRegistry;
class LinkService;
class SyntaxHighlightService;

struct MARKOFF_FOUNDATION_EXPORT MarkoffServices {
    SyntaxHighlightService     *syntax = nullptr;
    CodeBlockProcessorRegistry *codeProcessors = nullptr;
    LinkService                *links = nullptr;
    CompletionRegistry         *completion = nullptr;
};

}  // namespace Markoff
```

- [ ] **Step 2: Write a trivial smoke test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/core/CodeBlockProcessorRegistry.h>
#include <markoff/core/CompletionRegistry.h>
#include <markoff/core/DefaultLinkService.h>
#include <markoff/core/Kf6SyntaxHighlightService.h>
#include <markoff/core/MarkoffServices.h>

using namespace Markoff;

class TstFoundationServicesBundle : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void bundle_holds_non_owning_pointers() {
        Kf6SyntaxHighlightService syntax;
        CodeBlockProcessorRegistry procs;
        DefaultLinkService links;
        CompletionRegistry completion;

        MarkoffServices s;
        s.syntax = &syntax;
        s.codeProcessors = &procs;
        s.links = &links;
        s.completion = &completion;

        QVERIFY(s.syntax != nullptr);
        QVERIFY(s.codeProcessors != nullptr);
        QVERIFY(s.links != nullptr);
        QVERIFY(s.completion != nullptr);
    }
};

QTEST_APPLESS_MAIN(TstFoundationServicesBundle)
#include "tst_foundation_services_bundle.moc"
```

Append target.

- [ ] **Step 3: Add header to library target.**

- [ ] **Step 4: Build + run.** Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffServices.h \
        libs/markoff-core/tests/tst_foundation_services_bundle.cpp \
        libs/markoff-core/CMakeLists.txt \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "feat(foundation): MarkoffServices bundle"
```

---

### Task 54: tst_foundation_markoff_document_property

Property-based test: random sequence of `applyLocalEdit` calls; assert `toMarkdownUtf8()` always equals an independently-computed reference text. ~100 sequences seeded by deterministic RNG.

**Files:**
- Create: `libs/markoff-core/tests/tst_foundation_markoff_document_property.cpp`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QByteArray>
#include <QRandomGenerator>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>

using namespace Markoff;

class TstFoundationMarkoffDocumentProperty : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void random_edit_sequences_match_reference() {
        const int kSequences = 100;
        const int kEditsPerSeq = 30;
        for (int seed = 0; seed < kSequences; ++seed) {
            QRandomGenerator rng(static_cast<quint32>(seed));
            MarkoffDocument doc(1);
            QByteArray ref;

            for (int step = 0; step < kEditsPerSeq; ++step) {
                const quint32 len = static_cast<quint32>(ref.size());
                const quint32 a = rng.bounded(len + 1);
                const quint32 b = rng.bounded(len + 1);
                const quint32 lo = std::min(a, b);
                const quint32 hi = std::max(a, b);

                QByteArray ins;
                if (rng.bounded(3) != 0) {
                    const int n = rng.bounded(5);
                    for (int k = 0; k < n; ++k)
                        ins.append(static_cast<char>('a' + rng.bounded(26)));
                }

                MarkoffEdit e;
                e.oldStart = lo; e.oldEnd = hi; e.newText = ins;
                doc.applyLocalEdit({ e });

                ref.replace(static_cast<int>(lo),
                            static_cast<int>(hi - lo), ins);

                QCOMPARE(doc.toMarkdownUtf8(), ref);
            }
        }
    }
};

QTEST_APPLESS_MAIN(TstFoundationMarkoffDocumentProperty)
#include "tst_foundation_markoff_document_property.moc"
```

- [ ] **Step 2: Add test target**

```cmake
add_executable(tst_foundation_markoff_document_property tst_foundation_markoff_document_property.cpp)
add_test(NAME tst_foundation_markoff_document_property COMMAND tst_foundation_markoff_document_property)
target_link_libraries(tst_foundation_markoff_document_property PRIVATE Qt6::Test markoff_core)
set_tests_properties(tst_foundation_markoff_document_property PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build + run**

```bash
cmake --build build-dev --target tst_foundation_markoff_document_property -j 2>&1 | tail -3
ctest --test-dir build-dev -R '^tst_foundation_markoff_document_property$' --output-on-failure
```

Expected: 100 sequences pass.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-core/tests/tst_foundation_markoff_document_property.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "test(foundation): property test for MarkoffDocument vs reference text"
```

---

### Task 55: Acceptance pass

Run the full ctest suite, document the baseline. Foundation is "viable" when all tests pass.

**Files:**
- Create: `docs/2026-04-28-foundation-tests-baseline.log`

- [ ] **Step 1: Verify a clean configure + build**

```bash
cd /home/clinton/dev/Markoff/.worktrees/foundation-exploration
rm -rf build-dev
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 2>&1 | tail -20
```

Expected: clean build of `markoff_core` and every `tst_foundation_*` target.

- [ ] **Step 2: Run the full ctest suite**

```bash
ctest --test-dir build-dev --output-on-failure -j 2>&1 | tee /tmp/foundation-tests.log
```

Expected: all foundation tests pass (and the existing `markoff-core` / `markoff-live` / `markoff-source` / `markoff-reading` / `markoff-parser` tests continue to pass — Phase 1's scaffolding does not touch them).

- [ ] **Step 3: Persist the test-suite baseline**

```bash
cp /tmp/foundation-tests.log docs/2026-04-28-foundation-tests-baseline.log
```

- [ ] **Step 4: Update `docs/TODO.md`**

Add an entry noting that Phase 13 acceptance has passed and the foundation is feature-complete per spec §12.

- [ ] **Step 5: Commit**

```bash
git add docs/2026-04-28-foundation-tests-baseline.log docs/TODO.md
git commit -m "$(cat <<'EOF'
docs(foundation): test-suite baseline at Phase 13 acceptance

All tst_foundation_* targets pass on a fresh build; existing markoff-*
suites continue to pass. Foundation is viable per spec §12.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6 (optional): Tag a milestone**

If desired:

```bash
git tag -a v0.3.0-foundation -m "markoff-foundation Phase 13 acceptance"
```

(Per the project's invariant 4: every phase milestone tags a Markoff version.)

---

## Self-review notes

1. **Spec coverage.** Tasks 18–23 cover §7.2 (Session). Task 24 covers §6.1 (ParsePool). Tasks 25–28 cover §7.5 (Theme). Tasks 29–30 cover §7.6 (LinkService). Tasks 31–37 cover §7.7 (Cmd). Task 38 covers §7.7 (CommandFacade). Tasks 39–42 cover §7.8 (Search/Replace). Tasks 43–47 cover §7.9 (code blocks). Tasks 48–52 cover §7.10 (completion). Task 53 covers §7.11 (services bundle). Task 54 covers §11.3 (property tests). Task 55 covers §12 (acceptance).

2. **TDD discipline.** Every implementation task has a failing test written first, observed to fail, then made to pass. Pure value-type tasks (43, 46, 48, 53) include a trivial round-trip / field test before the header is written.

3. **Naming.** Every new test executable in this part uses the `tst_foundation_*` prefix to avoid collision with the existing `markoff-core`, `markoff-live`, `markoff-source`, `markoff-reading`, and `markoff-parser` test targets. Source filenames mirror the executable names.

4. **Restored default arg.** Task 18 explicitly restores the `= {}` default on `MarkoffDocument::createSession` after `SessionParams` becomes a complete type; the test re-verification step confirms `tst_foundation_markoff_document` continues to pass.

5. **Reordering noted.** Task 28 (`Theme::colorForCodeToken`) depends on Task 43 (`CodeTokenKind`). The plan preserves the spec's phase ordering (Theme is Phase 6, CodeTokenKind is Phase 11) and instead defers Task 28's body until after Task 43; the task block notes this explicitly. No other reorderings.

6. **Ambiguities flagged.** See `<!-- AMBIGUITY -->` comments in:
   - Task 24 (ParsePool's debounce / worker thread internals are picked up from the existing markoff-core source verbatim).
   - Task 45 (KF6::SyntaxHighlighting integration — sketch follows existing patterns; full state-handling refinement is left to executors).
   - Task 52 (curated emoji subset; spec allowed either ~600 entries or a curated subset).
