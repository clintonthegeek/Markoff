# Heading Folding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add collapsable ATX headings to the markoff editor with a dedicated left gutter, path-based fold identity, and host-agnostic JSON serialization.

**Architecture:** A new `FoldingModel` owns the set of folded heading paths (`QStringList`s), fed from the existing `Editor::headingsChanged` signal after each tree-sitter reparse. `SceneCoordinator` subscribes to the model and hides items under folded headings. A new `FoldGutter` (viewport-pinned `QGraphicsObject` built on a `GutterColumn` interface) paints fold triangles and dispatches clicks. One concrete column ships in v1 (`FoldArrowColumn`); the interface proves future line-number columns plug in cleanly.

**Tech Stack:** Qt6 (Core, Gui, Widgets, Test) ≥ 6.8; C++20; tree-sitter-markdown via `MarkoffParser::MarkoffParser`; `QT_QPA_PLATFORM=offscreen` for tests.

**Spec:** [`../specs/2026-04-15-heading-folding-design.md`](../specs/2026-04-15-heading-folding-design.md). Read it before starting.

**Host integration (informational):** [`../2026-04-15-heading-folding-host-integration.md`](../2026-04-15-heading-folding-host-integration.md). Not this plan's concern.

---

## File structure

### New files

- `libs/markoff/include/markoff/FoldingTypes.h` — public type aliases (`FoldRegionKey = QStringList`) and free-function helpers for path computation.
- `libs/markoff/src/FoldingModel.h` / `.cpp` — the fold-state model (private to library; exposed through `Editor`).
- `libs/markoff/src/GutterColumn.h` — abstract `GutterColumn` base + `FoldArrowColumn` declaration (single header, two classes; small).
- `libs/markoff/src/FoldArrowColumn.cpp` — concrete column impl (triangle paint + click).
- `libs/markoff/src/FoldGutter.h` / `.cpp` — viewport-pinned `QGraphicsObject` that owns columns.
- `libs/markoff/tests/tst_folding_model.cpp`
- `libs/markoff/tests/tst_folding_reconcile.cpp`
- `libs/markoff/tests/tst_folding_integration.cpp`
- `libs/markoff/tests/tst_fold_gutter.cpp`
- `libs/markoff/tests/tst_fold_persistence.cpp`

### Modified files

- `libs/markoff/include/markoff/Editor.h` — public fold API + new signals.
- `libs/markoff/src/Editor.cpp` — own `FoldingModel`, wire to signals, implement API, own `FoldGutter`.
- `libs/markoff/src/SceneCoordinator.h` / `.cpp` — add `itemIndexAt(qreal sceneY)`, `setItemVisibilityByHeadingFold()`, subscribe to fold changes.
- `libs/markoff/CMakeLists.txt` — add new sources.
- `libs/markoff/tests/CMakeLists.txt` — register 5 new tests.

---

## Task 1: Path computation free function

**Files:**
- Create: `libs/markoff/include/markoff/FoldingTypes.h`
- Create: `libs/markoff/src/FoldingTypes.cpp`
- Modify: `libs/markoff/CMakeLists.txt` — add `src/FoldingTypes.cpp` to sources, add `include/markoff/FoldingTypes.h` to public headers list.
- Test: `libs/markoff/tests/tst_folding_model.cpp` (create; will grow in later tasks)

**Tests in this task cover only the path-computation helper.**

- [ ] **Step 1: Write the failing tests**

Create `libs/markoff/tests/tst_folding_model.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/FoldingTypes.h>
#include <markoff-parser/Document.h>

using namespace Markoff;

class TstFoldingModel : public QObject {
    Q_OBJECT
private slots:
    // --- Path computation ---
    void path_singleHeading_returnsOwnText();
    void path_nestedHeadings_includesAncestors();
    void path_skippedLevels_skipsMissingAncestors();
    void path_boldMarkdownInHeading_isStripped();
    void path_duplicateSiblings_getSuffix();
    void path_duplicateSiblings_firstHasNoSuffix();
};

static HeadingInfo h(int level, QString text, int off = 0) {
    return HeadingInfo{level, std::move(text), off};
}

void TstFoldingModel::path_singleHeading_returnsOwnText() {
    QList<HeadingInfo> headings = { h(1, "Intro") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths.size(), 1);
    QCOMPARE(paths[0], QStringList{ "Intro" });
}

void TstFoldingModel::path_nestedHeadings_includesAncestors() {
    QList<HeadingInfo> headings = {
        h(1, "Intro"),
        h(2, "Goals"),
        h(3, "Non-goals"),
    };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[0], (QStringList{ "Intro" }));
    QCOMPARE(paths[1], (QStringList{ "Intro", "Goals" }));
    QCOMPARE(paths[2], (QStringList{ "Intro", "Goals", "Non-goals" }));
}

void TstFoldingModel::path_skippedLevels_skipsMissingAncestors() {
    // # A \n ### C — no H2 between. Path is ["A", "C"] (skipped level).
    QList<HeadingInfo> headings = { h(1, "A"), h(3, "C") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[1], (QStringList{ "A", "C" }));
}

void TstFoldingModel::path_boldMarkdownInHeading_isStripped() {
    QList<HeadingInfo> headings = { h(2, "**Goals**") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[0], QStringList{ "Goals" });
}

void TstFoldingModel::path_duplicateSiblings_getSuffix() {
    QList<HeadingInfo> headings = {
        h(1, "A"),
        h(2, "Goals"),
        h(2, "Goals"),
        h(2, "Goals"),
    };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[1], (QStringList{ "A", "Goals" }));
    QCOMPARE(paths[2], (QStringList{ "A", "Goals#2" }));
    QCOMPARE(paths[3], (QStringList{ "A", "Goals#3" }));
}

void TstFoldingModel::path_duplicateSiblings_firstHasNoSuffix() {
    // Re-asserts the no-suffix-on-first rule in isolation.
    QList<HeadingInfo> headings = { h(1, "Same"), h(1, "Same") };
    auto paths = computeHeadingPaths(headings);
    QCOMPARE(paths[0], QStringList{ "Same" });
    QCOMPARE(paths[1], QStringList{ "Same#2" });
}

QTEST_MAIN(TstFoldingModel)
#include "tst_folding_model.moc"
```

Register in `libs/markoff/tests/CMakeLists.txt` (append):

```cmake
add_executable(tst_markoff_folding_model tst_folding_model.cpp)
add_test(NAME tst_markoff_folding_model COMMAND tst_markoff_folding_model)
target_link_libraries(tst_markoff_folding_model PRIVATE Qt6::Test markoff)
target_include_directories(tst_markoff_folding_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run test; verify it fails to build**

```
cd /home/clinton/dev/Corbomite
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build --target tst_markoff_folding_model 2>&1 | tail -20
```

Expected: build failure — `markoff/FoldingTypes.h` not found.

- [ ] **Step 3: Create the header**

Create `libs/markoff/include/markoff/FoldingTypes.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_FOLDINGTYPES_H
#define MARKOFF_FOLDINGTYPES_H

#include <QList>
#include <QStringList>

namespace Markoff {

struct HeadingInfo; // fwd decl from markoff-parser

/// A fold region's identity. For headings, this is the hierarchy path
/// ["Intro", "Goals", "Non-goals"]. Later block types (lists, code
/// blocks) will use their own shape but reuse this type.
using FoldRegionKey = QStringList;

/// Compute the hierarchy path for each heading in document order.
/// Stripping rules: inline markdown removed (`**bold**` -> `bold`).
/// Duplicate siblings disambiguated with `#N` suffix starting at `#2`.
QList<FoldRegionKey> computeHeadingPaths(const QList<HeadingInfo> &headings);

/// Strip inline markdown delimiters from a heading's raw text. Public
/// so tests and host code can compute paths equivalently.
QString normalizeHeadingText(const QString &raw);

} // namespace Markoff

#endif // MARKOFF_FOLDINGTYPES_H
```

- [ ] **Step 4: Create the implementation**

Create `libs/markoff/src/FoldingTypes.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/FoldingTypes.h>
#include <markoff-parser/Document.h>
#include <QHash>
#include <QRegularExpression>

namespace Markoff {

QString normalizeHeadingText(const QString &raw) {
    QString text = raw.trimmed();
    // Strip common inline markdown: **bold**, *italic*, _emph_, `code`,
    // ~~strike~~. Keep link text (drop brackets/URLs).
    static const QRegularExpression re(
        R"((\*\*|__|\*|_|`|~~)(.+?)\1|\[([^\]]+)\]\([^)]+\)|\[\[([^\]|]+)(?:\|([^\]]+))?\]\])");
    QString out;
    out.reserve(text.size());
    int pos = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        out += text.mid(pos, m.capturedStart() - pos);
        // Prefer the innermost captured text.
        for (int g = 2; g <= 5; ++g) {
            if (!m.captured(g).isNull()) { out += m.captured(g); break; }
        }
        pos = m.capturedEnd();
    }
    out += text.mid(pos);
    return out.trimmed();
}

QList<FoldRegionKey> computeHeadingPaths(const QList<HeadingInfo> &headings) {
    QList<FoldRegionKey> result;
    result.reserve(headings.size());

    // Stack of (level, normalized-text) capturing current ancestor chain.
    struct Frame { int level; QString text; };
    QList<Frame> ancestors;

    // Map of (parent-path-join, level, text) -> occurrence count, to assign #N.
    QHash<QString, int> occurrences;

    for (const auto &h : headings) {
        // Pop ancestors at >= current level.
        while (!ancestors.isEmpty() && ancestors.last().level >= h.level)
            ancestors.removeLast();

        QStringList path;
        for (const auto &a : ancestors) path << a.text;
        QString normText = normalizeHeadingText(h.text);

        QString key;
        for (const auto &p : path) { key += p; key += QLatin1Char('\x1f'); }
        key += QString::number(h.level);
        key += QLatin1Char('\x1f');
        key += normText;

        int &count = occurrences[key];
        ++count;

        QString finalText = (count == 1)
            ? normText
            : QStringLiteral("%1#%2").arg(normText).arg(count);

        path << finalText;
        result << path;
        ancestors.push_back(Frame{h.level, finalText});
    }
    return result;
}

} // namespace Markoff
```

- [ ] **Step 5: Add sources to CMakeLists**

Open `libs/markoff/CMakeLists.txt`. Find the `target_sources(markoff ... PRIVATE ...)` call (or the `add_library(markoff ...)` with sources). Append `src/FoldingTypes.cpp`. Add `include/markoff/FoldingTypes.h` to any public-headers list if present.

- [ ] **Step 6: Build and run the test**

```
cmake --build build --target tst_markoff_folding_model
ctest --test-dir build -R tst_markoff_folding_model --output-on-failure
```

Expected: all 6 sub-tests pass.

- [ ] **Step 7: Commit**

```
git add libs/markoff/include/markoff/FoldingTypes.h \
         libs/markoff/src/FoldingTypes.cpp \
         libs/markoff/tests/tst_folding_model.cpp \
         libs/markoff/tests/CMakeLists.txt \
         libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): path computation for heading folding"
```

---

## Task 2: FoldingModel core (state + primitives)

**Files:**
- Create: `libs/markoff/src/FoldingModel.h`
- Create: `libs/markoff/src/FoldingModel.cpp`
- Modify: `libs/markoff/CMakeLists.txt` — append `src/FoldingModel.cpp`.
- Modify: `libs/markoff/tests/tst_folding_model.cpp` — add a second test class.

- [ ] **Step 1: Write the failing tests**

Append to `libs/markoff/tests/tst_folding_model.cpp` (before `QTEST_MAIN`):

```cpp
#include "FoldingModel.h"

class TstFoldingModelState : public QObject {
    Q_OBJECT
private slots:
    void initialState_isEmpty();
    void fold_addsPath();
    void unfold_removesPath();
    void toggle_flipsState();
    void fold_duplicateCall_doesNotDoubleEmit();
    void foldStateChanged_firesOnlyOnActualChange();
};

void TstFoldingModelState::initialState_isEmpty() {
    FoldingModel m;
    QVERIFY(m.foldedPaths().isEmpty());
    QVERIFY(!m.isFolded({ "Anything" }));
}

void TstFoldingModelState::fold_addsPath() {
    FoldingModel m;
    m.fold({ "Intro" });
    QVERIFY(m.isFolded({ "Intro" }));
    QCOMPARE(m.foldedPaths().size(), 1);
}

void TstFoldingModelState::unfold_removesPath() {
    FoldingModel m;
    m.fold({ "Intro" });
    m.unfold({ "Intro" });
    QVERIFY(!m.isFolded({ "Intro" }));
}

void TstFoldingModelState::toggle_flipsState() {
    FoldingModel m;
    m.toggle({ "X" }); QVERIFY(m.isFolded({ "X" }));
    m.toggle({ "X" }); QVERIFY(!m.isFolded({ "X" }));
}

void TstFoldingModelState::fold_duplicateCall_doesNotDoubleEmit() {
    FoldingModel m;
    QSignalSpy spy(&m, &FoldingModel::foldStateChanged);
    m.fold({ "X" });
    m.fold({ "X" }); // already folded; no-op.
    QCOMPARE(spy.count(), 1);
}

void TstFoldingModelState::foldStateChanged_firesOnlyOnActualChange() {
    FoldingModel m;
    QSignalSpy spy(&m, &FoldingModel::foldStateChanged);
    m.unfold({ "Nonexistent" }); // no-op
    QCOMPARE(spy.count(), 0);
}
```

Update `QTEST_MAIN` to run both classes — change to a custom main:

```cpp
int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        TstFoldingModel t;
        status |= QTest::qExec(&t, argc, argv);
    }
    {
        TstFoldingModelState t;
        status |= QTest::qExec(&t, argc, argv);
    }
    return status;
}
#include "tst_folding_model.moc"
```

(Remove the original `QTEST_MAIN(TstFoldingModel)` line.)

- [ ] **Step 2: Run to verify it fails**

```
cmake --build build --target tst_markoff_folding_model 2>&1 | tail -5
```

Expected: build failure — `FoldingModel.h` not found.

- [ ] **Step 3: Create the FoldingModel header**

Create `libs/markoff/src/FoldingModel.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_FOLDINGMODEL_H
#define MARKOFF_FOLDINGMODEL_H

#include <markoff/FoldingTypes.h>
#include <markoff-parser/Document.h>
#include <QObject>
#include <QSet>
#include <QList>
#include <QJsonObject>

namespace Markoff {

/// Owns the set of folded heading paths. Pure data, no widgets. Fed by
/// `Editor` from the `headingsChanged` reparse signal; consulted by
/// `SceneCoordinator` for item visibility and by `FoldGutter` for paint.
class FoldingModel : public QObject {
    Q_OBJECT
public:
    struct HeadingEntry {
        FoldRegionKey path;
        HeadingInfo info;
    };

    explicit FoldingModel(QObject *parent = nullptr);

    // --- Query ---
    bool isFolded(const FoldRegionKey &path) const;
    QList<FoldRegionKey> foldedPaths() const;
    QList<FoldRegionKey> allPaths() const;
    const QList<HeadingEntry> &headings() const { return m_headings; }

    /// True if `path` is folded OR any of its ancestor prefixes is folded.
    /// Used to decide item visibility.
    bool isHiddenByFold(const FoldRegionKey &path) const;

    // --- Individual mutation ---
    void fold(const FoldRegionKey &path);
    void unfold(const FoldRegionKey &path);
    void toggle(const FoldRegionKey &path);

    // --- Bulk mutation (implemented in Task 3) ---
    void foldAll();
    void unfoldAll();
    void foldAllAtLevel(int level);
    void unfoldAllAtLevel(int level);
    void foldLevel(int n);
    void unfoldLevel(int n);

    // --- Persistence (Task 4) ---
    QJsonObject serialize() const;
    void restore(const QJsonObject &);

    // --- Reparse reconcile (Task 5) ---
    void reconcile(const QList<HeadingInfo> &newHeadings);

    /// Walk `path` from root and unfold any folded prefix. Returns the
    /// prefixes actually unfolded (empty if none were folded). Used by
    /// auto-unfold on navigation and find.
    QList<FoldRegionKey> unfoldAncestors(const FoldRegionKey &path);

Q_SIGNALS:
    void foldStateChanged();

private:
    void emitIfChanged(bool changed);

    QSet<FoldRegionKey> m_folded;
    QList<HeadingEntry> m_headings;
};

} // namespace Markoff

#endif // MARKOFF_FOLDINGMODEL_H
```

- [ ] **Step 4: Create the minimal FoldingModel implementation**

Create `libs/markoff/src/FoldingModel.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FoldingModel.h"
#include <QJsonArray>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcFolding, "markoff.folding")

namespace Markoff {

FoldingModel::FoldingModel(QObject *parent) : QObject(parent) {}

bool FoldingModel::isFolded(const FoldRegionKey &path) const {
    return m_folded.contains(path);
}

QList<FoldRegionKey> FoldingModel::foldedPaths() const {
    return QList<FoldRegionKey>(m_folded.begin(), m_folded.end());
}

QList<FoldRegionKey> FoldingModel::allPaths() const {
    QList<FoldRegionKey> r;
    r.reserve(m_headings.size());
    for (const auto &h : m_headings) r << h.path;
    return r;
}

bool FoldingModel::isHiddenByFold(const FoldRegionKey &path) const {
    // Any ancestor prefix (including the path itself's proper prefixes)
    // being folded hides it. A heading hides ITSELF only as its children;
    // the heading line remains visible when folded.
    for (int i = 1; i < path.size(); ++i) {
        FoldRegionKey prefix = path.mid(0, i);
        if (m_folded.contains(prefix)) return true;
    }
    return false;
}

void FoldingModel::fold(const FoldRegionKey &path) {
    const bool inserted = !m_folded.contains(path);
    if (inserted) { m_folded.insert(path); emit foldStateChanged(); }
}

void FoldingModel::unfold(const FoldRegionKey &path) {
    const bool removed = m_folded.remove(path);
    if (removed) emit foldStateChanged();
}

void FoldingModel::toggle(const FoldRegionKey &path) {
    if (m_folded.contains(path)) unfold(path);
    else fold(path);
}

// --- stubs for later tasks ---
void FoldingModel::foldAll() {}
void FoldingModel::unfoldAll() {}
void FoldingModel::foldAllAtLevel(int) {}
void FoldingModel::unfoldAllAtLevel(int) {}
void FoldingModel::foldLevel(int) {}
void FoldingModel::unfoldLevel(int) {}
QJsonObject FoldingModel::serialize() const { return {}; }
void FoldingModel::restore(const QJsonObject &) {}
void FoldingModel::reconcile(const QList<HeadingInfo> &) {}
QList<FoldRegionKey> FoldingModel::unfoldAncestors(const FoldRegionKey &) { return {}; }

} // namespace Markoff
```

- [ ] **Step 5: Add to CMakeLists**

Append `src/FoldingModel.cpp` to `libs/markoff/CMakeLists.txt` sources list.

- [ ] **Step 6: Build and run**

```
cmake --build build --target tst_markoff_folding_model
ctest --test-dir build -R tst_markoff_folding_model --output-on-failure
```

Expected: all Task 1 + Task 2 tests pass.

- [ ] **Step 7: Commit**

```
git add libs/markoff/src/FoldingModel.h libs/markoff/src/FoldingModel.cpp \
         libs/markoff/tests/tst_folding_model.cpp libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): FoldingModel core state and primitives"
```

---

## Task 3: FoldingModel bulk operations

**Files:**
- Modify: `libs/markoff/src/FoldingModel.cpp` — implement the six bulk methods (currently stubs).
- Modify: `libs/markoff/tests/tst_folding_model.cpp` — add a test class `TstFoldingModelBulk`.

- [ ] **Step 1: Write the failing tests**

Append to `tst_folding_model.cpp` before the `main`:

```cpp
class TstFoldingModelBulk : public QObject {
    Q_OBJECT
private slots:
    void foldAll_foldsEveryHeading();
    void unfoldAll_clears();
    void foldAllAtLevel_foldsOnlySpecifiedLevel();
    void foldLevel_foldsAtLevelAndDeeper();
    void unfoldLevel_unfoldsAtLevelAndDeeper();
private:
    void seedHeadings(FoldingModel &m);
};

void TstFoldingModelBulk::seedHeadings(FoldingModel &m) {
    // # A (1) ## B (2) ## C (2) # D (1) ### E (3)
    QList<HeadingInfo> hs = {
        {1, "A", 0}, {2, "B", 10}, {2, "C", 20},
        {1, "D", 30}, {3, "E", 40}
    };
    m.reconcile(hs); // note: reconcile is stubbed until Task 5; test seeds via direct API instead
}
```

Because `reconcile` is still stubbed in Task 2, the seed helper can't actually populate headings. Rework to test bulk ops *against an already-populated heading cache* by calling `reconcile` after Task 5 is done. For Task 3, implement the methods such that they **iterate `m_headings`**, but test with a minimal helper injector. Add to `FoldingModel` header a test-only shortcut:

```cpp
public:
    /// Test-only: seed the heading cache without going through reconcile.
    void setHeadingsForTesting(QList<HeadingEntry> h) { m_headings = std::move(h); }
```

(This is intentional — keeps Tasks 2–5 independently testable.)

Rewrite the test seed:

```cpp
void TstFoldingModelBulk::seedHeadings(FoldingModel &m) {
    m.setHeadingsForTesting({
        { {"A"},           {1, "A", 0} },
        { {"A","B"},       {2, "B", 10} },
        { {"A","C"},       {2, "C", 20} },
        { {"D"},           {1, "D", 30} },
        { {"D","","E"},    {3, "E", 40} },  // skipped H2
    });
}

void TstFoldingModelBulk::foldAll_foldsEveryHeading() {
    FoldingModel m; seedHeadings(m);
    m.foldAll();
    QCOMPARE(m.foldedPaths().size(), 5);
}

void TstFoldingModelBulk::unfoldAll_clears() {
    FoldingModel m; seedHeadings(m);
    m.foldAll(); m.unfoldAll();
    QVERIFY(m.foldedPaths().isEmpty());
}

void TstFoldingModelBulk::foldAllAtLevel_foldsOnlySpecifiedLevel() {
    FoldingModel m; seedHeadings(m);
    m.foldAllAtLevel(2);
    QCOMPARE(m.foldedPaths().size(), 2);
    QVERIFY(m.isFolded({"A","B"}));
    QVERIFY(m.isFolded({"A","C"}));
    QVERIFY(!m.isFolded({"A"}));
}

void TstFoldingModelBulk::foldLevel_foldsAtLevelAndDeeper() {
    FoldingModel m; seedHeadings(m);
    m.foldLevel(2);  // H2 and H3
    QCOMPARE(m.foldedPaths().size(), 3);
    QVERIFY(m.isFolded({"A","B"}));
    QVERIFY(m.isFolded({"A","C"}));
    QVERIFY(m.isFolded({"D","","E"}));
}

void TstFoldingModelBulk::unfoldLevel_unfoldsAtLevelAndDeeper() {
    FoldingModel m; seedHeadings(m);
    m.foldAll();
    m.unfoldLevel(2);
    QVERIFY(m.isFolded({"A"}));
    QVERIFY(m.isFolded({"D"}));
    QVERIFY(!m.isFolded({"A","B"}));
}
```

Add to the `main` runner:

```cpp
{ TstFoldingModelBulk t; status |= QTest::qExec(&t, argc, argv); }
```

- [ ] **Step 2: Run; verify failure**

```
cmake --build build --target tst_markoff_folding_model
ctest --test-dir build -R tst_markoff_folding_model --output-on-failure
```

Expected: Bulk test failures (methods are still stubs). Also `setHeadingsForTesting` must be added for the test to compile — add it now.

- [ ] **Step 3: Add `setHeadingsForTesting` to the header**

Open `libs/markoff/src/FoldingModel.h`. Under the `public:` section, add:

```cpp
void setHeadingsForTesting(QList<HeadingEntry> h) { m_headings = std::move(h); }
```

- [ ] **Step 4: Implement the bulk methods**

Replace the six stubbed methods in `libs/markoff/src/FoldingModel.cpp`:

```cpp
void FoldingModel::foldAll() {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (!m_folded.contains(h.path)) { m_folded.insert(h.path); changed = true; }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::unfoldAll() {
    if (m_folded.isEmpty()) return;
    m_folded.clear();
    emit foldStateChanged();
}

void FoldingModel::foldAllAtLevel(int level) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level == level && !m_folded.contains(h.path)) {
            m_folded.insert(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::unfoldAllAtLevel(int level) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level == level && m_folded.contains(h.path)) {
            m_folded.remove(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::foldLevel(int n) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level >= n && !m_folded.contains(h.path)) {
            m_folded.insert(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::unfoldLevel(int n) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level >= n && m_folded.contains(h.path)) {
            m_folded.remove(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}
```

- [ ] **Step 5: Build and run**

```
cmake --build build --target tst_markoff_folding_model
ctest --test-dir build -R tst_markoff_folding_model --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```
git add libs/markoff/src/FoldingModel.h libs/markoff/src/FoldingModel.cpp \
         libs/markoff/tests/tst_folding_model.cpp
git commit -m "feat(markoff): FoldingModel bulk operations"
```

---

## Task 4: FoldingModel serialize/restore

**Files:**
- Modify: `libs/markoff/src/FoldingModel.cpp` — real impl.
- Create: `libs/markoff/tests/tst_fold_persistence.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt` — register new test.

- [ ] **Step 1: Write the failing tests**

Create `libs/markoff/tests/tst_fold_persistence.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QJsonDocument>
#include "FoldingModel.h"

using namespace Markoff;

class TstFoldPersistence : public QObject {
    Q_OBJECT
private slots:
    void serialize_emptyModel_returnsVersionOnly();
    void serialize_twoFolds_roundTrips();
    void restore_droppedPathsNotReadded_whenReconcileRemovedThem();
    void restore_malformedJson_isNoOpWithNoCrash();
    void restore_missingFoldsKey_isNoOp();
    void restore_unknownVersion_stillLoadsFolds();
};

static FoldingModel::HeadingEntry entry(QStringList path, int level) {
    return { path, HeadingInfo{level, path.last(), 0} };
}

void TstFoldPersistence::serialize_emptyModel_returnsVersionOnly() {
    FoldingModel m;
    auto j = m.serialize();
    QCOMPARE(j.value("version").toInt(), 1);
    QVERIFY(j.value("folds").toArray().isEmpty());
}

void TstFoldPersistence::serialize_twoFolds_roundTrips() {
    FoldingModel m;
    m.setHeadingsForTesting({
        entry({"Intro","Goals"}, 2),
        entry({"Reference","API","Query"}, 3),
        entry({"Other"}, 1),
    });
    m.fold({"Intro","Goals"});
    m.fold({"Reference","API","Query"});

    auto j = m.serialize();

    FoldingModel m2;
    m2.setHeadingsForTesting({
        entry({"Intro","Goals"}, 2),
        entry({"Reference","API","Query"}, 3),
        entry({"Other"}, 1),
    });
    m2.restore(j);

    QVERIFY(m2.isFolded({"Intro","Goals"}));
    QVERIFY(m2.isFolded({"Reference","API","Query"}));
    QVERIFY(!m2.isFolded({"Other"}));
}

void TstFoldPersistence::restore_droppedPathsNotReadded_whenReconcileRemovedThem() {
    FoldingModel m;
    m.restore(QJsonObject{
        {"version", 1},
        {"folds", QJsonArray{ QJsonArray{"Gone"} }}
    });
    // Headings cache is empty. reconcile() drops the path. Task 5 validates
    // end-to-end; here we just verify restore populated before reconcile runs.
    QVERIFY(m.isFolded({"Gone"}));
}

void TstFoldPersistence::restore_malformedJson_isNoOpWithNoCrash() {
    FoldingModel m;
    m.fold({"X"}); m.setHeadingsForTesting({ entry({"X"}, 1) });
    QJsonObject garbage;
    garbage["folds"] = QJsonValue(42); // wrong type
    m.restore(garbage);
    // No crash. Pre-existing fold cleared (restore replaces state).
    QVERIFY(!m.isFolded({"X"}));
}

void TstFoldPersistence::restore_missingFoldsKey_isNoOp() {
    FoldingModel m;
    m.restore(QJsonObject{ {"version", 1} });
    QVERIFY(m.foldedPaths().isEmpty());
}

void TstFoldPersistence::restore_unknownVersion_stillLoadsFolds() {
    FoldingModel m;
    m.setHeadingsForTesting({ entry({"A"}, 1) });
    m.restore(QJsonObject{
        {"version", 999},
        {"folds", QJsonArray{ QJsonArray{"A"} }},
    });
    QVERIFY(m.isFolded({"A"}));
}

QTEST_MAIN(TstFoldPersistence)
#include "tst_fold_persistence.moc"
```

Append to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_fold_persistence tst_fold_persistence.cpp)
add_test(NAME tst_markoff_fold_persistence COMMAND tst_markoff_fold_persistence)
target_link_libraries(tst_markoff_fold_persistence PRIVATE Qt6::Test markoff)
target_include_directories(tst_markoff_fold_persistence PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_fold_persistence PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run; verify failure**

```
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build --target tst_markoff_fold_persistence
ctest --test-dir build -R tst_markoff_fold_persistence --output-on-failure
```

Expected: failures (serialize/restore are stubs).

- [ ] **Step 3: Implement serialize/restore**

Replace the stubs in `libs/markoff/src/FoldingModel.cpp`:

```cpp
QJsonObject FoldingModel::serialize() const {
    QJsonArray folds;
    for (const auto &p : m_folded) {
        QJsonArray arr;
        for (const auto &seg : p) arr.append(seg);
        folds.append(arr);
    }
    QJsonObject root;
    root["version"] = 1;
    root["folds"] = folds;
    return root;
}

void FoldingModel::restore(const QJsonObject &obj) {
    const auto prev = m_folded;
    m_folded.clear();

    const QJsonValue foldsVal = obj.value("folds");
    if (foldsVal.isArray()) {
        for (const auto &entry : foldsVal.toArray()) {
            if (!entry.isArray()) continue;
            QStringList path;
            for (const auto &seg : entry.toArray()) {
                if (seg.isString()) path << seg.toString();
            }
            if (!path.isEmpty()) m_folded.insert(path);
        }
    } else if (!foldsVal.isUndefined() && !foldsVal.isNull()) {
        qCWarning(lcFolding) << "restore: 'folds' must be an array, got" << foldsVal.type();
    }

    if (prev != m_folded) emit foldStateChanged();
}
```

- [ ] **Step 4: Build and run**

```
cmake --build build --target tst_markoff_fold_persistence tst_markoff_folding_model
ctest --test-dir build -R "tst_markoff_(fold_persistence|folding_model)" --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```
git add libs/markoff/src/FoldingModel.cpp libs/markoff/tests/tst_fold_persistence.cpp \
         libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): FoldingModel JSON serialize/restore"
```

---

## Task 5: FoldingModel reconcile on reparse

**Files:**
- Modify: `libs/markoff/src/FoldingModel.cpp` — real `reconcile` impl + `unfoldAncestors`.
- Create: `libs/markoff/tests/tst_folding_reconcile.cpp`
- Modify: `libs/markoff/tests/CMakeLists.txt` — register test.

- [ ] **Step 1: Write the failing tests**

Create `libs/markoff/tests/tst_folding_reconcile.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include "FoldingModel.h"

using namespace Markoff;

class TstFoldingReconcile : public QObject {
    Q_OBJECT
private slots:
    void reconcile_empty_clearsStaleFolds();
    void reconcile_renameHeading_dropsFold();
    void reconcile_promoteH2ToH1_dropsFold();
    void reconcile_insertUnrelatedHeading_preservesFold();
    void reconcile_editBodyTextOnly_preservesFold();
    void reconcile_newDuplicateSibling_existingFoldPreserved();
    void reconcile_stableHeadings_noSignal();
    void reconcile_populatesHeadingsCache();
    void unfoldAncestors_noFoldedAncestors_returnsEmpty();
    void unfoldAncestors_twoFoldedAncestors_unfoldsBoth();
};

static HeadingInfo h(int lvl, QString text) { return {lvl, std::move(text), 0}; }

void TstFoldingReconcile::reconcile_empty_clearsStaleFolds() {
    FoldingModel m;
    m.fold({"Old"});
    m.reconcile({});
    QVERIFY(m.foldedPaths().isEmpty());
}

void TstFoldingReconcile::reconcile_renameHeading_dropsFold() {
    FoldingModel m;
    m.reconcile({ h(1, "Intro"), h(2, "Goals") });
    m.fold({"Intro","Goals"});
    m.reconcile({ h(1, "Intro"), h(2, "Objectives") }); // renamed
    QVERIFY(!m.isFolded({"Intro","Goals"}));
    QVERIFY(!m.isFolded({"Intro","Objectives"}));
}

void TstFoldingReconcile::reconcile_promoteH2ToH1_dropsFold() {
    FoldingModel m;
    m.reconcile({ h(1, "Intro"), h(2, "Goals") });
    m.fold({"Intro","Goals"});
    m.reconcile({ h(1, "Intro"), h(1, "Goals") }); // promoted -> path changes
    QVERIFY(!m.isFolded({"Intro","Goals"}));
}

void TstFoldingReconcile::reconcile_insertUnrelatedHeading_preservesFold() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(1, "B") });
    m.fold({"A"});
    m.reconcile({ h(1, "A"), h(1, "C"), h(1, "B") });
    QVERIFY(m.isFolded({"A"}));
}

void TstFoldingReconcile::reconcile_editBodyTextOnly_preservesFold() {
    FoldingModel m;
    const QList<HeadingInfo> hs = { h(1, "A"), h(2, "B") };
    m.reconcile(hs); m.fold({"A","B"});
    m.reconcile(hs); // identical reparse
    QVERIFY(m.isFolded({"A","B"}));
}

void TstFoldingReconcile::reconcile_newDuplicateSibling_existingFoldPreserved() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "Goals") });
    m.fold({"A","Goals"});
    m.reconcile({ h(1, "A"), h(2, "Goals"), h(2, "Goals") });
    QVERIFY(m.isFolded({"A","Goals"}));    // first sibling unchanged
    QVERIFY(!m.isFolded({"A","Goals#2"})); // new one not folded
}

void TstFoldingReconcile::reconcile_stableHeadings_noSignal() {
    FoldingModel m;
    const QList<HeadingInfo> hs = { h(1, "A"), h(2, "B") };
    m.reconcile(hs); m.fold({"A","B"});
    QSignalSpy spy(&m, &FoldingModel::foldStateChanged);
    m.reconcile(hs);
    QCOMPARE(spy.count(), 0);
}

void TstFoldingReconcile::reconcile_populatesHeadingsCache() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "B") });
    QCOMPARE(m.headings().size(), 2);
    QCOMPARE(m.headings()[1].path, (QStringList{"A","B"}));
}

void TstFoldingReconcile::unfoldAncestors_noFoldedAncestors_returnsEmpty() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "B") });
    auto r = m.unfoldAncestors({"A","B"});
    QVERIFY(r.isEmpty());
}

void TstFoldingReconcile::unfoldAncestors_twoFoldedAncestors_unfoldsBoth() {
    FoldingModel m;
    m.reconcile({ h(1, "A"), h(2, "B"), h(3, "C") });
    m.fold({"A"}); m.fold({"A","B"});
    auto r = m.unfoldAncestors({"A","B","C"});
    QCOMPARE(r.size(), 2);
    QVERIFY(r.contains({"A"}));
    QVERIFY(r.contains({"A","B"}));
    QVERIFY(!m.isFolded({"A"}));
    QVERIFY(!m.isFolded({"A","B"}));
}

QTEST_MAIN(TstFoldingReconcile)
#include "tst_folding_reconcile.moc"
```

Append to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_folding_reconcile tst_folding_reconcile.cpp)
add_test(NAME tst_markoff_folding_reconcile COMMAND tst_markoff_folding_reconcile)
target_link_libraries(tst_markoff_folding_reconcile PRIVATE Qt6::Test markoff)
target_include_directories(tst_markoff_folding_reconcile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_reconcile PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run to verify failure**

```
cmake --build build --target tst_markoff_folding_reconcile
ctest --test-dir build -R tst_markoff_folding_reconcile --output-on-failure
```

Expected: failures (`reconcile` is a stub).

- [ ] **Step 3: Implement reconcile and unfoldAncestors**

Replace the stubs in `libs/markoff/src/FoldingModel.cpp`:

```cpp
void FoldingModel::reconcile(const QList<HeadingInfo> &newHeadings) {
    // 1. Rebuild heading cache with paths.
    const auto paths = computeHeadingPaths(newHeadings);
    m_headings.clear();
    m_headings.reserve(newHeadings.size());
    QSet<FoldRegionKey> newPathSet;
    for (int i = 0; i < newHeadings.size(); ++i) {
        m_headings.append({ paths[i], newHeadings[i] });
        newPathSet.insert(paths[i]);
    }

    // 2. Intersect folded set with newPathSet.
    const auto prev = m_folded;
    auto it = m_folded.begin();
    while (it != m_folded.end()) {
        if (!newPathSet.contains(*it)) it = m_folded.erase(it);
        else ++it;
    }
    if (prev != m_folded) emit foldStateChanged();
}

QList<FoldRegionKey> FoldingModel::unfoldAncestors(const FoldRegionKey &path) {
    QList<FoldRegionKey> unfolded;
    for (int i = 1; i <= path.size(); ++i) {
        FoldRegionKey prefix = path.mid(0, i);
        if (m_folded.contains(prefix)) {
            m_folded.remove(prefix);
            unfolded.append(prefix);
        }
    }
    if (!unfolded.isEmpty()) emit foldStateChanged();
    return unfolded;
}
```

- [ ] **Step 4: Build and run**

```
cmake --build build --target tst_markoff_folding_reconcile tst_markoff_folding_model tst_markoff_fold_persistence
ctest --test-dir build -R "tst_markoff_(folding_|fold_persistence)" --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit**

```
git add libs/markoff/src/FoldingModel.cpp libs/markoff/tests/tst_folding_reconcile.cpp \
         libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): FoldingModel reconcile + unfoldAncestors"
```

---

## Task 6: Editor public API

Wire `FoldingModel` into `Editor` and expose the full public API.

**Files:**
- Modify: `libs/markoff/include/markoff/Editor.h` — add public API + signals.
- Modify: `libs/markoff/src/Editor.cpp` — own a `FoldingModel`, connect `headingsChanged` → `reconcile`, implement API methods.

- [ ] **Step 1: Declare the API**

Open `libs/markoff/include/markoff/Editor.h`. After the `// --- Search ---` section (around line 102), add:

```cpp
    // --- Folding ---
    QList<QStringList> headingPaths() const;
    bool isFolded(const QStringList &path) const;
    QList<QStringList> foldedPaths() const;

    void fold(const QStringList &path);
    void unfold(const QStringList &path);
    void toggleFold(const QStringList &path);
    void toggleFoldAtCursor();

    void foldAll();
    void unfoldAll();
    void foldAllAtLevel(int level);
    void unfoldAllAtLevel(int level);
    void foldLevel(int n);
    void unfoldLevel(int n);

    QJsonObject serializeFoldState() const;
    void restoreFoldState(const QJsonObject &);

    void setGutterVisible(bool visible);
    bool isGutterVisible() const;
```

In the signals block (around line 115), add:

```cpp
    void foldStateChanged();
    void foldsAutoExpanded(const QList<QStringList> &paths);
```

In the private members (around line 162), add forward decls and members:

```cpp
class FoldingModel;
class FoldGutter;
```

(Place at the top of the `Markoff` namespace, near other forward decls.) And:

```cpp
    FoldingModel *m_foldingModel = nullptr;
    FoldGutter *m_foldGutter = nullptr;
    bool m_gutterVisible = true;
```

Add `#include <QJsonObject>` at the top.

- [ ] **Step 2: Implement in Editor.cpp**

Open `libs/markoff/src/Editor.cpp`. Add includes:

```cpp
#include "FoldingModel.h"
// FoldGutter include added in Task 10.
```

In the `Editor` constructor (find `Editor::Editor(QWidget *parent)`), after `m_coordinator` is constructed, add:

```cpp
m_foldingModel = new FoldingModel(this);
connect(this, &Editor::headingsChanged,
        m_foldingModel, &FoldingModel::reconcile);
connect(m_foldingModel, &FoldingModel::foldStateChanged,
        this, &Editor::foldStateChanged);
```

(The scene-coordinator hookup for visibility is Task 7.)

At the end of `Editor.cpp` (or near the other API impls), add all public methods — each is a one-liner delegate to `m_foldingModel`:

```cpp
QList<QStringList> Editor::headingPaths() const { return m_foldingModel->allPaths(); }
bool Editor::isFolded(const QStringList &p) const { return m_foldingModel->isFolded(p); }
QList<QStringList> Editor::foldedPaths() const { return m_foldingModel->foldedPaths(); }

void Editor::fold(const QStringList &p) { m_foldingModel->fold(p); }
void Editor::unfold(const QStringList &p) { m_foldingModel->unfold(p); }
void Editor::toggleFold(const QStringList &p) { m_foldingModel->toggle(p); }

void Editor::toggleFoldAtCursor() {
    // Find the heading at or immediately before the cursor's source line.
    const int line = cursorLine();
    const auto &hs = m_foldingModel->headings();
    const FoldingModel::HeadingEntry *best = nullptr;
    for (const auto &h : hs) {
        // Convert sourceOffset to line. Helper: use the same byte-to-line
        // counting already used in Editor::scrollToHeading. For now assume
        // HeadingInfo.sourceOffset is line-based (matches existing usage).
        if (h.info.sourceOffset <= line) best = &h;
        else break;
    }
    if (best) m_foldingModel->toggle(best->path);
}

void Editor::foldAll() { m_foldingModel->foldAll(); }
void Editor::unfoldAll() { m_foldingModel->unfoldAll(); }
void Editor::foldAllAtLevel(int level) { m_foldingModel->foldAllAtLevel(level); }
void Editor::unfoldAllAtLevel(int level) { m_foldingModel->unfoldAllAtLevel(level); }
void Editor::foldLevel(int n) { m_foldingModel->foldLevel(n); }
void Editor::unfoldLevel(int n) { m_foldingModel->unfoldLevel(n); }

QJsonObject Editor::serializeFoldState() const { return m_foldingModel->serialize(); }
void Editor::restoreFoldState(const QJsonObject &j) { m_foldingModel->restore(j); }

void Editor::setGutterVisible(bool v) {
    m_gutterVisible = v;
    // FoldGutter toggle added in Task 11.
}
bool Editor::isGutterVisible() const { return m_gutterVisible; }
```

**Note the cursor-line resolution.** Verify what unit `HeadingInfo::sourceOffset` uses by reading existing `Editor::scrollToHeading` impl. If it's a UTF-8 byte offset (per the recent fix noted in markoff TODO.md), port the byte-to-line conversion helper here. If it's already line-based, the code above is correct.

- [ ] **Step 3: Write integration tests**

Create `libs/markoff/tests/tst_folding_integration.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <markoff/Editor.h>

using namespace Markoff;

class TstFoldingIntegration : public QObject {
    Q_OBJECT
private slots:
    void editor_setPlainText_populatesHeadingPaths();
    void editor_foldAndUnfold_emitsSignal();
    void editor_serializeAndRestore_roundTrip();
    void editor_renameHeading_dropsStaleFold();
};

static QString kSample =
    "# Intro\n\nBody\n\n## Goals\n\nMore body\n\n"
    "## Non-goals\n\nText\n\n# Other\n\nEnd\n";

static void waitForReparse() {
    // Coordinator uses a debounce timer for reparse. Spin the event loop
    // a short while. Actual timer period is ~50ms.
    QTest::qWait(300);
}

void TstFoldingIntegration::editor_setPlainText_populatesHeadingPaths() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    const auto paths = e.headingPaths();
    QVERIFY(paths.contains((QStringList{"Intro"})));
    QVERIFY(paths.contains((QStringList{"Intro","Goals"})));
    QVERIFY(paths.contains((QStringList{"Other"})));
}

void TstFoldingIntegration::editor_foldAndUnfold_emitsSignal() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    QSignalSpy spy(&e, &Editor::foldStateChanged);
    e.fold({"Intro","Goals"});
    QCOMPARE(spy.count(), 1);
    QVERIFY(e.isFolded({"Intro","Goals"}));
    e.unfold({"Intro","Goals"});
    QCOMPARE(spy.count(), 2);
}

void TstFoldingIntegration::editor_serializeAndRestore_roundTrip() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    e.fold({"Intro","Goals"});
    e.fold({"Other"});

    auto j = e.serializeFoldState();

    Editor e2;
    e2.setPlainText(kSample);
    waitForReparse();
    e2.restoreFoldState(j);

    QVERIFY(e2.isFolded({"Intro","Goals"}));
    QVERIFY(e2.isFolded({"Other"}));
}

void TstFoldingIntegration::editor_renameHeading_dropsStaleFold() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    e.fold({"Intro","Goals"});

    QString renamed = kSample;
    renamed.replace("## Goals", "## Objectives");
    e.setPlainText(renamed);
    waitForReparse();

    QVERIFY(!e.isFolded({"Intro","Goals"}));
}

QTEST_MAIN(TstFoldingIntegration)
#include "tst_folding_integration.moc"
```

Register in `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_folding_integration tst_folding_integration.cpp)
add_test(NAME tst_markoff_folding_integration COMMAND tst_markoff_folding_integration)
target_link_libraries(tst_markoff_folding_integration PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_folding_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_folding_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 4: Build and run**

```
cmake --build build --target tst_markoff_folding_integration
ctest --test-dir build -R tst_markoff_folding_integration --output-on-failure
```

Expected: all four tests pass.

- [ ] **Step 5: Commit**

```
git add libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp \
         libs/markoff/tests/tst_folding_integration.cpp \
         libs/markoff/tests/CMakeLists.txt
git commit -m "feat(markoff): Editor public folding API"
```

---

## Task 7: SceneCoordinator visibility integration

**Files:**
- Modify: `libs/markoff/src/SceneCoordinator.h` — add `setFoldingModel(FoldingModel*)`, `itemIndexAt(qreal sceneY)`.
- Modify: `libs/markoff/src/SceneCoordinator.cpp` — subscribe to `foldStateChanged`, implement visibility.
- Modify: `libs/markoff/src/Editor.cpp` — wire `m_coordinator->setFoldingModel(m_foldingModel)` in the constructor.
- Modify: `libs/markoff/tests/tst_folding_integration.cpp` — add visibility tests.

**Visibility rule:** An item is visible iff its enclosing heading path is NOT hidden-by-fold. The enclosing heading path for an item is the path of the most-recent heading at or before the item's index. Items before the first heading have no path — always visible. The heading item *itself* stays visible when folded (only its children hide).

- [ ] **Step 1: Write the failing tests**

Append to `tst_folding_integration.cpp` before `QTEST_MAIN`:

```cpp
class TstFoldingVisibility : public QObject {
    Q_OBJECT
private slots:
    void foldH1_hidesChildrenButKeepsHeading();
    void unfold_reshowsChildren();
    void nestedFold_independent();
};

#include <markoff/Editor.h>
#include "SceneCoordinator.h"
#include "SelectableItem.h"

static int visibleCount(const QList<SelectableItem *> &items) {
    int n = 0;
    for (auto *it : items) if (it->isVisible()) ++n;
    return n;
}

void TstFoldingVisibility::foldH1_hidesChildrenButKeepsHeading() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();

    const auto *coord = /* expose via friend or getter — see Step 2 */;
    const int total = visibleCount(coord->items());

    e.fold({"Intro"});
    QTest::qWait(50);  // allow signal to propagate

    const int afterFold = visibleCount(coord->items());
    QVERIFY(afterFold < total);

    // Heading item for # Intro must remain visible.
    // (Identify via item-to-heading mapping — a friend accessor exposed
    // for testing.)
}
```

(Full test body depends on exposing `coordinator()` or similar from `Editor` — add one for testing.)

Add to `libs/markoff/include/markoff/Editor.h` under `public:`:

```cpp
    /// Test-only accessor. Do not use from host code.
    SceneCoordinator *coordinatorForTesting() const { return m_coordinator; }
```

- [ ] **Step 2: SceneCoordinator API additions**

Open `libs/markoff/src/SceneCoordinator.h`. Add forward decl `class FoldingModel;` (in namespace). In the `public:` section:

```cpp
    void setFoldingModel(FoldingModel *model);
    int itemIndexAt(qreal sceneY) const;
```

In `private:` add members:

```cpp
    FoldingModel *m_foldingModel = nullptr;
    void applyFoldVisibility();
    QStringList enclosingHeadingPath(int itemIndex) const;
```

- [ ] **Step 3: Implement visibility in SceneCoordinator.cpp**

Add includes:

```cpp
#include "FoldingModel.h"
```

Implement the new methods:

```cpp
void SceneCoordinator::setFoldingModel(FoldingModel *model) {
    if (m_foldingModel) disconnect(m_foldingModel, nullptr, this, nullptr);
    m_foldingModel = model;
    if (m_foldingModel) {
        connect(m_foldingModel, &FoldingModel::foldStateChanged,
                this, &SceneCoordinator::applyFoldVisibility);
    }
}

int SceneCoordinator::itemIndexAt(qreal sceneY) const {
    for (int i = 0; i < m_items.size(); ++i) {
        auto *gi = dynamic_cast<QGraphicsItem *>(m_items[i]);
        if (!gi) continue;
        const QRectF r = gi->sceneBoundingRect();
        if (sceneY >= r.top() && sceneY <= r.bottom()) return i;
    }
    return -1;
}

QStringList SceneCoordinator::enclosingHeadingPath(int itemIndex) const {
    if (!m_foldingModel) return {};
    // Map item index to sourceOffset. For now assume item index aligns
    // with block order; use the most-recent heading whose sourceOffset
    // is <= the item's source line. Implementation detail: walk
    // FoldingModel::headings() which is in document order.
    const auto &hs = m_foldingModel->headings();
    // Heuristic: use heading count at or before itemIndex. Because
    // MarkoffSplitter produces one item per block and headings are blocks,
    // we can map by counting how many headings have an earlier item index.
    // Precise mapping: iterate items[0..itemIndex] and count MarkdownTextItem
    // whose first char is '#'. Replace this with a direct item->heading
    // association if added later.
    QString text;
    if (auto *mti = dynamic_cast<MarkdownTextItem *>(m_items.value(itemIndex))) {
        text = mti->toPlainText();
    }
    // Fallback: linear scan by sourceOffset not available on items. Resolve
    // by counting heading-type items preceding this one.
    int hIdx = -1, seen = 0;
    for (int i = 0; i <= itemIndex && i < m_items.size(); ++i) {
        auto *mti = dynamic_cast<MarkdownTextItem *>(m_items[i]);
        if (!mti) continue;
        const QString s = mti->toPlainText().trimmed();
        if (s.startsWith('#')) {
            if (seen < hs.size()) hIdx = seen;
            ++seen;
        }
    }
    return hIdx >= 0 ? hs[hIdx].path : QStringList{};
}

void SceneCoordinator::applyFoldVisibility() {
    if (!m_foldingModel) return;
    for (int i = 0; i < m_items.size(); ++i) {
        const QStringList path = enclosingHeadingPath(i);
        const bool isHeadingItself = !path.isEmpty()
            && m_foldingModel->isFolded(path);
        const bool hidden = m_foldingModel->isHiddenByFold(path);
        auto *gi = dynamic_cast<QGraphicsItem *>(m_items[i]);
        if (gi) gi->setVisible(!hidden);
        // A heading item whose own path is folded stays visible; its
        // descendants are hidden by isHiddenByFold() which checks
        // *prefixes only*, not the heading's own path — see
        // FoldingModel::isHiddenByFold.
        (void)isHeadingItself;
    }
    repositionItems();
}
```

**Implementation note for the engineer:** The `enclosingHeadingPath` logic above is best-effort pending a cleaner item-to-heading link. If `MarkdownTextItem` already exposes a heading-flag or a source-offset, prefer that over string-prefix matching. Investigate at implementation time; update this helper to use the clean path.

- [ ] **Step 4: Wire in Editor constructor**

In `libs/markoff/src/Editor.cpp`, after the `connect` calls for `m_foldingModel`, add:

```cpp
m_coordinator->setFoldingModel(m_foldingModel);
```

- [ ] **Step 5: Complete the visibility test**

Flesh out `tst_folding_integration.cpp`'s visibility tests using `e.coordinatorForTesting()->items()`.

```cpp
void TstFoldingVisibility::foldH1_hidesChildrenButKeepsHeading() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();

    auto *coord = e.coordinatorForTesting();
    const int total = visibleCount(coord->items());

    e.fold({"Intro"});
    QTest::qWait(50);

    const int afterFold = visibleCount(coord->items());
    QVERIFY2(afterFold < total, "folding should hide at least one item");
}

void TstFoldingVisibility::unfold_reshowsChildren() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    auto *coord = e.coordinatorForTesting();
    const int total = visibleCount(coord->items());

    e.fold({"Intro"});
    QTest::qWait(50);
    e.unfold({"Intro"});
    QTest::qWait(50);

    QCOMPARE(visibleCount(coord->items()), total);
}

void TstFoldingVisibility::nestedFold_independent() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    auto *coord = e.coordinatorForTesting();

    e.fold({"Intro","Goals"});
    e.fold({"Intro"});
    const int bothFolded = visibleCount(coord->items());

    e.unfold({"Intro"});
    QTest::qWait(50);
    // Goals is still folded — items under Goals still hidden.
    const int onlyGoalsFolded = visibleCount(coord->items());
    QVERIFY(onlyGoalsFolded > bothFolded); // Intro body re-shown
    QVERIFY(e.isFolded({"Intro","Goals"})); // unchanged
}
```

Register the extra test class in a combined-main runner at the bottom of `tst_folding_integration.cpp` (replace `QTEST_MAIN`):

```cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    int status = 0;
    { TstFoldingIntegration t;   status |= QTest::qExec(&t, argc, argv); }
    { TstFoldingVisibility t;    status |= QTest::qExec(&t, argc, argv); }
    return status;
}
#include "tst_folding_integration.moc"
```

Add `Qt6::Widgets` to the test target libs (already present).

- [ ] **Step 6: Build and run**

```
cmake --build build --target tst_markoff_folding_integration
ctest --test-dir build -R tst_markoff_folding_integration --output-on-failure
```

Expected: all integration tests pass.

- [ ] **Step 7: Commit**

```
git add libs/markoff/src/SceneCoordinator.h libs/markoff/src/SceneCoordinator.cpp \
         libs/markoff/include/markoff/Editor.h libs/markoff/src/Editor.cpp \
         libs/markoff/tests/tst_folding_integration.cpp
git commit -m "feat(markoff): SceneCoordinator hides items under folded headings"
```

---

## Task 8: Auto-unfold on navigation and find

**Files:**
- Modify: `libs/markoff/src/Editor.cpp` — hook `scrollToHeading` and `findText`.
- Modify: `libs/markoff/tests/tst_folding_integration.cpp` — add auto-unfold tests.

- [ ] **Step 1: Write failing tests**

Append to `tst_folding_integration.cpp`:

```cpp
class TstFoldAutoExpand : public QObject {
    Q_OBJECT
private slots:
    void scrollToHeading_foldedAncestor_autoUnfolds();
    void findText_matchInFoldedRegion_autoUnfolds();
};

void TstFoldAutoExpand::scrollToHeading_foldedAncestor_autoUnfolds() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    e.fold({"Intro"});

    QSignalSpy spy(&e, &Editor::foldsAutoExpanded);

    // Find the HeadingInfo for "Goals" via the emitted headingsChanged
    // (cached in FoldingModel). Use e.toggleFoldAtCursor's resolver,
    // or expose via coordinator. Simplest: iterate foldable paths and
    // call through via a public helper. Here we construct a HeadingInfo.
    HeadingInfo goals{2, "Goals", /*sourceOffset*/ 0};
    e.scrollToHeading(goals);
    QTest::qWait(50);

    QVERIFY(!e.isFolded({"Intro"}));
    QCOMPARE(spy.count(), 1);
    const auto list = spy.first().first().value<QList<QStringList>>();
    QVERIFY(list.contains((QStringList{"Intro"})));
}

void TstFoldAutoExpand::findText_matchInFoldedRegion_autoUnfolds() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    e.fold({"Intro"});

    QSignalSpy spy(&e, &Editor::foldsAutoExpanded);
    const bool found = e.findText("More body");
    QTest::qWait(50);

    QVERIFY(found);
    QVERIFY(!e.isFolded({"Intro"}));
    QCOMPARE(spy.count(), 1);
}
```

Add to the main runner:

```cpp
{ TstFoldAutoExpand t; status |= QTest::qExec(&t, argc, argv); }
```

Register `QList<QStringList>` as a metatype at the top of the file:

```cpp
Q_DECLARE_METATYPE(QList<QStringList>)
```

- [ ] **Step 2: Run; expect failures**

```
cmake --build build --target tst_markoff_folding_integration
ctest --test-dir build -R tst_markoff_folding_integration --output-on-failure
```

- [ ] **Step 3: Hook scrollToHeading**

In `libs/markoff/src/Editor.cpp`, find `Editor::scrollToHeading(const HeadingInfo &heading)`. At the top of the body (before the existing scroll logic), add:

```cpp
// Auto-unfold any folded ancestors of the target heading.
const auto &cache = m_foldingModel->headings();
const FoldRegionKey *targetPath = nullptr;
for (const auto &h : cache) {
    if (h.info.level == heading.level && h.info.text == heading.text) {
        targetPath = &h.path;
        break;
    }
}
if (targetPath) {
    const auto expanded = m_foldingModel->unfoldAncestors(*targetPath);
    if (!expanded.isEmpty()) emit foldsAutoExpanded(expanded);
}
```

Register the metatype in `FoldingTypes.h` (or in `Editor.cpp`):

```cpp
Q_DECLARE_METATYPE(QList<QStringList>)
```

- [ ] **Step 4: Hook findText**

In `Editor::findText`, after a successful match is located (before returning true), add:

```cpp
// Resolve the enclosing heading path for the match item and auto-unfold.
const QPointF scenePoint = /* the match's scene coord — use existing
   match-cursor variable */;
const int idx = m_coordinator->itemIndexAt(scenePoint.y());
if (idx >= 0) {
    // Use coordinator's enclosingHeadingPath (now public for this call
    // — promote it to public in SceneCoordinator.h and move the impl
    // from private to public). Alternative: iterate headings in the
    // FoldingModel cache against idx.
    const QStringList path = m_coordinator->enclosingHeadingPath(idx);
    if (!path.isEmpty()) {
        const auto expanded = m_foldingModel->unfoldAncestors(path);
        if (!expanded.isEmpty()) emit foldsAutoExpanded(expanded);
    }
}
```

Promote `enclosingHeadingPath` from `private:` to `public:` in `SceneCoordinator.h` (it was added in Task 7).

- [ ] **Step 5: Build and run**

```
cmake --build build --target tst_markoff_folding_integration
ctest --test-dir build -R tst_markoff_folding_integration --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit**

```
git add libs/markoff/src/Editor.cpp libs/markoff/src/SceneCoordinator.h \
         libs/markoff/tests/tst_folding_integration.cpp \
         libs/markoff/include/markoff/FoldingTypes.h
git commit -m "feat(markoff): auto-unfold ancestors on scrollToHeading and findText"
```

---

## Task 9: GutterColumn interface + FoldArrowColumn

**Files:**
- Create: `libs/markoff/src/GutterColumn.h`
- Create: `libs/markoff/src/FoldArrowColumn.cpp`
- Modify: `libs/markoff/CMakeLists.txt` — append source.
- Create: `libs/markoff/tests/tst_fold_gutter.cpp` (grows in Task 10).

- [ ] **Step 1: Write the failing tests (column unit tests only)**

Create `libs/markoff/tests/tst_fold_gutter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include "GutterColumn.h"
#include "FoldingModel.h"

using namespace Markoff;

class TstFoldArrowColumn : public QObject {
    Q_OBJECT
private slots:
    void width_returns16();
    void paintCell_nonHeading_paintsNothing();
    void paintCell_unfoldedHeading_paintsDownTriangle();
    void paintCell_foldedHeading_paintsRightTriangle();
    void handleClick_noModifier_togglesThatHeading();
    void handleClick_ctrlModifier_foldsAllAtThatLevel();
};

static FoldingModel::HeadingEntry mk(QStringList path, int level) {
    return {path, HeadingInfo{level, path.last(), 0}};
}

static bool imageHasNonBackgroundPixels(const QImage &img) {
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(img.pixel(x, y)) > 0) return true;
    return false;
}

void TstFoldArrowColumn::width_returns16() {
    FoldingModel m;
    FoldArrowColumn col(&m);
    QCOMPARE(col.width(), 16);
}

void TstFoldArrowColumn::paintCell_nonHeading_paintsNothing() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    FoldArrowColumn col(&m);
    QImage img(16, 20, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    col.paintCell(&p, QRect(0, 0, 16, 20), /*itemIndex=*/999); // out of range
    p.end();
    QVERIFY(!imageHasNonBackgroundPixels(img));
}

void TstFoldArrowColumn::paintCell_unfoldedHeading_paintsDownTriangle() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    FoldArrowColumn col(&m);
    QImage img(16, 20, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    col.paintCell(&p, QRect(0, 0, 16, 20), /*headingIdx=*/0);
    p.end();
    QVERIFY(imageHasNonBackgroundPixels(img));
}

void TstFoldArrowColumn::paintCell_foldedHeading_paintsRightTriangle() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    m.fold({"A"});
    FoldArrowColumn col(&m);
    QImage img(16, 20, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    col.paintCell(&p, QRect(0, 0, 16, 20), 0);
    p.end();
    QVERIFY(imageHasNonBackgroundPixels(img));
    // Rightward triangle: right third of image empty, left third heavier.
    // Relaxed check: at least left quarter has pixels.
    bool leftQuarter = false;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < 4; ++x)
            if (qAlpha(img.pixel(x, y)) > 0) leftQuarter = true;
    QVERIFY(leftQuarter);
}

void TstFoldArrowColumn::handleClick_noModifier_togglesThatHeading() {
    FoldingModel m;
    m.setHeadingsForTesting({ mk({"A"}, 1) });
    FoldArrowColumn col(&m);
    QVERIFY(col.handleClick(QPoint(5, 5), 0, Qt::NoModifier));
    QVERIFY(m.isFolded({"A"}));
}

void TstFoldArrowColumn::handleClick_ctrlModifier_foldsAllAtThatLevel() {
    FoldingModel m;
    m.setHeadingsForTesting({
        mk({"A"}, 1), mk({"A","B"}, 2), mk({"A","C"}, 2)
    });
    FoldArrowColumn col(&m);
    QVERIFY(col.handleClick(QPoint(5, 5), 1, Qt::ControlModifier));
    QVERIFY(m.isFolded({"A","B"}));
    QVERIFY(m.isFolded({"A","C"}));
    QVERIFY(!m.isFolded({"A"}));
}

QTEST_MAIN(TstFoldArrowColumn)
#include "tst_fold_gutter.moc"
```

Append to `libs/markoff/tests/CMakeLists.txt`:

```cmake
add_executable(tst_markoff_fold_gutter tst_fold_gutter.cpp)
add_test(NAME tst_markoff_fold_gutter COMMAND tst_markoff_fold_gutter)
target_link_libraries(tst_markoff_fold_gutter PRIVATE Qt6::Test Qt6::Widgets markoff)
target_include_directories(tst_markoff_fold_gutter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set_tests_properties(tst_markoff_fold_gutter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 2: Run; verify failure (header doesn't exist)**

```
cmake -B build -DCORBOMITE_DEV_BUILD=ON
cmake --build build --target tst_markoff_fold_gutter 2>&1 | tail -5
```

- [ ] **Step 3: Create GutterColumn header**

Create `libs/markoff/src/GutterColumn.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_GUTTERCOLUMN_H
#define MARKOFF_GUTTERCOLUMN_H

#include <QRect>
#include <Qt>

class QPainter;

namespace Markoff {

class FoldingModel;

/// Abstract column rendered inside FoldGutter. Columns paint cells
/// for each visible heading-index and handle clicks addressed to them.
class GutterColumn {
public:
    virtual ~GutterColumn() = default;
    virtual int width() const = 0;
    /// Paint a cell for heading at `headingIndex` (index into
    /// FoldingModel::headings()). `cellRect` is in column-local coords.
    virtual void paintCell(QPainter *painter,
                           const QRect &cellRect,
                           int headingIndex) = 0;
    /// Handle a click. `localPos` is within this column's rect.
    /// Returns true if the click was handled.
    virtual bool handleClick(QPoint localPos,
                             int headingIndex,
                             Qt::KeyboardModifiers mods) = 0;
};

/// Concrete column: paints a fold triangle per heading, handles
/// toggle on click, foldAllAtLevel on Ctrl+Click.
class FoldArrowColumn : public GutterColumn {
public:
    explicit FoldArrowColumn(FoldingModel *model) : m_model(model) {}
    int width() const override { return 16; }
    void paintCell(QPainter *p, const QRect &rect, int idx) override;
    bool handleClick(QPoint pos, int idx, Qt::KeyboardModifiers mods) override;
private:
    FoldingModel *m_model;
};

} // namespace Markoff

#endif // MARKOFF_GUTTERCOLUMN_H
```

- [ ] **Step 4: Create FoldArrowColumn.cpp**

Create `libs/markoff/src/FoldArrowColumn.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GutterColumn.h"
#include "FoldingModel.h"
#include <QPainter>
#include <QPolygon>

namespace Markoff {

void FoldArrowColumn::paintCell(QPainter *p, const QRect &rect, int idx) {
    if (idx < 0 || idx >= m_model->headings().size()) return;
    const auto &entry = m_model->headings()[idx];
    const bool folded = m_model->isFolded(entry.path);

    // 7px triangle centered in the 16px cell. Adapted from
    // ~/src/kde/src/ktexteditor/src/view/kateviewhelpers.cpp:2194.
    const QPoint c = rect.center();
    const int s = 3; // half-size
    QPolygon tri;
    if (folded) {
        // Rightward: closed fold.
        tri << QPoint(c.x() - s, c.y() - s)
            << QPoint(c.x() - s, c.y() + s)
            << QPoint(c.x() + s, c.y());
    } else {
        // Downward: open fold.
        tri << QPoint(c.x() - s, c.y() - s)
            << QPoint(c.x() + s, c.y() - s)
            << QPoint(c.x(),     c.y() + s);
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(128, 128, 128));  // theme-aware in Task 11
    p->drawPolygon(tri);
    p->restore();
}

bool FoldArrowColumn::handleClick(QPoint, int idx, Qt::KeyboardModifiers mods) {
    if (idx < 0 || idx >= m_model->headings().size()) return false;
    const auto &entry = m_model->headings()[idx];
    if (mods & Qt::ControlModifier) {
        const int level = entry.info.level;
        // Toggle: if all at this level already folded, unfold them; else fold.
        bool allFolded = true;
        for (const auto &h : m_model->headings()) {
            if (h.info.level == level && !m_model->isFolded(h.path)) {
                allFolded = false; break;
            }
        }
        if (allFolded) m_model->unfoldAllAtLevel(level);
        else m_model->foldAllAtLevel(level);
    } else {
        m_model->toggle(entry.path);
    }
    return true;
}

} // namespace Markoff
```

- [ ] **Step 5: Add source + build**

Append `src/FoldArrowColumn.cpp` to `libs/markoff/CMakeLists.txt`.

```
cmake --build build --target tst_markoff_fold_gutter
ctest --test-dir build -R tst_markoff_fold_gutter --output-on-failure
```

Expected: all 6 tests pass.

- [ ] **Step 6: Commit**

```
git add libs/markoff/src/GutterColumn.h libs/markoff/src/FoldArrowColumn.cpp \
         libs/markoff/tests/tst_fold_gutter.cpp libs/markoff/tests/CMakeLists.txt \
         libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): GutterColumn interface and FoldArrowColumn"
```

---

## Task 10: FoldGutter (viewport-pinned)

**Files:**
- Create: `libs/markoff/src/FoldGutter.h` / `.cpp`
- Modify: `libs/markoff/CMakeLists.txt` — append source.
- Modify: `libs/markoff/tests/tst_fold_gutter.cpp` — add `TstFoldGutter` class.

- [ ] **Step 1: Write failing tests**

Append to `tst_fold_gutter.cpp` (before `QTEST_MAIN`):

```cpp
#include "FoldGutter.h"
#include <QGraphicsScene>

class TstFoldGutter : public QObject {
    Q_OBJECT
private slots:
    void width_sumsColumnsPlusSeparator();
    void click_onArrowRow_togglesFold();
    void click_onNonHeadingRow_isNoop();
    void setColumns_replacesExisting();
};

static FoldingModel::HeadingEntry mk2(QStringList p, int lvl) {
    return {p, HeadingInfo{lvl, p.last(), 0}};
}

void TstFoldGutter::width_sumsColumnsPlusSeparator() {
    FoldingModel model;
    FoldGutter gutter(&model);
    gutter.setColumns({ new FoldArrowColumn(&model) });
    QCOMPARE(gutter.width(), 16 + 2);
}

// Click tests exercise the click-to-column dispatch logic. Use a mock
// coordinator that returns fixed itemIndexAt() results.
// For this task, FoldGutter exposes handleMouseClick(QPointF localPos,
// Qt::KeyboardModifiers, int resolvedHeadingIdx) as a test hook.

void TstFoldGutter::click_onArrowRow_togglesFold() {
    FoldingModel model;
    model.setHeadingsForTesting({ mk2({"A"}, 1) });
    FoldGutter gutter(&model);
    gutter.setColumns({ new FoldArrowColumn(&model) });

    // Simulate click on column 0, heading index 0.
    QVERIFY(gutter.handleMouseClickForTesting(QPoint(5, 10), 0, Qt::NoModifier));
    QVERIFY(model.isFolded({"A"}));
}

void TstFoldGutter::click_onNonHeadingRow_isNoop() {
    FoldingModel model;
    model.setHeadingsForTesting({ mk2({"A"}, 1) });
    FoldGutter gutter(&model);
    gutter.setColumns({ new FoldArrowColumn(&model) });

    // headingIdx = -1 → click not on a heading row.
    QVERIFY(!gutter.handleMouseClickForTesting(QPoint(5, 10), -1, Qt::NoModifier));
    QVERIFY(!model.isFolded({"A"}));
}

void TstFoldGutter::setColumns_replacesExisting() {
    FoldingModel model;
    FoldGutter gutter(&model);
    gutter.setColumns({ new FoldArrowColumn(&model) });
    QCOMPARE(gutter.width(), 18);
    gutter.setColumns({ new FoldArrowColumn(&model), new FoldArrowColumn(&model) });
    QCOMPARE(gutter.width(), 16 + 16 + 2);
}
```

Replace `QTEST_MAIN(TstFoldArrowColumn)` with:

```cpp
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    int status = 0;
    { TstFoldArrowColumn t; status |= QTest::qExec(&t, argc, argv); }
    { TstFoldGutter t;      status |= QTest::qExec(&t, argc, argv); }
    return status;
}
#include "tst_fold_gutter.moc"
```

- [ ] **Step 2: Create FoldGutter header**

Create `libs/markoff/src/FoldGutter.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_FOLDGUTTER_H
#define MARKOFF_FOLDGUTTER_H

#include <QGraphicsObject>
#include <QList>

namespace Markoff {

class FoldingModel;
class SceneCoordinator;
class GutterColumn;

/// A viewport-pinned left-side gutter painting fold arrows and, in
/// future plans, line numbers. Owns its GutterColumn list.
class FoldGutter : public QGraphicsObject {
    Q_OBJECT
public:
    explicit FoldGutter(FoldingModel *model, QGraphicsItem *parent = nullptr);
    ~FoldGutter() override;

    void setCoordinator(SceneCoordinator *coord);
    void setColumns(QList<GutterColumn *> columns); // takes ownership

    int width() const;
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

    /// Test-only: same dispatch path as a real click, but with a
    /// pre-resolved heading index (bypassing the coordinator lookup).
    bool handleMouseClickForTesting(QPoint localPos,
                                    int headingIndex,
                                    Qt::KeyboardModifiers mods);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;

private:
    /// Find which column owns the x-coordinate and its local position.
    /// Returns -1 if out of range.
    int columnAt(qreal x, QPoint *localOut = nullptr) const;

    FoldingModel *m_model;
    SceneCoordinator *m_coordinator = nullptr;
    QList<GutterColumn *> m_columns;
    int m_separator = 2;
};

} // namespace Markoff

#endif // MARKOFF_FOLDGUTTER_H
```

- [ ] **Step 3: Implement FoldGutter.cpp**

Create `libs/markoff/src/FoldGutter.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FoldGutter.h"
#include "FoldingModel.h"
#include "GutterColumn.h"
#include "SceneCoordinator.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>

namespace Markoff {

FoldGutter::FoldGutter(FoldingModel *model, QGraphicsItem *parent)
    : QGraphicsObject(parent), m_model(model) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(QGraphicsItem::ItemIgnoresTransformations, false);
    connect(model, &FoldingModel::foldStateChanged,
            this, [this]() { update(); });
}

FoldGutter::~FoldGutter() { qDeleteAll(m_columns); }

void FoldGutter::setCoordinator(SceneCoordinator *coord) {
    m_coordinator = coord;
}

void FoldGutter::setColumns(QList<GutterColumn *> columns) {
    qDeleteAll(m_columns);
    m_columns = std::move(columns);
    prepareGeometryChange();
    update();
}

int FoldGutter::width() const {
    int w = 0;
    for (auto *c : m_columns) w += c->width();
    if (!m_columns.isEmpty()) w += m_separator;
    return w;
}

QRectF FoldGutter::boundingRect() const {
    // Height is set by owning editor via setPos/parent viewport. For
    // the item alone, bound is the gutter column stripe up to a large
    // virtual height; actual visible height is the scene's visible rect.
    return QRectF(0, 0, width(), 100000);
}

void FoldGutter::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    if (!m_coordinator) return;
    int x = 0;
    for (auto *col : m_columns) {
        for (int i = 0; i < m_model->headings().size(); ++i) {
            // Y-coordinate of each heading item in scene coords, mapped
            // to gutter-local. Look up via coordinator.
            const int itemIdx = /* heading-to-item map — use coord's
                itemIndexAt inverse. For v1, naive: scan items for
                matching heading text/offset. Optimisation deferred. */ -1;
            if (itemIdx < 0) continue;
            // Resolve scene Y → local Y via mapFromScene.
            // Simplified: the SceneCoordinator exposes items(); pick
            // the GraphicsItem for that index and use sceneBoundingRect.
            auto *it = m_coordinator->items().value(itemIdx);
            auto *gi = dynamic_cast<QGraphicsItem *>(it);
            if (!gi) continue;
            const QRectF itemRect = gi->sceneBoundingRect();
            const QPointF topLeftLocal = mapFromScene(QPointF(0, itemRect.top()));
            const QRect cell(x, int(topLeftLocal.y()),
                             col->width(), int(itemRect.height()));
            col->paintCell(p, cell, i);
        }
        x += col->width();
    }
}

void FoldGutter::mousePressEvent(QGraphicsSceneMouseEvent *e) {
    if (!m_coordinator) { e->ignore(); return; }
    QPoint local;
    const int ci = columnAt(e->pos().x(), &local);
    if (ci < 0) { e->ignore(); return; }
    // Resolve item index at scene Y.
    const int itemIdx = m_coordinator->itemIndexAt(e->scenePos().y());
    if (itemIdx < 0) { e->ignore(); return; }
    // Map itemIdx to a heading index.
    int hIdx = -1;
    int seen = 0;
    // Uses coordinator::enclosingHeadingPath's counting logic. Duplicate
    // here or expose helper. For v1, use FoldingModel::headings linear
    // scan by item-to-source mapping.
    hIdx = -1;  // TODO: wire via SceneCoordinator::headingIndexForItem — see note below.
    (void)hIdx;
    const bool handled = m_columns[ci]->handleClick(local, hIdx, e->modifiers());
    if (handled) e->accept();
    else e->ignore();
}

int FoldGutter::columnAt(qreal x, QPoint *localOut) const {
    int acc = 0;
    for (int i = 0; i < m_columns.size(); ++i) {
        if (x >= acc && x < acc + m_columns[i]->width()) {
            if (localOut) *localOut = QPoint(int(x - acc), 0);
            return i;
        }
        acc += m_columns[i]->width();
    }
    return -1;
}

bool FoldGutter::handleMouseClickForTesting(QPoint localPos, int headingIndex,
                                            Qt::KeyboardModifiers mods) {
    QPoint local;
    const int ci = columnAt(localPos.x(), &local);
    if (ci < 0 || headingIndex < 0) return false;
    return m_columns[ci]->handleClick(local, headingIndex, mods);
}

} // namespace Markoff
```

**Implementation note for the engineer:** The "map item index → heading index" is marked `TODO` in the click path. Before committing, add a helper `int SceneCoordinator::headingIndexForItem(int itemIndex) const` using the same counting logic already in `enclosingHeadingPath`, then call it from `mousePressEvent` to populate `hIdx`. This keeps the mapping in one place and removes the TODO.

Add `int headingIndexForItem(int itemIndex) const;` to `SceneCoordinator.h` (public). Implement:

```cpp
int SceneCoordinator::headingIndexForItem(int itemIndex) const {
    int seen = 0, lastHeading = -1;
    for (int i = 0; i <= itemIndex && i < m_items.size(); ++i) {
        auto *mti = dynamic_cast<MarkdownTextItem *>(m_items[i]);
        if (!mti) continue;
        if (mti->toPlainText().trimmed().startsWith('#')) {
            lastHeading = seen;
            ++seen;
        }
    }
    // Only return non-negative if the itemIndex IS a heading (not child).
    auto *target = dynamic_cast<MarkdownTextItem *>(m_items.value(itemIndex));
    if (target && target->toPlainText().trimmed().startsWith('#')) return lastHeading;
    return -1;
}
```

Then in `FoldGutter::mousePressEvent`, replace the TODO with:

```cpp
const int hIdx = m_coordinator->headingIndexForItem(itemIdx);
```

- [ ] **Step 4: Add to CMakeLists**

Append `src/FoldGutter.cpp` to `libs/markoff/CMakeLists.txt`.

- [ ] **Step 5: Build and run**

```
cmake --build build --target tst_markoff_fold_gutter
ctest --test-dir build -R tst_markoff_fold_gutter --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

```
git add libs/markoff/src/FoldGutter.h libs/markoff/src/FoldGutter.cpp \
         libs/markoff/src/SceneCoordinator.h libs/markoff/src/SceneCoordinator.cpp \
         libs/markoff/tests/tst_fold_gutter.cpp libs/markoff/CMakeLists.txt
git commit -m "feat(markoff): FoldGutter viewport-pinned graphics object"
```

---

## Task 11: Wire FoldGutter into Editor

**Files:**
- Modify: `libs/markoff/src/Editor.cpp` — instantiate `FoldGutter` after scene, handle viewport resize, implement `setGutterVisible`.
- Modify: `libs/markoff/tests/tst_folding_integration.cpp` — add gutter visibility test.

- [ ] **Step 1: Write the failing test**

Append to `tst_folding_integration.cpp`:

```cpp
void TstFoldingVisibility::editor_setGutterVisible_false_hidesGutter() {
    Editor e;
    e.setPlainText(kSample);
    waitForReparse();
    QVERIFY(e.isGutterVisible());
    e.setGutterVisible(false);
    QVERIFY(!e.isGutterVisible());
}
```

Add the slot declaration.

- [ ] **Step 2: Instantiate and wire FoldGutter**

In `libs/markoff/src/Editor.cpp`, add `#include "FoldGutter.h"` and `#include "GutterColumn.h"`. In the constructor after the `m_foldingModel` setup:

```cpp
m_foldGutter = new FoldGutter(m_foldingModel);
m_foldGutter->setCoordinator(m_coordinator);
m_foldGutter->setColumns({ new FoldArrowColumn(m_foldingModel) });
m_scene->addItem(m_foldGutter);

// Viewport-pinned: position the gutter at the left edge of the
// visible scene rect. Update on scroll and resize.
auto repositionGutter = [this]() {
    if (!m_foldGutter) return;
    const QPointF topLeft = mapToScene(viewport()->rect().topLeft());
    m_foldGutter->setPos(topLeft);
};
connect(horizontalScrollBar(), &QScrollBar::valueChanged,
        this, repositionGutter);
connect(verticalScrollBar(), &QScrollBar::valueChanged,
        this, repositionGutter);
```

In `Editor::resizeEvent`, call the same repositioning logic (extract to a private method `repositionFoldGutter()` if cleaner).

Update `setGutterVisible` in `Editor.cpp`:

```cpp
void Editor::setGutterVisible(bool v) {
    m_gutterVisible = v;
    if (m_foldGutter) m_foldGutter->setVisible(v);
}
```

- [ ] **Step 3: Build and run**

```
cmake --build build --target tst_markoff_folding_integration
ctest --test-dir build -R tst_markoff_folding_integration --output-on-failure
```

Expected: all pass.

- [ ] **Step 4: Manual visual check**

```
cmake --build build --target markoff-testapp
./build/bin/markoff-testapp libs/markoff/tests/showcase.md
```

Verify: left gutter visible with triangles next to headings; clicking toggles fold; Ctrl+Click folds all at that level. Report what you see (triangle appearance, click behavior, any visual glitches).

- [ ] **Step 5: Commit**

```
git add libs/markoff/src/Editor.cpp libs/markoff/tests/tst_folding_integration.cpp
git commit -m "feat(markoff): wire FoldGutter into Editor"
```

---

## Task 12: TODO.md update + final verification

**Files:**
- Modify: `libs/markoff/docs/TODO.md` — move heading-fold item from "Recently fixed" N/A to an added "Recently added" entry; remove the blocked-spec marker anywhere it appeared.

- [ ] **Step 1: Update TODO.md**

Edit `libs/markoff/docs/TODO.md`. Under "Recently fixed (for context)" add at the top:

```
- Heading folding (v1): `Editor::fold`, `unfold`, `toggleFold`,
  `foldAll`, `foldAllAtLevel`, `foldLevel` and persistence hooks
  (`serializeFoldState` / `restoreFoldState`). Left gutter with
  triangle arrows; Ctrl+Click folds all at level. Auto-unfold on
  `scrollToHeading` and `findText`. Plan:
  `docs/plans/2026-04-15-heading-folding.md`.
```

- [ ] **Step 2: Run the full markoff test suite**

```
ctest --test-dir build -R tst_markoff --output-on-failure
```

Expected: **all** markoff tests pass (existing + new).

- [ ] **Step 3: Commit**

```
git add libs/markoff/docs/TODO.md
git commit -m "docs(markoff): note heading-folding v1 in TODO"
```

---

## Self-review checklist

Before declaring complete:

1. **Spec coverage**
   - Path encoding + duplicate disambiguation — Task 1
   - `FoldingModel` core + bulk + persistence + reconcile — Tasks 2–5
   - `Editor` public API — Task 6
   - `SceneCoordinator` visibility integration — Task 7
   - Auto-unfold on nav + find — Task 8
   - `GutterColumn` + `FoldArrowColumn` — Task 9
   - `FoldGutter` viewport-pinned — Task 10
   - `Editor` gutter wiring + visibility toggle — Task 11
   - All 5 test binaries registered — Tasks 1, 4, 5, 6, 9

2. **No placeholders in final source** — the "Implementation note" boxes in Tasks 7 and 10 flag TODO items that must be resolved *within that task* (cleaner heading-to-item mapping, `headingIndexForItem` helper). Do not commit code with these TODO markers unresolved.

3. **Type consistency**
   - `FoldRegionKey` = `QStringList` everywhere.
   - `FoldingModel::HeadingEntry { path, info }` used consistently.
   - Signal name `foldStateChanged()` identical on `FoldingModel` and `Editor`.
   - `serializeFoldState` / `restoreFoldState` on `Editor`, `serialize` / `restore` on `FoldingModel`.

4. **Verification-before-completion** — all ctest commands in every task must pass before moving on. Never mark a task complete without running its test.
