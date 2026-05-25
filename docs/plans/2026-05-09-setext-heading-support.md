# Setext heading support — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land first-class setext (`text\n===` / `text\n---`) heading support across load, render, save, typing, and demote — eliminating the gap where the parser correctly identifies setext but our typing flow / save path treat it as paragraph + HR.

**Architecture:** Asymmetric heading-buffer convention with a new `headingForm` attr distinguishing ATX (`"atx"` / absent) from Setext (`"setext"`). Single-block kind-transition recognises setext shape (last line = `(=+|-+)` after a non-blank line); promotion + demote both run through the existing `LiveListModelBinding::onD2Changed` Equal-op loop. New `Shift+Enter` soft-newline path in `LiveStructuralKeyHandler` lets users type setext into one block by construction (no cross-block machinery needed).

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, QML, QJsonDocument (clipboard payloads), existing `markoff-core` and `markoff-live` test harnesses.

**Spec:** `docs/specs/2026-05-09-setext-heading-support-design.md`. Read §3–§9 before starting.

**Build cap:** `-j 8` everywhere. Never bare `-j` or higher (memory-saturation guard per CLAUDE.md feedback memory).

**Test discipline:** TDD. Each task: write failing test → run (red) → minimal impl → run (green) → commit. Run full suite at end of each phase.

**Branch:** `exploration/new-foundation`. Worktree: `.worktrees/foundation-exploration/`.

**Test commands:**
- Fast inner loop: `ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8`
- Markoff-core only: `ctest --test-dir build-dev -R '^tst_(d2|markoff_doc)' --output-on-failure -j 8`
- Full suite (slow): `ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"`

**Build:** `cmake --build build-dev -j 8`. Configure (one-time, only if `CMakeCache.txt` is missing): `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.

---

## File structure

### New files
- None. All work is additive in existing files.

### Modified files
- `libs/markoff-core/include/markoff/core/AttrNames.h` — add `HeadingForm` constant.
- `libs/markoff-core/src/MarkoffDocument.cpp` — set `headingForm="setext"` in `materializeBlocksFromParsedDoc`; widen `reconstructFlatMarkdown` heading branch (no-op pass-through unaffected).
- `libs/markoff-core/src/BlockSerializers.cpp` — form-aware `serializeHeading` + `stripLeadingHashes` helper.
- `libs/markoff-live/include/markoff/live/BlockRecord.h` — add `headingForm` field.
- `libs/markoff-live/include/markoff/live/LiveBlockModel.h` — `HeadingFormRole`.
- `libs/markoff-live/src/LiveBlockModel.cpp` — role registration + `data()` switch arm.
- `libs/markoff-live/src/LiveListModelBinding.cpp` — populate `r.headingForm` from attrs in `onD2Changed`; emit `headingForm` on heading promotion (atx + setext); form-aware demote relaxation.
- `libs/markoff-live/src/KindTransition.cpp` (+ header) — `matchesSetextShape` helper, `inferBlockKind` setext branch.
- `libs/markoff-live/src/LiveStructuralKeyHandler.cpp` — Shift+Enter soft-newline early-out.

### New tests
- `libs/markoff-core/tests/d2/tst_d2_load.cpp` — extend with setext-load cases (existing file; new test methods).
- `libs/markoff-core/tests/d2/tst_d2_save.cpp` — extend with setext-save cases + ATX-touched-no-double-prefix.
- `libs/markoff-live/tests/tst_live_render_kind_transition.cpp` — extend with setext promotion, demote, level-switch, HR-regression cases.
- `libs/markoff-live/tests/tst_live_render_structural.cpp` — extend with Shift+Enter soft-newline cases.
- New: `libs/markoff-live/tests/tst_live_render_setext_e2e.cpp` — end-to-end typing + save + reload.

---

## Phase 1 — Foundation: thread `headingForm` attr

### Task 1A: Add `AttrNames::HeadingForm` constant

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/AttrNames.h`

- [ ] **Step 1: Add the constant.**

In `libs/markoff-core/include/markoff/core/AttrNames.h`, inside the `Markoff::AttrNames` namespace, alongside the other heading-related constant `Level`:

```cpp
inline const AttrName HeadingForm  = "headingForm"; // Heading: QString — "atx" or "setext"
```

Order it after `Level` to group heading attrs together.

- [ ] **Step 2: Verify compile.**

Run: `cmake --build build-dev --target markoff_core -j 8`
Expected: clean build, no errors.

- [ ] **Step 3: Commit.**

```bash
git add libs/markoff-core/include/markoff/core/AttrNames.h
git commit -m "markoff-core: add AttrNames::HeadingForm for setext/atx distinction"
```

### Task 1B: Add `headingForm` field to `BlockRecord` + populate

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/BlockRecord.h`
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp:317-340` (the `onD2Changed` records-build loop)

- [ ] **Step 1: Add the field.**

In `BlockRecord.h`, after the `codeLanguage` field:

```cpp
QString              codeLanguage;      ///< Fence info-string if kind=="code-block".
QString              headingForm;       ///< "atx" / "setext" if kind=="heading"; else empty.
```

Update `operator==` to include `headingForm`:

```cpp
return kind == o.kind && text == o.text
    && headingLevel == o.headingLevel
    && codeLanguage == o.codeLanguage
    && headingForm  == o.headingForm
    && blockAnchor == o.blockAnchor
    && attrs == o.attrs;
```

- [ ] **Step 2: Populate in `onD2Changed`.**

Locate the kind-specific extras section in `LiveListModelBinding::onD2Changed` (around line 320, where `r.headingLevel` and `r.codeLanguage` are assigned). Add the heading-form lookup beside the heading-level lookup:

```cpp
if (r.kind == BlockKind::Heading) {
    auto lvl = rec.attrs.constFind(Markoff::AttrNames::Level);
    if (lvl != rec.attrs.cend()) {
        if (const int *p = std::get_if<int>(&lvl.value()))
            r.headingLevel = *p;
    }
    auto fm = rec.attrs.constFind(Markoff::AttrNames::HeadingForm);
    if (fm != rec.attrs.cend()) {
        if (const QString *p = std::get_if<QString>(&fm.value()))
            r.headingForm = *p;
    }
    if (r.headingForm.isEmpty()) r.headingForm = QStringLiteral("atx");
}
```

(The exact surrounding code may differ slightly; integrate with the existing heading branch.)

- [ ] **Step 3: Verify compile.**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/BlockRecord.h \
        libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "markoff-live: BlockRecord.headingForm, populate from attrs in onD2Changed"
```

### Task 1C: Add `HeadingFormRole` to `LiveBlockModel`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveBlockModel.h:31-50` (Role enum)
- Modify: `libs/markoff-live/src/LiveBlockModel.cpp:25-50` (`roleNames()` + `data()`)

- [ ] **Step 1: Add the role.**

In `LiveBlockModel.h`, in the `Role` enum, after `CodeLanguageRole`:

```cpp
enum Role {
    KindRole         = Qt::UserRole + 1,
    TextRole,
    HeadingLevelRole,
    HeadingFormRole,
    CodeLanguageRole,
    BlockAnchorRole,
    // ... rest unchanged
};
```

- [ ] **Step 2: Register the role name.**

In `LiveBlockModel.cpp` `roleNames()`:

```cpp
{ KindRole,         "kind" },
{ TextRole,         "text" },
{ HeadingLevelRole, "headingLevel" },
{ HeadingFormRole,  "headingForm" },
{ CodeLanguageRole, "codeLanguage" },
// ... rest unchanged
```

- [ ] **Step 3: Add the `data()` arm.**

In `LiveBlockModel::data()`, in the role switch:

```cpp
case HeadingLevelRole: return rec.headingLevel;
case HeadingFormRole:  return rec.headingForm;
case CodeLanguageRole: return rec.codeLanguage;
```

- [ ] **Step 4: Verify compile.**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean build.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/include/markoff/live/LiveBlockModel.h \
        libs/markoff-live/src/LiveBlockModel.cpp
git commit -m "markoff-live: LiveBlockModel.HeadingFormRole exposes attrs.headingForm"
```

### Task 1D: Set `headingForm="setext"` at load

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp:1679-1682` (`materializeBlocksFromParsedDoc` heading branch)
- Modify: `libs/markoff-core/tests/d2/tst_d2_load.cpp` (add test)

- [ ] **Step 1: Write failing test.**

Append to `libs/markoff-core/tests/d2/tst_d2_load.cpp` (inside `TstD2Load` class, declarations alongside other test methods):

```cpp
void heading_setextH2_setsHeadingFormAttr()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Heading\n---\n");
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
    auto attrs = doc.blockAttrs(blk);
    QVERIFY(attrs.contains(AttrNames::HeadingForm));
    QCOMPARE(std::get<QString>(attrs.value(AttrNames::HeadingForm)),
             QString("setext"));
    QCOMPARE(std::get<int>(attrs.value(AttrNames::Level)), 2);
}

void heading_setextH1_setsHeadingFormAttr()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Title\n===\n");
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
    auto attrs = doc.blockAttrs(blk);
    QCOMPARE(std::get<QString>(attrs.value(AttrNames::HeadingForm)),
             QString("setext"));
    QCOMPARE(std::get<int>(attrs.value(AttrNames::Level)), 1);
}

void heading_atx_doesNotSetHeadingFormAttr()
{
    // ATX headings get no explicit form attr — absent ≡ "atx".
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("## Heading\n");
    BlockId blk = doc.iterateBlocks().front();
    auto attrs = doc.blockAttrs(blk);
    QVERIFY(!attrs.contains(AttrNames::HeadingForm));
}
```

Add the same names to the `private slots:` declarations at top of class.

- [ ] **Step 2: Run test (red).**

```bash
cmake --build build-dev --target tst_d2_load -j 8
ctest --test-dir build-dev -R '^tst_d2_load$' --output-on-failure
```
Expected: FAIL (the two setext tests; ATX test should already pass).

- [ ] **Step 3: Implement.**

In `MarkoffDocument.cpp`, in `materializeBlocksFromParsedDoc`'s heading branch (around line 1679-1682), add a setext-form check:

```cpp
// Set kind-specific attrs
if (kind == BlockKind::Heading && tb.headingLevel > 0) {
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::Level}, AttrValue{tb.headingLevel});
}
if (kind == BlockKind::Heading
    && tb.kind == TopLevelBlock::Kind::SetextHeading) {
    d->blockAttrsMap.setWithNextStamp(
        BlockAttrKey{newId, AttrNames::HeadingForm},
        AttrValue{QString("setext")});
}
```

The `TopLevelBlock` enum is in `<markoff/parser/Document.h>` — already included.

- [ ] **Step 4: Run test (green).**

```bash
cmake --build build-dev --target tst_d2_load -j 8
ctest --test-dir build-dev -R '^tst_d2_load$' --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/tests/d2/tst_d2_load.cpp
git commit -m "markoff-core: tag setext blocks with headingForm=setext at load"
```

### Phase 1 gate

- [ ] **Run full live-render + core test suite. All green.**

```bash
ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"
```
Expected: 100% pass.

---

## Phase 2 — Save: form-aware serializer

### Task 2A: Add `stripLeadingHashes` helper

**Files:**
- Modify: `libs/markoff-core/src/BlockSerializers.cpp`

- [ ] **Step 1: Add the helper.**

In `BlockSerializers.cpp`, in the anonymous namespace (above `serializeHeading`):

```cpp
// Drops up to 6 leading `#` characters and one optional space after them.
// Returns the remainder. Used by serializeHeading to defend against the
// case where the heading buffer already carries an ATX prefix (the load-
// time convention) so re-serialisation doesn't double-prefix.
QByteArray stripLeadingHashes(const QByteArray &content)
{
    int i = 0;
    while (i < 6 && i < content.size() && content[i] == '#') ++i;
    if (i == 0) return content;
    if (i < content.size() && content[i] == ' ') ++i;
    return content.mid(i);
}
```

- [ ] **Step 2: Verify compile.**

Run: `cmake --build build-dev --target markoff_core -j 8`
Expected: clean build.

- [ ] **Step 3: Commit.**

```bash
git add libs/markoff-core/src/BlockSerializers.cpp
git commit -m "markoff-core: stripLeadingHashes helper for ATX heading re-serialisation"
```

### Task 2B: Form-aware `serializeHeading`

**Files:**
- Modify: `libs/markoff-core/src/BlockSerializers.cpp:50-60`
- Modify: `libs/markoff-core/tests/d2/tst_d2_save.cpp` (extend tests)

- [ ] **Step 1: Write failing tests.**

Append to `libs/markoff-core/tests/d2/tst_d2_save.cpp` (alongside `headingSerializer_prependsHashes`):

```cpp
void headingSerializer_atx_doesNotDoublePrefix()
{
    // Pre-existing latent bug: if the buffer already carries `## `
    // (the load-time convention for ATX headings), re-serialising with
    // the old code would emit `## ## Heading`. Fix is stripLeadingHashes.
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    QHash<AttrName, AttrValue> attrs;
    attrs["level"] = AttrValue{2};

    QCOMPARE(fn(BlockKind::Heading, attrs, "## Heading"),
             QByteArray("## Heading"));
    QCOMPARE(fn(BlockKind::Heading, attrs, "Heading"),
             QByteArray("## Heading"));
}

void headingSerializer_setext_emitsBufferVerbatim()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    QHash<AttrName, AttrValue> attrs;
    attrs["level"] = AttrValue{2};
    attrs["headingForm"] = AttrValue{QString("setext")};

    // Buffer contains the underline; serializer emits as-is.
    QCOMPARE(fn(BlockKind::Heading, attrs, "Heading\n---"),
             QByteArray("Heading\n---"));
}

void headingSerializer_setextH1_emitsBufferVerbatim()
{
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();
    auto fn = BuiltinBlockSerializerRegistry::instance().get(BlockKind::Heading);

    QHash<AttrName, AttrValue> attrs;
    attrs["level"] = AttrValue{1};
    attrs["headingForm"] = AttrValue{QString("setext")};

    QCOMPARE(fn(BlockKind::Heading, attrs, "Title\n==="),
             QByteArray("Title\n==="));
}
```

Declare them in the `private slots:` block.

- [ ] **Step 2: Run tests (red).**

```bash
cmake --build build-dev --target tst_d2_save -j 8
ctest --test-dir build-dev -R '^tst_d2_save$' --output-on-failure
```
Expected: FAIL on the three new methods (existing `headingSerializer_prependsHashes` should still pass).

- [ ] **Step 3: Implement form-aware serializer.**

Replace `serializeHeading` in `BlockSerializers.cpp` with:

```cpp
QByteArray serializeHeading(BlockKind, const QHash<AttrName, AttrValue> &attrs,
                             const QByteArray &content)
{
    int level = 1;
    auto it = attrs.constFind("level");
    if (it != attrs.cend()) {
        if (const int *p = std::get_if<int>(&it.value()))
            level = *p;
    }

    // Setext form: buffer already contains `text\n<underline>`. Emit
    // verbatim. Only valid for level 1 / 2 per CommonMark; fall through
    // to ATX otherwise (defensive).
    auto fmIt = attrs.constFind("headingForm");
    if (fmIt != attrs.cend()) {
        if (const QString *p = std::get_if<QString>(&fmIt.value())) {
            if (*p == QStringLiteral("setext") && (level == 1 || level == 2))
                return content;
        }
    }

    // ATX form. Strip any leading `# ` markers from content first so a
    // buffer like "## Heading" round-trips as "## Heading", not
    // "## ## Heading".
    return QByteArray(level, '#') + " " + stripLeadingHashes(content);
}
```

- [ ] **Step 4: Run tests (green).**

```bash
cmake --build build-dev --target tst_d2_save -j 8
ctest --test-dir build-dev -R '^tst_d2_save$' --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-core/src/BlockSerializers.cpp \
        libs/markoff-core/tests/d2/tst_d2_save.cpp
git commit -m "markoff-core: serializeHeading is form-aware; ATX strips prefix from content first"
```

### Phase 2 gate

- [ ] **Run full live-render + core test suite. All green.**

```bash
ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"
```

---

## Phase 3 — Kind-transition: setext recognition + form-aware demote

### Task 3A: Add `matchesSetextShape` helper

**Files:**
- Modify: `libs/markoff-live/src/KindTransition.h` (header — add new file if missing; existing header is at this path per plan grep)
- Modify: `libs/markoff-live/src/KindTransition.cpp`

- [ ] **Step 1: Declare the helper.**

In `KindTransition.h`, add to the `Markoff::Live` namespace:

```cpp
/// Returns 1 if `text` ends with a `===`-form setext H1 underline (preceded
/// by a non-blank line), 2 if `---`-form H2, 0 otherwise. Underline must be
/// the last line of `text`; the line directly above it must be non-blank
/// (CommonMark setext rules).
int matchesSetextShape(const QString &text);
```

- [ ] **Step 2: Define the helper.**

In `KindTransition.cpp`, before `inferBlockKind`:

```cpp
int matchesSetextShape(const QString &text)
{
    const int lastNl = text.lastIndexOf(u'\n');
    if (lastNl < 0) return 0;                         // single-line buffer, can't be setext

    // Underline candidate = substring after the last newline.
    const QString tail = text.mid(lastNl + 1);
    static const QRegularExpression underlineRe(
        QStringLiteral("^[ \\t]{0,3}(=+|-+)[ \\t]*$"));
    auto m = underlineRe.match(tail);
    if (!m.hasMatch()) return 0;
    const QChar uchar = m.captured(1).at(0);
    const int level = (uchar == u'=') ? 1 : 2;

    // Find the line directly above the underline; it must be non-blank.
    const int prevNl = text.lastIndexOf(u'\n', lastNl - 1);
    const QString aboveLine = (prevNl < 0)
        ? text.left(lastNl)
        : text.mid(prevNl + 1, lastNl - prevNl - 1);
    if (aboveLine.trimmed().isEmpty()) return 0;

    return level;
}
```

- [ ] **Step 3: Verify compile.**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/src/KindTransition.h \
        libs/markoff-live/src/KindTransition.cpp
git commit -m "markoff-live: matchesSetextShape recognises CommonMark setext underline"
```

### Task 3B: `inferBlockKind` setext branch

**Files:**
- Modify: `libs/markoff-live/src/KindTransition.cpp` (the `inferBlockKind` function)
- Modify: `libs/markoff-live/tests/tst_live_render_kind_transition.cpp` (extend)

- [ ] **Step 1: Write failing tests.**

Append to `tst_live_render_kind_transition.cpp` (inside the test class, declared in `private slots:`):

```cpp
void inferBlockKind_setextH2_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n---")),
             BlockKind::Heading);
}

void inferBlockKind_setextH1_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Title\n===")),
             BlockKind::Heading);
}

void inferBlockKind_bareDashes_stillReturnsHorizontalRule()
{
    // Single-line `---` is HR — regression check.
    QCOMPARE(inferBlockKind(QStringLiteral("---")),
             BlockKind::HorizontalRule);
}

void inferBlockKind_emptyTextLineThenDashes_returnsParagraph()
{
    // `\n---` — line above the underline is blank (empty); not setext.
    QCOMPARE(inferBlockKind(QStringLiteral("\n---")),
             BlockKind::Paragraph);
}

void inferBlockKind_blankLineAboveUnderline_returnsParagraph()
{
    // `Heading\n\n---` — line directly above underline is blank; not setext.
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n\n---")),
             BlockKind::Paragraph);
}

void inferBlockKind_setextWithLeadingWhitespace_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n  ---")),
             BlockKind::Heading);
}

void inferBlockKind_setextWithTrailingWhitespace_returnsHeading()
{
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n--- ")),
             BlockKind::Heading);
}

void inferBlockKind_mixedDashesAndEquals_returnsParagraph()
{
    // Mixed underline chars are not valid setext.
    QCOMPARE(inferBlockKind(QStringLiteral("Heading\n=-=")),
             BlockKind::Paragraph);
}
```

- [ ] **Step 2: Run tests (red).**

```bash
cmake --build build-dev --target tst_live_render_kind_transition -j 8
ctest --test-dir build-dev -R 'tst_live_render_kind_transition' --output-on-failure
```
Expected: FAIL on setext positives (`Heading\n---` etc.) — they currently return Paragraph.

- [ ] **Step 3: Implement the branch in `inferBlockKind`.**

In `KindTransition.cpp` `inferBlockKind`, **before** the bare-`---` HR check (currently lines 33-39), add:

```cpp
// Setext heading: text + \n + underline. Checked before bare-`---`-HR
// so `Heading\n---` wins over `---` alone.
if (matchesSetextShape(text) > 0)
    return BlockKind::Heading;
```

- [ ] **Step 4: Run tests (green).**

```bash
cmake --build build-dev --target tst_live_render_kind_transition -j 8
ctest --test-dir build-dev -R 'tst_live_render_kind_transition' --output-on-failure
```
Expected: PASS all eight new tests, plus existing tests still green.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/src/KindTransition.cpp \
        libs/markoff-live/tests/tst_live_render_kind_transition.cpp
git commit -m "markoff-live: inferBlockKind recognises setext shape before bare-dashes-HR"
```

### Task 3C: Heading promotion emits `headingForm` attr

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp:450-465` (the heading promotion path)

- [ ] **Step 1: Set form on ATX promotion.**

In `LiveListModelBinding::onD2Changed`, in the kind-transition promotion section where `attrNames` / `attrVals` are built for Heading (around line 452-457):

```cpp
QList<Markoff::AttrName> attrNames;
QList<Markoff::AttrValue> attrVals;
if (fk == Markoff::BlockKind::Heading) {
    attrNames << Markoff::AttrNames::Level;
    attrVals  << int(countLeadingHashes(rec.text));
    attrNames << Markoff::AttrNames::HeadingForm;
    // If the inferred shape is setext, the level helper above gave 0
    // (no leading hashes); use matchesSetextShape's level instead.
    const int setextLvl = matchesSetextShape(rec.text);
    if (setextLvl > 0) {
        attrVals[0] = setextLvl;  // overwrite the count-leading-hashes 0
        attrVals  << QString("setext");
    } else {
        attrVals  << QString("atx");
    }
} else if (fk == Markoff::BlockKind::Math) {
    // existing branch unchanged
    attrNames << Markoff::AttrNames::DisplayMode;
    attrVals  << displayMode;
}
```

`matchesSetextShape` requires the include in `LiveListModelBinding.cpp`. Add at top:

```cpp
#include "KindTransition.h"   // already included if your build picks it up; verify
```

(`KindTransition.h` is already included per the existing `inferBlockKind` call.)

- [ ] **Step 2: Verify compile.**

Run: `cmake --build build-dev --target markoff_live -j 8`
Expected: clean build.

- [ ] **Step 3: Commit.**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp
git commit -m "markoff-live: heading promotion emits headingForm attr (atx/setext)"
```

### Task 3D: Form-aware demote relaxation

**Files:**
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp:373-388` (the carve-out loop)
- Modify: `libs/markoff-live/tests/tst_live_render_kind_transition.cpp` (tests)

- [ ] **Step 1: Write failing tests.**

Append to `tst_live_render_kind_transition.cpp`:

```cpp
void atxHeading_allHashesDeleted_demotesToParagraph()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "## Heading", 1));
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);

    // Simulate user deleting all the hashes + space.
    auto id = binding.model()->recordAt(0).blockAnchor;
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(id, 0, 3, QByteArray{}, t);  // strip "## "
    }
    QTest::qWait(50);

    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Paragraph);
}

void setextHeading_underlineDeleted_demotesToParagraph()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Heading\n---\n", 1));
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
    QCOMPARE(binding.model()->recordAt(0).headingForm, QString("setext"));

    // Simulate user deleting "\n---" from the buffer.
    auto id = binding.model()->recordAt(0).blockAnchor;
    const QByteArray cur = doc.blockText(id);
    const int nlIdx = cur.indexOf('\n');
    QVERIFY(nlIdx > 0);
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(id, nlIdx,
                              cur.size() - nlIdx, QByteArray{}, t);
    }
    QTest::qWait(50);

    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Paragraph);
}

void setextHeading_levelChangeDashesToEquals_updatesLevel()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Heading\n---\n", 1));
    QCOMPARE(binding.model()->recordAt(0).headingLevel, 2);

    // Simulate user replacing "---" with "===".
    auto id = binding.model()->recordAt(0).blockAnchor;
    const QByteArray cur = doc.blockText(id);
    const int dashIdx = cur.indexOf('-');
    QVERIFY(dashIdx > 0);
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(id, dashIdx, 3, QByteArray("==="), t);
    }
    QTest::qWait(50);

    QCOMPARE(binding.model()->recordAt(0).headingLevel, 1);
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
}
```

`waitForModelRows` is the existing test helper (used elsewhere in this file).

- [ ] **Step 2: Run tests (red).**

```bash
cmake --build build-dev --target tst_live_render_kind_transition -j 8
ctest --test-dir build-dev -R 'tst_live_render_kind_transition' --output-on-failure
```
Expected: FAIL on all three new tests (current carve-out blocks both demotes; setext-level update is unimplemented).

- [ ] **Step 3: Implement form-aware demote + level update.**

In `LiveListModelBinding::onD2Changed`, find the same-kind branch (around line 357-371) and the carve-out (line 385-387). Replace the block:

```cpp
if (inferred == rec.kind) {
    // Same kind — check if heading level changed.
    if (rec.kind == BlockKind::Heading) {
        const QString form = rec.headingForm.isEmpty()
            ? QStringLiteral("atx") : rec.headingForm;
        int newLevel = 0;
        if (form == QStringLiteral("setext"))
            newLevel = matchesSetextShape(rec.text);
        else
            newLevel = countLeadingHashes(rec.text);
        if (newLevel > 0 && newLevel != rec.headingLevel) {
            Markoff::Cmd::changeKind(*doc,
                                     Markoff::BlockId(rec.blockAnchor),
                                     Markoff::BlockKind::Heading,
                                     {Markoff::AttrNames::Level},
                                     {newLevel});
            return;
        }
    }
    continue;
}

// Form-aware Heading demote: if the buffer no longer matches the
// stored form's marker pattern, demote to Paragraph.
if (rec.kind == BlockKind::Heading) {
    const QString form = rec.headingForm.isEmpty()
        ? QStringLiteral("atx") : rec.headingForm;
    const bool atxLost   = (form == QStringLiteral("atx")
                            && countLeadingHashes(rec.text) == 0);
    const bool setextLost = (form == QStringLiteral("setext")
                             && matchesSetextShape(rec.text) == 0);
    if (atxLost || setextLost) {
        Markoff::Cmd::changeKind(*doc, Markoff::BlockId(rec.blockAnchor),
                                 Markoff::BlockKind::Paragraph, {}, {});
        return;
    }
    // Heading is not "lost" — its content is content-only by design (no
    // marker prefix); keep it. Continue to fall through.
    continue;
}

// Existing carve-out for ListItem / CodeBlock / etc. — unchanged.
if (rec.kind != BlockKind::Paragraph
    && rec.kind != BlockKind::HorizontalRule
    && rec.kind != BlockKind::Image) continue;
```

(The `continue;` at the end of the same-kind branch + the new demote block + the existing carve-out together cover all heading paths; the carve-out then gates only the remaining non-paragraph kinds.)

- [ ] **Step 4: Run tests (green).**

```bash
cmake --build build-dev --target tst_live_render_kind_transition -j 8
ctest --test-dir build-dev -R 'tst_live_render_kind_transition' --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/tests/tst_live_render_kind_transition.cpp
git commit -m "markoff-live: form-aware heading demote + setext level switch"
```

### Phase 3 gate

- [ ] **Full test suite green.**

```bash
ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"
```

If `tst_live_render_structural` (which has a `backspace_at_start_of_heading_after_hr_demotes_merged_block` case asserting heading kind on a buffer with literal `---` content) regresses, revisit: the new setext branch now infers Heading on `---\n## 1. TL;DR` style content. The test's expected kind may need updating or the regression deserves a closer look. **Do not silently bypass.**

---

## Phase 4 — Shift+Enter soft newline

### Task 4A: Add soft-newline branch in `LiveStructuralKeyHandler::tryHandle`

**Files:**
- Modify: `libs/markoff-live/src/LiveStructuralKeyHandler.cpp:71-100`
- Modify: `libs/markoff-live/tests/tst_live_render_structural.cpp` (extend)

- [ ] **Step 1: Write failing tests.**

Append to `tst_live_render_structural.cpp` (declared in `private slots:`):

```cpp
void shiftEnter_inParagraph_insertsLiteralNewline_doesNotSplit()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Hello", 1));

    // Cursor at end of "Hello" (qtPos=5). Press Shift+Enter.
    const bool consumed = binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::ShiftModifier,
        /*blockIndex=*/0, /*qtPos=*/5,
        /*selectionEmpty=*/true,
        QStringLiteral("Hello"));
    QVERIFY(consumed);

    QTest::qWait(50);
    QCOMPARE(binding.model()->rowCount(), 1);                    // no split
    QCOMPARE(binding.model()->recordAt(0).text,
             QStringLiteral("Hello\n"));                         // newline appended
}

void shiftEnter_thenDashes_promotesToSetextHeading()
{
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Heading", 1));

    // Shift+Enter at end of "Heading" → buffer becomes "Heading\n".
    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::ShiftModifier, 0, 7, true,
        QStringLiteral("Heading"));
    QTest::qWait(50);

    // Type "---" (simulate via direct buffer edit).
    auto id = binding.model()->recordAt(0).blockAnchor;
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(id, 8, 0, QByteArray("---"), t);
    }
    QTest::qWait(50);

    QCOMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
    QCOMPARE(binding.model()->recordAt(0).headingLevel, 2);
    QCOMPARE(binding.model()->recordAt(0).headingForm,
             QString("setext"));
}

void plainEnter_stillSplitsBlock()
{
    // Regression: plain Enter (no Shift) continues to split.
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    QVERIFY(waitForModelRows(binding, doc, "Hello", 1));

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Return, Qt::NoModifier, 0, 5, true,
        QStringLiteral("Hello"));
    QTest::qWait(50);

    QCOMPARE(binding.model()->rowCount(), 2);
}
```

- [ ] **Step 2: Run tests (red).**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R 'tst_live_render_structural' --output-on-failure
```
Expected: FAIL on the two Shift+Enter tests; the plain-Enter test passes.

- [ ] **Step 3: Implement the soft-newline branch.**

In `LiveStructuralKeyHandler::tryHandle`, add an early-out **after** the BlockRecord/desc lookup (so we have `desc->supportedCursorVariants` available), but **before** the `consumedStructuralKeys.contains(key)` check (so it runs regardless of per-kind structural-key registration). Place it after the `Key_Escape` block (around line 120):

```cpp
// Shift+Enter: soft newline within the current block. Inserts a literal
// '\n' at qtPos in the buffer — does NOT split the block. Available on
// any TextCaret-supporting kind (paragraph, heading, list-item,
// blockquote, code-block — i.e. not HR/image/math). General-purpose
// affordance; setext-heading typing is one consumer (Heading + \n + ---).
if ((key == Qt::Key_Return || key == Qt::Key_Enter)
    && (modifiers & Qt::ShiftModifier)
    && !(modifiers & Qt::ControlModifier)
    && !(modifiers & Qt::AltModifier)
    && desc->supportedCursorVariants.contains(QStringLiteral("TextCaret"))) {

    const Markoff::BlockId id(rec.blockAnchor);
    const QByteArray modelUtf8 = blockText.toUtf8();
    const int byteOff = qBound(0,
        Coordinates::qtPosToByte(modelUtf8, qtPos),
        modelUtf8.size());

    UndoLog::Transaction t(m_document->d2UndoLog());
    m_document->d2ApplyBufferEdit(id,
        static_cast<uint32_t>(byteOff), 0,
        QByteArray("\n"), t);

    // Caret advances past the inserted newline.
    m_cursorState->requestTextCaretAtAnchor(rec.blockAnchor, byteOff + 1);
    return true;
}
```

The `Coordinates::qtPosToByte` helper is in `<markoff/live/Coordinates.h>` — already included in this file via the existing `qtPosToByte` calls. Verify the include is present; if not, add `#include <markoff/live/Coordinates.h>`.

- [ ] **Step 4: Run tests (green).**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8
ctest --test-dir build-dev -R 'tst_live_render_structural' --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add libs/markoff-live/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live/tests/tst_live_render_structural.cpp
git commit -m "markoff-live: Shift+Enter inserts soft newline without splitting block"
```

### Phase 4 gate

- [ ] **Full test suite green.**

```bash
ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"
```

---

## Phase 5 — End-to-end + dogfood

### Task 5A: End-to-end typing + save + reload test

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_setext_e2e.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt` (register the new test)

- [ ] **Step 1: Write the test file.**

Create `libs/markoff-live/tests/tst_live_render_setext_e2e.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveStructuralKeyHandler.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>

using namespace Markoff::Live;

class TestSetextE2E : public QObject {
    Q_OBJECT

private Q_SLOTS:

    void typeShiftEnterDashes_producesSetextH2()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Heading");
        QTest::qWait(50);
        QCOMPARE(binding.model()->rowCount(), 1);

        // Shift+Enter at end of "Heading".
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier, 0, 7, true,
            QStringLiteral("Heading"));
        QTest::qWait(50);

        // Type "---".
        auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 8, 0, QByteArray("---"), t);
        }
        QTest::qWait(50);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(rec.kind, BlockKind::Heading);
        QCOMPARE(rec.headingLevel, 2);
        QCOMPARE(rec.headingForm, QString("setext"));
        QCOMPARE(rec.text, QStringLiteral("Heading\n---"));
    }

    void typeShiftEnterEquals_producesSetextH1()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("Title");
        QTest::qWait(50);

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier, 0, 5, true,
            QStringLiteral("Title"));
        QTest::qWait(50);

        auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 6, 0, QByteArray("==="), t);
        }
        QTest::qWait(50);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(rec.kind, BlockKind::Heading);
        QCOMPARE(rec.headingLevel, 1);
        QCOMPARE(rec.headingForm, QString("setext"));
    }

    void loadSetext_editContent_save_preservesForm()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("Heading\n---\n");
        auto id = doc.iterateBlocks().front();
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);

        // Edit the heading content.
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 7, 0, QByteArray(" Edited"), t);
            // buffer is now "Heading Edited\n---"
        }

        const QByteArray saved = doc.serializeForSave();
        QVERIFY2(saved.contains("Heading Edited\n---"), saved.constData());
        QVERIFY2(!saved.contains("## "), saved.constData());  // not converted to ATX

        // Round-trip: load the saved bytes into a fresh doc.
        Markoff::MarkoffDocument doc2(/*replicaId=*/2);
        doc2.loadFromMarkdown(saved);
        auto id2 = doc2.iterateBlocks().front();
        QCOMPARE(doc2.blockKind(id2), Markoff::BlockKind::Heading);
        auto attrs2 = doc2.blockAttrs(id2);
        QCOMPARE(std::get<QString>(attrs2.value(Markoff::AttrNames::HeadingForm)),
                 QString("setext"));
        QCOMPARE(std::get<int>(attrs2.value(Markoff::AttrNames::Level)), 2);
    }

    void typeDashes_inEmptyParagraph_producesHorizontalRule()
    {
        // Regression: bare `---` in a paragraph still becomes HR.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("");
        QTest::qWait(50);

        // Insert "---" into the auto-created empty paragraph.
        auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 0, 0, QByteArray("---"), t);
        }
        QTest::qWait(50);

        QCOMPARE(binding.model()->recordAt(0).kind,
                 BlockKind::HorizontalRule);
    }
};

QTEST_MAIN(TestSetextE2E)
#include "tst_live_render_setext_e2e.moc"
```

- [ ] **Step 2: Register the test in CMake.**

In `libs/markoff-live/tests/CMakeLists.txt`, add a new entry next to the existing structural test:

```cmake
qt_add_executable(tst_live_render_setext_e2e tst_live_render_setext_e2e.cpp)
target_link_libraries(tst_live_render_setext_e2e
    PRIVATE markoff_live markoff_core Qt6::Test Qt6::Quick)
add_test(NAME tst_live_render_setext_e2e
         COMMAND tst_live_render_setext_e2e)
```

(Match the exact pattern of the surrounding tests — copy from `tst_live_render_structural`'s registration.)

- [ ] **Step 3: Run test (red, then green).**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target tst_live_render_setext_e2e -j 8
ctest --test-dir build-dev -R 'tst_live_render_setext_e2e' --output-on-failure
```
Expected: PASS (all earlier phases green means the e2e tests should pass on first run).

- [ ] **Step 4: Commit.**

```bash
git add libs/markoff-live/tests/tst_live_render_setext_e2e.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: tst_live_render_setext_e2e covers typing + save round-trip"
```

### Task 5B: Manual dogfood pass

- [ ] **Step 1: Build the test app.**

```bash
cmake --build build-dev --target markoff-live-app -j 8
```

- [ ] **Step 2: Create a setext-bearing markdown file.**

```bash
cat > /tmp/setext-dogfood.md <<'EOF'
A First Heading
===============

Some body text under H1.

A Second Heading
----------------

Body text under H2.

Plain paragraph.

---

After the HR.
EOF
```

- [ ] **Step 3: Open in markoff-live-app and verify visually.**

```bash
./build-dev/bin/markoff-live-app /tmp/setext-dogfood.md
```

Visual checks:
- "A First Heading" renders in H1 typography. The `===` underline is visible directly below in the same heading block (chunky for now — that's expected, polish-deferred).
- "A Second Heading" renders in H2 with `---` underline.
- "Plain paragraph" is unstyled.
- The standalone `---` (after the paragraph) renders as a horizontal rule (own block).

Editing checks:
- Click into "A First Heading", type a character at end. Heading remains heading; level unchanged.
- Select the underline `===`, delete it. The heading demotes to a paragraph (typography reverts).
- Click somewhere new, type "Heading", **Shift+Enter**, `---`. Result is a single H2 setext heading.
- Type a paragraph, plain Enter, then `---`. The `---` is a horizontal rule (separate block).
- Save (Ctrl+S). Reopen the file. The setext form should be preserved byte-for-byte for headings you didn't touch; touched headings still appear as setext (form preserved).

- [ ] **Step 4: Record findings.**

If anything renders or behaves unexpectedly, capture in a new findings doc under `docs/handoff/2026-05-09-setext-dogfood-findings.md` and stop here for triage. Otherwise continue to Phase 6.

### Phase 5 gate

- [ ] **Full test suite green.**

```bash
ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"
```

---

## Phase 6 — Wrap-up

### Task 6A: Update e-arc status board

**Files:**
- Modify: `docs/e-arc/e-arc-status.md` (activity log section)

- [ ] **Step 1: Append an entry to the activity log.**

Add an entry near the top of the log, dated 2026-05-09, summarising:
- Spec reference: `docs/specs/2026-05-09-setext-heading-support-design.md`
- Plan reference: this doc
- What landed: setext load + render + save + typing (Shift+Enter) + form-aware demote, plus the latent ATX double-prefix bug closure.
- Test count delta.

- [ ] **Step 2: Commit.**

```bash
git add docs/e-arc/e-arc-status.md
git commit -m "docs: setext heading support landed (activity log entry)"
```

### Task 6B: Final verification

- [ ] **Run full fast suite + smoke-test the app one more time.**

```bash
ctest --test-dir build-dev --output-on-failure -j 8 -E "tst_realistic|tst_benchmark"
timeout 3 env QT_QPA_PLATFORM=minimal ./build-dev/bin/markoff-live-app /tmp/setext-dogfood.md
```

- [ ] **Verify clean working tree (or only the e-arc-status commit + final dogfood notes).**

```bash
git status
```

---

## Self-review pass

After implementation: re-read the spec §3–§9 against the landed code. Any gaps → file a follow-up ticket; do not silently leave anything undone. Any test that was retrofitted to match an unintended behaviour → revert and fix the code instead (per CLAUDE.md "Tests define expected behavior").
