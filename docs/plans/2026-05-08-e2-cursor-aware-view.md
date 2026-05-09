# E2 — Cursor-aware view (auto-hide markers + cross-block keyboard nav) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Markoff Live's caret position drive both rendering (auto-hide of inline markers, heading prefixes, code-fence lines via true zero-width collapse) and full desktop-editor keyboard navigation across block boundaries (Up/Down/Left/Right with column preservation, Home/End/Ctrl-variants, word boundaries, Page-Up/Down, Shift- and Ctrl+Shift-extend).

**Architecture:** New `LiveNavigationController` (sibling to `LiveStructuralKeyHandler`) handles all cross-block keyboard nav; existing E1 `InlineHighlighter` is extended with a `setLocalCaretPosition` slot and uses the parser's already-populated `SourceSpan::{isDelimiter, parentCharStart, parentCharEnd}` fields to drive reveal/hide. `LiveCursorState` gains a `desiredVisualX` field for cross-block column preservation. `Theme` gains a `HiddenMarker` slot whose `QTextCharFormat` carries negative `QFont::letterSpacing` to absorb marker glyph advance. No new `Cmd::*` ops, no D5 changes.

**Tech Stack:** C++20, Qt 6.8+, CMake 3.19+, QML, KDE QSyntaxHighlighter, existing `markoff-live` test harness.

**Spec:** `docs/specs/2026-05-08-e2-cursor-aware-view-design.md`. Read §0–§5 before starting. §9 (open questions) names per-task resolution gates.

**Build cap:** `-j 8` everywhere. Never bare `-j` or higher.

**Test discipline:** TDD. Each task: failing test → run (red) → minimal impl → run (green) → commit. Each phase ends with full ctest run. Final phase requires manual dogfood pass before tagging.

**Branch:** `exploration/new-foundation`. Worktree: `.worktrees/foundation-exploration/`.

**Completion tag:** `v0.7.0-e2` after Phase I.

---

## File structure

### New files

- `libs/markoff-live/include/markoff/live/LiveNavigationController.h`
- `libs/markoff-live/src/LiveNavigationController.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_autohide_per_kind.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_autohide_caret_adjacent.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_autohide_nested.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_autohide_selection.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_autohide_block_prefix.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_autohide_peer_cursor.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_nav_column_preservation.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_nav_word.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_nav_home_end.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_nav_page.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_perf_caret_move.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_perf_typing_no_regression.cpp`
- `libs/markoff-live/tests/tst_live_render_e2_zero_width_mechanism.cpp` (Phase A measurement test)

### Modified files

- `libs/markoff-core/include/markoff/core/Theme.h` — add `HiddenMarker` Slot + helper.
- `libs/markoff-core/src/Theme.cpp` — populate `HiddenMarker` defaults.
- `libs/markoff-live/include/markoff/live/LiveCursorState.h` — add `desiredVisualX` field + accessors + `Q_INVOKABLE clearDesiredVisualX`.
- `libs/markoff-live/src/LiveCursorState.cpp` — implement.
- `libs/markoff-live/include/markoff/live/InlineHighlighter.h` — add `setLocalCaretPosition`, `setSelectionRange` slots; new internal state.
- `libs/markoff-live/src/InlineHighlighter.cpp` — extend `highlightBlock` to apply hidden format on delimiter spans whose parent range does not contain caret/selection.
- `libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h` — add `caretPosition`, `selectionStart`, `selectionEnd` Q_PROPERTY.
- `libs/markoff-live/src/InlineHighlighterAttached.cpp` — implement.
- `libs/markoff-live/qml/delegates/ParagraphDelegate.qml` — wire caret/selection; forward arrow + Home/End + Page + Ctrl-variant keys; expose `Q_INVOKABLE computePrevWordPos`, `computeNextWordPos`, `cursorRectangle()` accessors.
- `libs/markoff-live/qml/delegates/HeadingDelegate.qml` — same.
- `libs/markoff-live/qml/delegates/ListItemDelegate.qml` — same.
- `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml` — same.
- `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml` — same.
- `libs/markoff-live/qml/delegates/HorizontalRuleDelegate.qml` — Shift-extend support only.
- `libs/markoff-live/qml/delegates/ImageDelegate.qml` — Shift-extend support only.
- `libs/markoff-live/qml/delegates/MathDelegate.qml` — Shift-extend support only.
- `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` — expose new `LiveNavigationController *navigationController()` accessor.
- `libs/markoff-live/src/LiveListModelBinding.cpp` — instantiate the controller.
- `libs/markoff-live/CMakeLists.txt` — add new sources + tests.
- `libs/markoff-live/tests/CMakeLists.txt` — register new test executables.
- `docs/e-arc/e-arc-status.md` — final phase: status `complete`, tag entry.
- `CLAUDE.md` (worktree) — final phase: banner update.

---

## Phase A — Pre-flight & shared infrastructure

Goal: lay the substrate that auto-hide and nav both depend on. **No behavior changes for the user yet.**

---

### Task A1: Add `HiddenMarker` Slot to Theme

**Files:**
- Modify: `libs/markoff-core/include/markoff/core/Theme.h`
- Modify: `libs/markoff-core/src/Theme.cpp`
- Test: extend an existing theme test or add `libs/markoff-core/tests/tst_theme_hidden_marker.cpp` if no closely-fitting test exists.

- [ ] **Step 1: Read the current Theme structure**

Run: `grep -n "Slot\|setSlotColor\|color(.*Slot" libs/markoff-core/include/markoff/core/Theme.h | head -40`

Goal: confirm the `enum class Slot` location and the `color(Slot)` accessor pattern. Note any sibling helpers like `isBold(Slot)`, `font(...)`.

- [ ] **Step 2: Write the failing test**

Add to an existing Theme test file (whichever covers Slot defaults — `find libs/markoff-core/tests -name 'tst_theme*'`). If none exists, create `libs/markoff-core/tests/tst_theme_hidden_marker.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/Theme.h>

class TestThemeHiddenMarker : public QObject {
    Q_OBJECT
private slots:
    void hiddenMarker_slot_exists_in_default_theme() {
        Markoff::Theme theme;
        // HiddenMarker should be addressable as a Slot value and produce
        // a QTextCharFormat that has negative letterSpacing applied.
        const QTextCharFormat fmt = theme.charFormat(Markoff::Theme::Slot::HiddenMarker);
        QFont f = fmt.font();
        // Negative letterSpacing in AbsoluteSpacing mode is the marker
        // of "hidden" rendering.
        QCOMPARE(f.letterSpacingType(), QFont::AbsoluteSpacing);
        QVERIFY(f.letterSpacing() < 0.0);
    }
};

QTEST_MAIN(TestThemeHiddenMarker)
#include "tst_theme_hidden_marker.moc"
```

Register in `libs/markoff-core/tests/CMakeLists.txt` following the sibling-test pattern (look for `add_executable(tst_theme_*` for the convention).

- [ ] **Step 3: Run test to verify it fails**

```
cmake --build build-dev --target tst_theme_hidden_marker -j 8
ctest --test-dir build-dev -R tst_theme_hidden_marker --output-on-failure
```

Expected: FAIL — `Slot::HiddenMarker` does not exist; or `Theme::charFormat(Slot)` does not exist.

- [ ] **Step 4: Add the Slot enum value**

Edit `libs/markoff-core/include/markoff/core/Theme.h`. Add `HiddenMarker` to the `enum class Slot` (place it after the other inline-format slots like `Highlight`):

```cpp
enum class Slot {
    TextDefault,
    Heading1, Heading2, Heading3, Heading4, Heading5, Heading6,
    InlineCode, CodeBlock,
    Link, WikiLink, Tag, Math,
    Quote,
    BoldEmphasis, ItalicEmphasis, StrikeEmphasis,
    Highlight,
    HiddenMarker,        // <-- new
    SelectionBackground,
    // ... rest unchanged
};
```

- [ ] **Step 5: Add a `charFormat(Slot)` accessor if not already present**

Read the current Theme header to see if `charFormat(Slot)` exists. If yes, skip. If no, add a public method:

```cpp
QTextCharFormat charFormat(Slot s) const;
```

And implement in `Theme.cpp` as a switch over Slot that returns a `QTextCharFormat` with the slot's color/bold/italic/etc. applied. For most slots this just wraps the existing `color(Slot)` etc. helpers. For `HiddenMarker` specifically:

```cpp
QTextCharFormat Theme::charFormat(Slot s) const {
    QTextCharFormat fmt;
    if (s == Slot::HiddenMarker) {
        QFont f = fmt.font();
        // Absorb a typical Latin glyph's advance. Real per-glyph negative
        // spacing is applied at highlight time (see InlineHighlighter); the
        // value here is a baseline that gets refined per-character.
        f.setLetterSpacing(QFont::AbsoluteSpacing, -1000.0);
        fmt.setFont(f);
        return fmt;
    }
    // Generic path for the other slots: build from existing color/bold/italic
    const QColor c = color(s);
    if (c.isValid()) fmt.setForeground(c);
    if (isBold(s))   fmt.setFontWeight(QFont::Bold);
    if (isItalic(s)) fmt.setFontItalic(true);
    return fmt;
}
```

The `-1000.0` is intentionally aggressive — at highlight time, `InlineHighlighter` will measure the actual glyph advance via `QFontMetrics::horizontalAdvance(ch)` and produce a per-char `QTextCharFormat` based on the `HiddenMarker` template but with the precise negative spacing. The Slot just carries the recipe.

- [ ] **Step 6: Run test to verify it passes**

```
cmake --build build-dev --target tst_theme_hidden_marker -j 8
ctest --test-dir build-dev -R tst_theme_hidden_marker --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Run the full markoff-core test suite to confirm no regression**

```
ctest --test-dir build-dev -R '^tst_' -E 'tst_realistic|tst_benchmark' -j 8
```

Expected: all green. If a Theme-related test now fails because of an enum-position shift, fix the test (the addition is append-only at the new line; existing slots keep their positions if you placed `HiddenMarker` after `Highlight`).

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-core/include/markoff/core/Theme.h \
        libs/markoff-core/src/Theme.cpp \
        libs/markoff-core/tests/tst_theme_hidden_marker.cpp \
        libs/markoff-core/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-core: Theme::Slot::HiddenMarker (E2 substrate)

E2 needs a per-theme QTextCharFormat that carries negative
letterSpacing to collapse marker glyphs to zero advance.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task A2: Verify the zero-width mechanism (measurement test)

Per spec §9.Q2: confirm Qt's negative `QFont::letterSpacing` actually collapses glyph advance before committing to the auto-hide implementation. If it doesn't, the spec authorizes a fallback combining with `setFontStretch(1)`.

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_zero_width_mechanism.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test that measures collapse**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase A measurement test for spec §9.Q2.
// Verifies that Qt's negative QFont::letterSpacing(AbsoluteSpacing) does
// in fact collapse a glyph's effective advance. If this fails on the host
// platform, the implementation must fall back to setFontStretch(1) and
// the spec's Fallback A path.
#include <QFontMetricsF>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QTest>

class TestE2ZeroWidthMechanism : public QObject {
    Q_OBJECT
private slots:
    void negative_letter_spacing_absorbs_glyph_advance() {
        QTextDocument doc;
        doc.setPlainText("**hello**");

        // Measure baseline width (no formatting applied).
        const qreal baselineWidth = doc.idealWidth();

        // Apply hide format to the two leading and two trailing asterisks.
        QFont f = doc.defaultFont();
        const qreal asteriskAdvance =
            QFontMetricsF(f).horizontalAdvance(QChar(u'*'));
        f.setLetterSpacing(QFont::AbsoluteSpacing, -asteriskAdvance);
        QTextCharFormat hidden;
        hidden.setFont(f);

        QTextCursor c(&doc);
        c.setPosition(0);
        c.setPosition(2, QTextCursor::KeepAnchor);
        c.mergeCharFormat(hidden);
        c.setPosition(7);
        c.setPosition(9, QTextCursor::KeepAnchor);
        c.mergeCharFormat(hidden);

        // Re-measure after formatting.
        const qreal hiddenWidth = doc.idealWidth();

        // The four asterisks together have advance = 4 * asteriskAdvance.
        // After negative spacing, hiddenWidth should be roughly baselineWidth
        // - 4 * asteriskAdvance, within a 4px tolerance for kerning.
        const qreal expectedDelta = 4.0 * asteriskAdvance;
        const qreal actualDelta = baselineWidth - hiddenWidth;
        qInfo() << "baseline=" << baselineWidth << "hidden=" << hiddenWidth
                << "asteriskAdvance=" << asteriskAdvance
                << "expectedDelta=" << expectedDelta
                << "actualDelta=" << actualDelta;
        // If the mechanism does NOT collapse, actualDelta will be ~0 and
        // this test fails — flagging the platform issue described in
        // spec §9.Q2 / §2.4 Fallback A.
        QVERIFY2(actualDelta >= expectedDelta - 4.0,
                 qPrintable(QStringLiteral(
                     "negative letterSpacing did not absorb glyph advance "
                     "(expected delta ≥ %1, got %2). Fall back to "
                     "setFontStretch(1) per spec §2.4.")
                     .arg(expectedDelta - 4.0)
                     .arg(actualDelta)));
    }
};

QTEST_MAIN(TestE2ZeroWidthMechanism)
#include "tst_live_render_e2_zero_width_mechanism.moc"
```

Register in `libs/markoff-live/tests/CMakeLists.txt`:

```cmake
add_executable(tst_live_render_e2_zero_width_mechanism
    tst_live_render_e2_zero_width_mechanism.cpp)
target_link_libraries(tst_live_render_e2_zero_width_mechanism
    PRIVATE markoff_core Qt6::Test Qt6::Gui)
add_test(NAME tst_live_render_e2_zero_width_mechanism
         COMMAND tst_live_render_e2_zero_width_mechanism)
```

- [ ] **Step 2: Build and run**

```
cmake --build build-dev --target tst_live_render_e2_zero_width_mechanism -j 8
ctest --test-dir build-dev -R tst_live_render_e2_zero_width_mechanism --output-on-failure
```

Expected: PASS. The qInfo trace will be visible — record the actualDelta value for the implementation reference. If FAIL, halt the plan and escalate per spec §2.4.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_e2_zero_width_mechanism.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
markoff-live: zero-width letterSpacing measurement test (E2 §9.Q2)

Confirms Qt's negative QFont::letterSpacing(AbsoluteSpacing) collapses
glyph advance. If this test ever fails on a future platform, the
implementation falls back to setFontStretch(1) per spec §2.4.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

### Task A3: Add `desiredVisualX` to `LiveCursorState`

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveCursorState.h`
- Modify: `libs/markoff-live/src/LiveCursorState.cpp`
- Test: `libs/markoff-live/tests/tst_live_render_cursor.cpp` (extend; use the existing test file, don't add new)

- [ ] **Step 1: Write the failing test**

Append to `tst_live_render_cursor.cpp`:

```cpp
private slots:
    void desired_visual_x_default_is_unset() {
        // ... use the existing setup pattern (parser + model + binding)
        LiveCursorState cs(/* same args as existing test */);
        QCOMPARE(cs.desiredVisualX(), -1.0);  // sentinel for "unset"
    }

    void desired_visual_x_persists_across_set_and_get() {
        LiveCursorState cs(/* ... */);
        cs.setDesiredVisualX(42.5);
        QCOMPARE(cs.desiredVisualX(), 42.5);
    }

    void clear_desired_visual_x_resets_to_sentinel() {
        LiveCursorState cs(/* ... */);
        cs.setDesiredVisualX(42.5);
        cs.clearDesiredVisualX();
        QCOMPARE(cs.desiredVisualX(), -1.0);
    }
```

(Mirror the constructor-args pattern from existing `tst_live_render_cursor` slots — they instantiate `BlockKindRegistry`, `LiveBlockModel`, etc. via the existing test fixture.)

- [ ] **Step 2: Run test to verify failure**

```
ctest --test-dir build-dev -R tst_live_render_cursor --output-on-failure
```

Expected: FAIL — `desiredVisualX` / `setDesiredVisualX` / `clearDesiredVisualX` undefined.

- [ ] **Step 3: Add the field, accessors, and slot**

Edit `LiveCursorState.h`:

```cpp
class MARKOFF_LIVE_EXPORT LiveCursorState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("...")

    // ... existing properties ...

    /// Cross-block column-preservation state. Set by LiveNavigationController
    /// before each Up/Down cross; cleared on Left/Right or any non-vertical
    /// motion. -1.0 sentinel = unset. Spec §4.5 lifecycle rules.
    Q_PROPERTY(qreal desiredVisualX READ desiredVisualX
                                    WRITE setDesiredVisualX
                                    NOTIFY desiredVisualXChanged)
public:
    // ... existing ctors / methods ...

    qreal desiredVisualX() const noexcept { return m_desiredVisualX; }
    void  setDesiredVisualX(qreal x);
    Q_INVOKABLE void clearDesiredVisualX();

Q_SIGNALS:
    void cursorChanged();
    void desiredVisualXChanged();

private:
    // ... existing members ...
    qreal m_desiredVisualX = -1.0;
};
```

Edit `LiveCursorState.cpp`:

```cpp
void LiveCursorState::setDesiredVisualX(qreal x) {
    if (qFuzzyCompare(m_desiredVisualX, x)) return;
    m_desiredVisualX = x;
    Q_EMIT desiredVisualXChanged();
}

void LiveCursorState::clearDesiredVisualX() {
    setDesiredVisualX(-1.0);
}
```

- [ ] **Step 4: Run test to verify pass**

```
cmake --build build-dev --target tst_live_render_cursor -j 8
ctest --test-dir build-dev -R tst_live_render_cursor --output-on-failure
```

Expected: PASS for the three new slots; existing slots also pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp \
        libs/markoff-live/tests/tst_live_render_cursor.cpp
git commit -m "markoff-live: LiveCursorState::desiredVisualX (E2 column preservation)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task A4: Add `setLocalCaretPosition` and `setSelectionRange` to `InlineHighlighter`

These are the C++ slots that the QML layer (Phase C) will wire from the delegate's TextEdit. The slot triggers a `rehighlight()` so delimiter visibility recomputes.

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/InlineHighlighter.h`
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp`
- Test: extend `tst_live_render_inline_per_kind.cpp` (add new slots; do not break existing)

- [ ] **Step 1: Write the failing test**

Append to `tst_live_render_inline_per_kind.cpp` (use the existing fixture for setting up an `InlineHighlighter` against a `QTextDocument`):

```cpp
void set_local_caret_position_triggers_rehighlight() {
    QTextDocument doc;
    doc.setPlainText("**bold**");
    Markoff::Theme theme;
    InlineHighlighter h(&doc);
    h.setTheme(&theme);

    Markoff::SourceSpan boldSpan;
    boldSpan.charOffset = 2; boldSpan.charLength = 4;
    boldSpan.bold = true;
    Markoff::SourceSpan openMarker;
    openMarker.charOffset = 0; openMarker.charLength = 2;
    openMarker.bold = true; openMarker.isDelimiter = true;
    openMarker.parentCharStart = 0; openMarker.parentCharEnd = 8;
    Markoff::SourceSpan closeMarker;
    closeMarker.charOffset = 6; closeMarker.charLength = 2;
    closeMarker.bold = true; closeMarker.isDelimiter = true;
    closeMarker.parentCharStart = 0; closeMarker.parentCharEnd = 8;
    h.setInlineSpans({openMarker, boldSpan, closeMarker});

    QSignalSpy spy(&doc, &QTextDocument::contentsChange);  // proxy for rehighlight
    h.setLocalCaretPosition(4);  // inside the span
    QTRY_VERIFY(true);  // rehighlight is sync; just confirm no crash
    QCOMPARE(h.localCaretPosition(), 4);
    h.setLocalCaretPosition(20);
    QCOMPARE(h.localCaretPosition(), 20);
}

void set_selection_range_records_state() {
    QTextDocument doc;
    InlineHighlighter h(&doc);
    h.setSelectionRange(3, 7);
    QCOMPARE(h.selectionStart(), 3);
    QCOMPARE(h.selectionEnd(), 7);
    h.setSelectionRange(-1, -1);  // no-selection sentinel
    QCOMPARE(h.selectionStart(), -1);
    QCOMPARE(h.selectionEnd(), -1);
}
```

- [ ] **Step 2: Run test to verify failure**

```
ctest --test-dir build-dev -R tst_live_render_inline_per_kind --output-on-failure
```

Expected: FAIL — `setLocalCaretPosition` / `localCaretPosition` / `setSelectionRange` / `selectionStart` / `selectionEnd` undefined.

- [ ] **Step 3: Add the slots**

Edit `InlineHighlighter.h`:

```cpp
class MARKOFF_LIVE_EXPORT InlineHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit InlineHighlighter(QTextDocument *parent);
    ~InlineHighlighter() override;

    void setInlineSpans(const QList<Markoff::SourceSpan> &spans);
    const QList<Markoff::SourceSpan> &inlineSpans() const noexcept { return m_spans; }

    void setTheme(const Markoff::Theme *theme);
    const Markoff::Theme *theme() const noexcept { return m_theme; }

    /// Local caret qtPos within the bound block, or -1 for "no local caret
    /// in this block". Spec §3.1 / §4.4 — peer cursors do NOT use this slot.
    void setLocalCaretPosition(int qtPos);
    int  localCaretPosition() const noexcept { return m_localCaretPos; }

    /// Selection range within the bound block in qtPos coords. Either may
    /// be -1 to indicate no local selection in this block.
    void setSelectionRange(int startQtPos, int endQtPos);
    int  selectionStart() const noexcept { return m_selStart; }
    int  selectionEnd()   const noexcept { return m_selEnd;   }

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat formatFor(const Markoff::SourceSpan &span) const;
    bool delimiterShouldHide(const Markoff::SourceSpan &span) const;
    QTextCharFormat hiddenFormatForChar(QChar ch) const;

    QList<Markoff::SourceSpan> m_spans;
    const Markoff::Theme      *m_theme = nullptr;
    int                        m_localCaretPos = -1;
    int                        m_selStart      = -1;
    int                        m_selEnd        = -1;
};
```

Edit `InlineHighlighter.cpp` — add no-op-impl now (real logic lands in Phase B):

```cpp
void InlineHighlighter::setLocalCaretPosition(int qtPos) {
    if (m_localCaretPos == qtPos) return;
    m_localCaretPos = qtPos;
    rehighlight();
}

void InlineHighlighter::setSelectionRange(int startQtPos, int endQtPos) {
    if (m_selStart == startQtPos && m_selEnd == endQtPos) return;
    m_selStart = startQtPos;
    m_selEnd   = endQtPos;
    rehighlight();
}

bool InlineHighlighter::delimiterShouldHide(const Markoff::SourceSpan &span) const {
    Q_UNUSED(span);
    return false;  // Phase A: never hide. Phase B fills in real logic.
}

QTextCharFormat InlineHighlighter::hiddenFormatForChar(QChar ch) const {
    QTextCharFormat fmt;
    if (!m_theme) return fmt;
    fmt = m_theme->charFormat(Markoff::Theme::Slot::HiddenMarker);
    QFont f = fmt.font();
    const qreal advance = QFontMetricsF(f).horizontalAdvance(ch);
    f.setLetterSpacing(QFont::AbsoluteSpacing, -advance);
    fmt.setFont(f);
    return fmt;
}
```

(`#include <QFontMetricsF>` at the top of the .cpp.)

- [ ] **Step 4: Run test to verify pass**

```
cmake --build build-dev --target tst_live_render_inline_per_kind -j 8
ctest --test-dir build-dev -R tst_live_render_inline_per_kind --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/InlineHighlighter.h \
        libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_inline_per_kind.cpp
git commit -m "markoff-live: InlineHighlighter caret + selection slots (E2 §3.1)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task A5: Phase A green-tree gate

- [ ] **Run the full fast-loop test suite.**

```
ctest --test-dir build-dev -E 'tst_realistic|tst_benchmark' --output-on-failure -j 8
```

Expected: all green. If anything regresses, fix it before moving to Phase B.

---

## Phase B — Auto-hide implementation (TDD per behavior)

Goal: implement the real reveal/hide logic in `InlineHighlighter::highlightBlock` so delimiter spans whose parent range does not contain the caret render with the `HiddenMarker` format. Selection-cover and nested-span behaviors fall out of the same predicate.

The reveal predicate (per spec §4.1, §4.3):

```
shouldReveal(delimiterSpan, caret, selStart, selEnd) =
    caret ∈ [delimiterSpan.parentCharStart - 1, delimiterSpan.parentCharEnd + 1]
    OR selectionTouchesParent(selStart, selEnd, delimiterSpan.parentCharStart, delimiterSpan.parentCharEnd)
```

`selectionTouchesParent` = the selection range overlaps `[parentCharStart, parentCharEnd]` (any overlap, including endpoint-touch).

For non-delimiter spans, formatting from E1 is unchanged.

---

### Task B1: Symmetric kinds — bold/italic/strike/highlight/code-span hide on caret-out

**Files:**
- Modify: `libs/markoff-live/src/InlineHighlighter.cpp`
- Test: create `libs/markoff-live/tests/tst_live_render_e2_autohide_per_kind.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTextDocument>
#include <markoff/core/Theme.h>
#include <markoff/live/InlineHighlighter.h>
#include <markoff/parser/SourceSpan.h>

using namespace Markoff;
using Markoff::Live::InlineHighlighter;

namespace {
QTextCharFormat formatAt(QTextDocument *doc, int charPos) {
    auto *block = &doc->firstBlock();
    while (block->isValid() && (charPos < block->position() ||
           charPos >= block->position() + block->length())) {
        const QTextBlock next = block->next();
        if (!next.isValid()) break;
        *block = next;
    }
    auto layout = block->layout();
    for (const QTextLayout::FormatRange &fr : layout->formats()) {
        if (charPos - block->position() >= fr.start &&
            charPos - block->position() <  fr.start + fr.length)
            return fr.format;
    }
    return QTextCharFormat();
}

bool isHidden(const QTextCharFormat &fmt) {
    return fmt.font().letterSpacingType() == QFont::AbsoluteSpacing
        && fmt.font().letterSpacing() < 0.0;
}

SourceSpan delimiterSpan(int charOffset, int charLength,
                         int parentStart, int parentEnd,
                         /* kind setter */ auto setKind) {
    SourceSpan s;
    s.charOffset = charOffset; s.charLength = charLength;
    s.isDelimiter = true;
    s.parentCharStart = parentStart;
    s.parentCharEnd = parentEnd;
    setKind(s);
    return s;
}
SourceSpan contentSpan(int charOffset, int charLength,
                       /* kind setter */ auto setKind) {
    SourceSpan s;
    s.charOffset = charOffset; s.charLength = charLength;
    setKind(s);
    return s;
}
}

class TestE2AutohidePerKind : public QObject {
    Q_OBJECT
private slots:
    void bold_markers_hidden_when_caret_outside_span() {
        QTextDocument doc;
        doc.setPlainText("Some **bold** here");
        // Source positions: "Some " = 0-4; "**" = 5-6; "bold" = 7-10; "**" = 11-12; " here" = 13-17
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(5,  2, 5, 12, bold),
            contentSpan  (7,  4,         bold),
            delimiterSpan(11, 2, 5, 12, bold),
        });
        h.setLocalCaretPosition(0);  // "Some " — outside span
        QTRY_VERIFY(isHidden(formatAt(&doc, 5)));   // first '*'
        QTRY_VERIFY(isHidden(formatAt(&doc, 6)));   // second '*'
        QTRY_VERIFY(isHidden(formatAt(&doc, 11)));  // third '*'
        QTRY_VERIFY(isHidden(formatAt(&doc, 12)));  // fourth '*'
    }

    void bold_markers_revealed_when_caret_inside_span() {
        QTextDocument doc;
        doc.setPlainText("Some **bold** here");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(5,  2, 5, 12, bold),
            contentSpan  (7,  4,         bold),
            delimiterSpan(11, 2, 5, 12, bold),
        });
        h.setLocalCaretPosition(8);  // inside the bold content
        QTRY_VERIFY(!isHidden(formatAt(&doc, 5)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 6)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 11)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 12)));
    }

    void italic_strike_highlight_code_follow_same_predicate() {
        struct Case { const char *name; void (*setKind)(SourceSpan &); QString text; int markerLen; };
        const Case cases[] = {
            {"italic",    [](SourceSpan &s){ s.italic = true; }, "_x_", 1},
            {"strike",    [](SourceSpan &s){ s.strikethrough = true; }, "~~x~~", 2},
            {"highlight", [](SourceSpan &s){ s.highlight = true; }, "==x==", 2},
            {"code",      [](SourceSpan &s){ s.code = true; }, "`x`", 1},
        };
        for (const Case &c : cases) {
            QTextDocument doc;
            doc.setPlainText(c.text);
            Theme theme;
            InlineHighlighter h(&doc);
            h.setTheme(&theme);
            const int contentStart = c.markerLen;
            const int parentEnd = c.text.length();
            h.setInlineSpans({
                delimiterSpan(0, c.markerLen, 0, parentEnd, c.setKind),
                contentSpan  (contentStart, 1, c.setKind),
                delimiterSpan(contentStart + 1, c.markerLen, 0, parentEnd, c.setKind),
            });
            h.setLocalCaretPosition(-1);  // no local caret
            QTRY_VERIFY2(isHidden(formatAt(&doc, 0)),
                         qPrintable(QStringLiteral("%1 first marker").arg(c.name)));
            h.setLocalCaretPosition(contentStart);
            QTRY_VERIFY2(!isHidden(formatAt(&doc, 0)),
                         qPrintable(QStringLiteral("%1 first marker after caret-in").arg(c.name)));
        }
    }
};

QTEST_MAIN(TestE2AutohidePerKind)
#include "tst_live_render_e2_autohide_per_kind.moc"
```

Register in `libs/markoff-live/tests/CMakeLists.txt` per the existing inline-test pattern.

- [ ] **Step 2: Run to verify fail**

```
cmake --build build-dev --target tst_live_render_e2_autohide_per_kind -j 8
ctest --test-dir build-dev -R tst_live_render_e2_autohide_per_kind --output-on-failure
```

Expected: FAIL (`delimiterShouldHide` always returns false, so all delimiters render with their kind format, not hidden).

- [ ] **Step 3: Implement the predicate**

Edit `InlineHighlighter.cpp`:

```cpp
bool InlineHighlighter::delimiterShouldHide(const SourceSpan &span) const {
    if (!span.isDelimiter) return false;
    if (span.parentCharStart < 0 || span.parentCharEnd < 0) return false;

    // Selection touches the parent range? Reveal.
    if (m_selStart >= 0 && m_selEnd >= 0) {
        const int lo = std::min(m_selStart, m_selEnd);
        const int hi = std::max(m_selStart, m_selEnd);
        if (lo <= span.parentCharEnd && hi >= span.parentCharStart) return false;
    }

    // Caret in [parentCharStart - 1, parentCharEnd + 1]? Reveal.
    if (m_localCaretPos >= span.parentCharStart - 1 &&
        m_localCaretPos <= span.parentCharEnd + 1) {
        return false;
    }

    return true;  // hide
}
```

Now extend `highlightBlock` to apply the hidden format on hidden-delimiter chars (and otherwise paint as before):

```cpp
void InlineHighlighter::highlightBlock(const QString &text) {
    if (!m_theme) return;
    for (const SourceSpan &span : std::as_const(m_spans)) {
        if (span.charLength <= 0) continue;
        const bool hide = delimiterShouldHide(span);
        if (hide) {
            // Apply per-char hidden format (negative letterSpacing tuned per glyph).
            for (int i = span.charOffset; i < span.charOffset + span.charLength; ++i) {
                if (i < 0 || i >= text.length()) continue;
                QTextCharFormat merged = format(i);
                merged.merge(hiddenFormatForChar(text[i]));
                setFormat(i, 1, merged);
            }
            continue;
        }
        const QTextCharFormat spanFmt = formatFor(span);
        if (spanFmt == QTextCharFormat()) continue;
        for (int i = span.charOffset; i < span.charOffset + span.charLength; ++i) {
            QTextCharFormat merged = format(i);
            merged.merge(spanFmt);
            setFormat(i, 1, merged);
        }
    }
}
```

- [ ] **Step 4: Run to verify pass**

```
cmake --build build-dev --target tst_live_render_e2_autohide_per_kind -j 8
ctest --test-dir build-dev -R tst_live_render_e2_autohide_per_kind --output-on-failure
```

Expected: PASS for both new test methods.

- [ ] **Step 5: Run E1's tests to confirm no regression**

```
ctest --test-dir build-dev -R 'tst_live_render_inline_' --output-on-failure -j 8
```

Expected: all E1 tests still green. The new code path (`delimiterShouldHide`) only fires when `isDelimiter == true`, which is the parser's flag — pre-E2 spans where the parser didn't populate `isDelimiter` continue to render exactly as before.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_e2_autohide_per_kind.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: auto-hide delimiter spans on caret-out (E2 B1)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task B2: Caret-adjacent reveal trigger

The B1 predicate already reveals when `qtPos == parentCharStart - 1` (immediately before opening) or `qtPos == parentCharEnd + 1` (immediately after closing). This task adds explicit assertions for those boundary positions and confirms the predicate.

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_autohide_caret_adjacent.cpp`
- Modify: `libs/markoff-live/tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// (... header and helpers as in tst_live_render_e2_autohide_per_kind ...)
class TestE2CaretAdjacent : public QObject {
    Q_OBJECT
private slots:
    void caret_one_before_opening_reveals() {
        QTextDocument doc;
        doc.setPlainText("x **b** y");
        // "x " 0-1; "**" 2-3; "b" 4; "**" 5-6; " y" 7-8
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(2);   // caret at parentCharStart - 1 + 1 = 2 (start)
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));
        h.setLocalCaretPosition(1);   // caret at parentCharStart - 1 = 1
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));
        h.setLocalCaretPosition(0);   // caret 2 chars before parent — should hide
        QTRY_VERIFY(isHidden(formatAt(&doc, 2)));
    }

    void caret_one_after_closing_reveals() {
        QTextDocument doc;
        doc.setPlainText("x **b** y");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(7);  // parentCharEnd + 1 — should reveal
        QTRY_VERIFY(!isHidden(formatAt(&doc, 5)));
        h.setLocalCaretPosition(8);  // parentCharEnd + 2 — should hide
        QTRY_VERIFY(isHidden(formatAt(&doc, 5)));
    }
};

QTEST_MAIN(TestE2CaretAdjacent)
#include "tst_live_render_e2_autohide_caret_adjacent.moc"
```

(Helper functions `formatAt`, `isHidden`, `delimiterSpan`, `contentSpan` should be moved to a shared header, e.g. `libs/markoff-live/tests/E2TestHelpers.h`, to avoid copy-paste across all the e2-* tests. Do this consolidation in this task before writing the test body.)

- [ ] **Step 2: Run to verify pass** (B1's predicate already implements adjacent-reveal — these tests confirm)

```
cmake --build build-dev --target tst_live_render_e2_autohide_caret_adjacent -j 8
ctest --test-dir build-dev -R tst_live_render_e2_autohide_caret_adjacent --output-on-failure
```

Expected: PASS without further code changes (regression-style test).

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_e2_autohide_caret_adjacent.cpp \
        libs/markoff-live/tests/E2TestHelpers.h \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: caret-adjacent reveal regression tests (E2 B2)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task B3: Atomic reveal for link `[text](url)`

When the caret is anywhere in `[…](…)` OR adjacent, all marker-classified parts (`[`, `]`, `(`, URL-bytes, `)`) reveal. This depends entirely on the parser correctly emitting all those parts as `isDelimiter` SourceSpans with the SAME `parentCharStart`/`parentCharEnd` pair (the full link range). Verify this assumption first.

**Files:**
- Read: `libs/markoff-parser/src/...` to find link-emission code
- Test: create `libs/markoff-live/tests/tst_live_render_e2_autohide_link.cpp`

- [ ] **Step 1: Confirm parser emits the link as delimiter spans with shared parent range**

```
grep -rn "isLink\|isDelimiter.*Link\|parentChar.*Link" libs/markoff-parser/src/ | head -20
```

Read what spans the parser emits for `[text](url)`. Based on the SourceSpan flag set, expected spans for `[text](url)`:
- `[` — `isDelimiter=true`, `isLink=true`, parentCharStart/End covering `[text](url)`
- `text` — `isLink=true`, NOT delimiter
- `](` — `isDelimiter=true`, `isLink=true`, same parent range
- `url` — `isLink=true`, `isDelimiter=true` (the URL portion is markup, not display)
- `)` — `isDelimiter=true`, `isLink=true`, same parent range

If the parser does not yet emit this shape, **halt this task and add a parser-side fixup task before B3**. (As of 2026-05-08, E1 reads `inlineSpans` and was happy, but E1 didn't exercise `parentCharStart`. This is the first phase that does.)

- [ ] **Step 2: Write the test against the spec'd shape**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "E2TestHelpers.h"

class TestE2AutohideLink : public QObject {
    Q_OBJECT
private slots:
    void link_atomic_reveal_caret_in_display_text() {
        QTextDocument doc;
        doc.setPlainText("see [text](https://x) end");
        // "see " 0-3; "[" 4; "text" 5-8; "](" 9-10; "https://x" 11-19; ")" 20; " end" 21-24
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto link = [](SourceSpan &s){ s.isLink = true; };
        const int parentStart = 4, parentEnd = 20;
        h.setInlineSpans({
            delimiterSpan(4,  1,            parentStart, parentEnd, link),
            contentSpan  (5,  4,                                      link),
            delimiterSpan(9,  2,            parentStart, parentEnd, link),
            delimiterSpan(11, 9,            parentStart, parentEnd, link),
            delimiterSpan(20, 1,            parentStart, parentEnd, link),
        });
        h.setLocalCaretPosition(7);  // inside "text"
        // All five delimiter ranges should be revealed.
        QTRY_VERIFY(!isHidden(formatAt(&doc, 4)));   // [
        QTRY_VERIFY(!isHidden(formatAt(&doc, 9)));   // ]
        QTRY_VERIFY(!isHidden(formatAt(&doc, 10)));  // (
        QTRY_VERIFY(!isHidden(formatAt(&doc, 11)));  // url first char
        QTRY_VERIFY(!isHidden(formatAt(&doc, 19)));  // url last char
        QTRY_VERIFY(!isHidden(formatAt(&doc, 20)));  // )
    }

    void link_atomic_hide_caret_outside() {
        // ... mirror, caret at qtPos 0, all five delimiter ranges hidden
    }
};

QTEST_MAIN(TestE2AutohideLink)
#include "tst_live_render_e2_autohide_link.moc"
```

- [ ] **Step 3: Run to verify** — should pass without further code (B1 predicate already implements atomic reveal because all link-marker spans share the same `parentCharStart`/`parentCharEnd`)

```
ctest --test-dir build-dev -R tst_live_render_e2_autohide_link --output-on-failure
```

If FAIL: check the parser-side assumption. If parser is wrong, fix parser first; if predicate is wrong, fix predicate.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_e2_autohide_link.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: atomic link reveal (E2 B3)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task B4: Atomic reveal for wikilink `[[Page|alias]]`

Same shape as B3, but the parent range covers `[[Page|alias]]` and the delimiter spans include `[[`, `|`, `]]` and the Page-name-portion (when an alias is present, Page is markup; alias is display).

**Files:**
- Test: create `libs/markoff-live/tests/tst_live_render_e2_autohide_wikilink.cpp`

- [ ] **Step 1: Verify parser shape for wikilinks**

```
grep -rn "isWikilink\|wikilink.*delimiter" libs/markoff-parser/src/ | head -20
```

- [ ] **Step 2: Write the test mirroring B3, with two sub-tests: aliased and plain**

(Test code follows the exact pattern of B3; for `[[Plain Page]]`, no alias spans, only `[[`, page-name, `]]`. For `[[Page|alias]]`, the page-name-portion is also a delimiter span. Caret in alias → all reveal; caret outside → all hide.)

- [ ] **Step 3: Run to verify pass**

Expected: PASS without further code changes.

- [ ] **Step 4: Commit**

---

### Task B5: Tag — always shown invariant

The parser emits `isTag` spans for `#tagname`. The `#` char is part of the tag content (or is its own delimiter span?) — verify shape and ensure auto-hide does NOT collapse it.

**Files:**
- Test: extend `tst_live_render_e2_autohide_per_kind.cpp` with a tag test

- [ ] **Step 1: Verify the parser's tag emission**

```
grep -rn "isTag\|tag.*delimiter\|tag.*span" libs/markoff-parser/src/ | head
```

If the parser emits the leading `#` as `isDelimiter=true, isTag=true`, the B1 predicate would currently hide it on caret-out. This is contrary to spec §4.1 which requires tag `#` to always show.

- [ ] **Step 2: Write the test**

```cpp
void tag_hash_always_shown_regardless_of_caret() {
    QTextDocument doc;
    doc.setPlainText("Filed under #research and more");
    // "#research" at 12-20
    Theme theme;
    InlineHighlighter h(&doc);
    h.setTheme(&theme);
    auto tag = [](SourceSpan &s){ s.isTag = true; };
    h.setInlineSpans({
        delimiterSpan(12, 1, 12, 20, tag),  // '#'
        contentSpan  (13, 8,            tag),  // 'research'
    });
    h.setLocalCaretPosition(0);   // caret far away
    QTRY_VERIFY(!isHidden(formatAt(&doc, 12)));  // '#' MUST stay visible
    h.setLocalCaretPosition(50);  // caret far away (position past end is fine)
    QTRY_VERIFY(!isHidden(formatAt(&doc, 12)));
}
```

- [ ] **Step 3: Run — expect failure**

```
ctest --test-dir build-dev -R tst_live_render_e2_autohide_per_kind --output-on-failure
```

Expected: FAIL — current `delimiterShouldHide` predicate hides any delimiter span when caret is far away, including tag `#`.

- [ ] **Step 4: Add the per-kind exception**

Edit `delimiterShouldHide` in `InlineHighlighter.cpp`:

```cpp
bool InlineHighlighter::delimiterShouldHide(const SourceSpan &span) const {
    if (!span.isDelimiter) return false;

    // Tag '#' never auto-hides — it is the tag's visual identity (spec §4.1).
    if (span.isTag) return false;

    // Block-level delimiters that ALWAYS show: list-item bullets and
    // blockquote markers. Spec §4.2.
    if (span.isListMarker || span.isBlockquoteMarker) return false;

    if (span.parentCharStart < 0 || span.parentCharEnd < 0) return false;

    // ... rest of selection-cover + caret-range checks unchanged
}
```

- [ ] **Step 5: Run to verify pass**

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/src/InlineHighlighter.cpp \
        libs/markoff-live/tests/tst_live_render_e2_autohide_per_kind.cpp
git commit -m "markoff-live: tag/list/blockquote markers never auto-hide (E2 B5/B8)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

This commit also covers Task B8's invariants (list bullet, blockquote `>`); B8 below is a regression-style test for them.

---

### Task B6: Heading `#` prefix — block-level hide/reveal

The parser emits the heading prefix `#` (or `##`, `###`, ...) as `isDelimiter=true, isHeading=true`. The reveal trigger is "caret in the heading block (anywhere)" rather than "caret in `[parentCharStart-1, parentCharEnd+1]`."

This is a different predicate. The heading prefix's `parentCharStart`/`parentCharEnd` should cover the whole block, but verify.

**Files:**
- Test: create `libs/markoff-live/tests/tst_live_render_e2_autohide_block_prefix.cpp`
- Modify if needed: `libs/markoff-live/src/InlineHighlighter.cpp`

- [ ] **Step 1: Verify parser's heading-prefix emission**

```
grep -rn "isHeading\|heading.*delimiter\|heading.*marker" libs/markoff-parser/src/ | head
```

Expected: heading prefix is emitted as a delimiter span with `parentCharStart=0, parentCharEnd=blockLen` (covers the whole block) OR specific to the prefix only. If the parent range doesn't already cover the whole block, the B1 predicate gives "caret near prefix only reveals." That's wrong for spec §4.2; the spec requires "caret anywhere in heading block reveals prefix."

- [ ] **Step 2: Write the test**

```cpp
class TestE2AutohideBlockPrefix : public QObject {
    Q_OBJECT
private slots:
    void heading_prefix_hides_when_caret_outside_block() {
        QTextDocument doc;
        doc.setPlainText("# My heading");
        // '#' at 0; ' ' at 1; "My heading" at 2-11
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto heading = [](SourceSpan &s){
            s.isHeading = true; s.headingLevel = 1;
        };
        h.setInlineSpans({
            // Per spec, the heading-prefix delimiter span covers the prefix
            // and trailing space, with parent range = entire block.
            delimiterSpan(0, 2, 0, 12, heading),
        });
        h.setLocalCaretPosition(-1);  // no local caret
        QTRY_VERIFY(isHidden(formatAt(&doc, 0)));
        QTRY_VERIFY(isHidden(formatAt(&doc, 1)));
    }

    void heading_prefix_reveals_when_caret_anywhere_in_block() {
        QTextDocument doc;
        doc.setPlainText("# My heading");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto heading = [](SourceSpan &s){
            s.isHeading = true; s.headingLevel = 1;
        };
        h.setInlineSpans({
            delimiterSpan(0, 2, 0, 12, heading),
        });
        // Caret at end of heading content
        h.setLocalCaretPosition(11);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
        QTRY_VERIFY(!isHidden(formatAt(&doc, 1)));
        // Caret in middle of heading content
        h.setLocalCaretPosition(6);
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    }
};

QTEST_MAIN(TestE2AutohideBlockPrefix)
#include "tst_live_render_e2_autohide_block_prefix.moc"
```

- [ ] **Step 3: Run to verify**

If parser emits heading-prefix delimiters with `parentCharStart=0, parentCharEnd=blockLen`, B1's predicate already handles this — the test passes. If parser does not, this test fails and the parser must be fixed (separate parser commit).

- [ ] **Step 4: If parser fix needed:** add a sub-task for the parser to populate `parentCharStart`/`parentCharEnd` correctly for heading-prefix delimiter spans. Otherwise skip.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/tests/tst_live_render_e2_autohide_block_prefix.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: heading prefix auto-hide test (E2 B6)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task B7: Code-fence lines — collapse-and-reveal

`isCodeBlockFence` flag on SourceSpan. The fence chars (` ``` ` opening, language tag, closing ` ``` `) participate in auto-hide.

- [ ] **Step 1: Append to `tst_live_render_e2_autohide_block_prefix.cpp`** (same fixture):

```cpp
void code_fence_collapses_when_caret_outside_block() {
    QTextDocument doc;
    doc.setPlainText("```python\nprint(\"hi\")\n```");
    // "```python" 0-8; "\n" 9; "print(...)" 10-21; "\n" 22; "```" 23-25
    Theme theme;
    InlineHighlighter h(&doc);
    h.setTheme(&theme);
    auto fence = [](SourceSpan &s){ s.isCodeBlockFence = true; };
    h.setInlineSpans({
        delimiterSpan(0,  9, 0, 26, fence),  // ```python
        delimiterSpan(23, 3, 0, 26, fence),  // closing ```
    });
    h.setLocalCaretPosition(-1);
    QTRY_VERIFY(isHidden(formatAt(&doc, 0)));   // first '`'
    QTRY_VERIFY(isHidden(formatAt(&doc, 8)));   // 'n' of python
    QTRY_VERIFY(isHidden(formatAt(&doc, 23)));  // closing '`'
    QTRY_VERIFY(isHidden(formatAt(&doc, 25)));  // closing '`' last
}

void code_fence_reveals_when_caret_in_block() {
    QTextDocument doc;
    doc.setPlainText("```python\nprint(\"hi\")\n```");
    Theme theme;
    InlineHighlighter h(&doc);
    h.setTheme(&theme);
    auto fence = [](SourceSpan &s){ s.isCodeBlockFence = true; };
    h.setInlineSpans({
        delimiterSpan(0,  9, 0, 26, fence),
        delimiterSpan(23, 3, 0, 26, fence),
    });
    h.setLocalCaretPosition(15);  // inside print(...)
    QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    QTRY_VERIFY(!isHidden(formatAt(&doc, 23)));
}
```

- [ ] **Step 2: Run to verify** — should pass via B1 predicate (parent range covers whole block).

- [ ] **Step 3: Commit**

---

### Task B8: List bullet / blockquote — always-shown invariants (regression test)

B5's commit already implemented the per-kind exception. This task is a regression-style test that confirms.

- [ ] **Step 1: Append to `tst_live_render_e2_autohide_block_prefix.cpp`**:

```cpp
void list_bullet_always_shown() {
    QTextDocument doc;
    doc.setPlainText("- An item");
    Theme theme;
    InlineHighlighter h(&doc);
    h.setTheme(&theme);
    auto listMarker = [](SourceSpan &s){ s.isListMarker = true; };
    h.setInlineSpans({
        delimiterSpan(0, 2, 0, 9, listMarker),  // "- "
    });
    h.setLocalCaretPosition(-1);
    QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    h.setLocalCaretPosition(5);
    QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
}

void blockquote_marker_always_shown() {
    QTextDocument doc;
    doc.setPlainText("> A quote");
    Theme theme;
    InlineHighlighter h(&doc);
    h.setTheme(&theme);
    auto bq = [](SourceSpan &s){ s.isBlockquoteMarker = true; };
    h.setInlineSpans({
        delimiterSpan(0, 2, 0, 9, bq),  // "> "
    });
    h.setLocalCaretPosition(-1);
    QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
    h.setLocalCaretPosition(5);
    QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));
}
```

- [ ] **Step 2: Run to verify pass** — B5's commit already covered the predicate.

- [ ] **Step 3: Commit**

---

### Task B9: Nested span reveal

Italic-in-bold: caret in italic → both italic and bold markers reveal.

The B1 predicate already produces this behavior (every span whose `parentCharStart`/`parentCharEnd` contains the caret reveals; nested spans have nested parent ranges that BOTH contain the caret). This task is a regression test.

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_autohide_nested.cpp`

- [ ] **Step 1: Write the test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "E2TestHelpers.h"

class TestE2AutohideNested : public QObject {
    Q_OBJECT
private slots:
    void caret_in_italic_inside_bold_reveals_both() {
        QTextDocument doc;
        doc.setPlainText("**a _b_ c**");
        // "**" 0-1; "a " 2-3; "_" 4; "b" 5; "_" 6; " c" 7-8; "**" 9-10
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        auto boldItalic = [](SourceSpan &s){ s.bold = true; s.italic = true; };
        auto italic = [](SourceSpan &s){ s.italic = true; };
        // Bold parent range: 0..10 (full source). Italic parent range: 4..6.
        h.setInlineSpans({
            delimiterSpan(0,  2, 0, 10, bold),
            contentSpan  (2,  2,        bold),
            delimiterSpan(4,  1, 4,  6, italic),
            contentSpan  (5,  1,        boldItalic),
            delimiterSpan(6,  1, 4,  6, italic),
            contentSpan  (7,  2,        bold),
            delimiterSpan(9,  2, 0, 10, bold),
        });
        h.setLocalCaretPosition(5);  // inside italic 'b'
        // Both italic underscores AND bold asterisks should reveal.
        QTRY_VERIFY(!isHidden(formatAt(&doc, 0)));   // **
        QTRY_VERIFY(!isHidden(formatAt(&doc, 1)));   // **
        QTRY_VERIFY(!isHidden(formatAt(&doc, 4)));   // _
        QTRY_VERIFY(!isHidden(formatAt(&doc, 6)));   // _
        QTRY_VERIFY(!isHidden(formatAt(&doc, 9)));   // **
        QTRY_VERIFY(!isHidden(formatAt(&doc, 10)));  // **
    }
};

QTEST_MAIN(TestE2AutohideNested)
#include "tst_live_render_e2_autohide_nested.moc"
```

- [ ] **Step 2: Run** — should pass via B1 predicate.

- [ ] **Step 3: Commit**

---

### Task B10: Selection cover reveal

Selection touching a span's parent range → that span's markers reveal regardless of caret.

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_autohide_selection.cpp`

- [ ] **Step 1: Write the test**

```cpp
class TestE2AutohideSelection : public QObject {
    Q_OBJECT
private slots:
    void selection_touching_span_reveals_markers() {
        QTextDocument doc;
        doc.setPlainText("a **b** c");
        // "a " 0-1; "**" 2-3; "b" 4; "**" 5-6; " c" 7-8
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(0);  // far from span
        h.setSelectionRange(0, 4);   // selection from start to inside span
        QTRY_VERIFY(!isHidden(formatAt(&doc, 2)));  // first '**' revealed
        QTRY_VERIFY(!isHidden(formatAt(&doc, 5)));  // closing '**' revealed
    }

    void selection_fully_outside_does_not_reveal() {
        QTextDocument doc;
        doc.setPlainText("a **b** c");
        Theme theme;
        InlineHighlighter h(&doc);
        h.setTheme(&theme);
        auto bold = [](SourceSpan &s){ s.bold = true; };
        h.setInlineSpans({
            delimiterSpan(2, 2, 2, 6, bold),
            contentSpan  (4, 1,        bold),
            delimiterSpan(5, 2, 2, 6, bold),
        });
        h.setLocalCaretPosition(-1);
        h.setSelectionRange(7, 9);  // ' c' — fully past span
        QTRY_VERIFY(isHidden(formatAt(&doc, 2)));
    }
};

QTEST_MAIN(TestE2AutohideSelection)
#include "tst_live_render_e2_autohide_selection.moc"
```

- [ ] **Step 2: Run** — should pass via B1 predicate (selection-cover branch is in there).

- [ ] **Step 3: Commit**

---

### Task B11: Peer cursor invariant (does NOT trigger reveal)

The local-caret slot is wired ONLY from the local TextEdit's `cursorPositionChanged`, never from `LiveCursorState`'s peer-cursor stream. Verifying this is structural — the test asserts that calling `LiveCursorState`'s peer-cursor mutator (or simulating one) does not call `setLocalCaretPosition`.

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_autohide_peer_cursor.cpp`

- [ ] **Step 1: Find the peer-cursor entry point**

```
grep -rn "remoteCursor\|peerCursor\|setRemoteCursor" libs/markoff-live/include/markoff/live/ libs/markoff-live/src/ | head
```

This was added in D5. Find the model that holds peer cursor positions (`remoteCursorsModel` per the LiveView Repeater observation earlier).

- [ ] **Step 2: Write the test**

```cpp
// Setup: use LiveListModelBinding fixture (sibling to existing tst_d5_remote_cursor_render.cpp).
// 1. Construct binding + cursor state.
// 2. Inject a peer cursor at a position inside a bold span.
// 3. Verify the InlineHighlighter for that block has localCaretPosition == -1.
// 4. Verify the bold span markers are STILL hidden (peer cursor doesn't trigger reveal).
//
// Code follows the existing tst_d5_remote_cursor_render setup pattern.
```

(Detailed code follows the D5 remote-cursor test fixture in `tst_d5_remote_cursor_render.cpp` — read that file before writing this test, mirror its construction.)

- [ ] **Step 3: Run** — should pass without further code changes (the wiring in Phase C will deliberately NOT route peer cursors to setLocalCaretPosition).

- [ ] **Step 4: Commit**

---

### Task B12: Phase B green-tree gate

- [ ] **Run full markoff-live test suite**

```
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green. Commit any test fixes that surfaced.

---

## Phase C — Wire auto-hide into delegates (QML)

Goal: route the QML TextEdit's `cursorPositionChanged` and `selectionChanged` signals into the `InlineHighlighter`'s new slots via `InlineHighlighterAttached`.

---

### Task C1: Extend `InlineHighlighterAttached` with caret + selection properties

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h`
- Modify: `libs/markoff-live/src/InlineHighlighterAttached.cpp`

- [ ] **Step 1: Add Q_PROPERTY entries**

```cpp
class MARKOFF_LIVE_EXPORT InlineHighlighterAttached : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QVariantList spans          READ spans  WRITE setSpans  NOTIFY spansChanged)
    Q_PROPERTY(const Markoff::Theme *theme READ theme  WRITE setTheme  NOTIFY themeChanged)
    Q_PROPERTY(int caretPosition           READ caretPosition WRITE setCaretPosition NOTIFY caretPositionChanged)
    Q_PROPERTY(int selectionStart          READ selectionStart WRITE setSelectionStart NOTIFY selectionStartChanged)
    Q_PROPERTY(int selectionEnd            READ selectionEnd   WRITE setSelectionEnd   NOTIFY selectionEndChanged)
    // ...
public:
    int  caretPosition()  const noexcept { return m_caretPos; }
    void setCaretPosition(int qtPos);
    int  selectionStart() const noexcept { return m_selStart; }
    void setSelectionStart(int qtPos);
    int  selectionEnd()   const noexcept { return m_selEnd; }
    void setSelectionEnd(int qtPos);
Q_SIGNALS:
    void caretPositionChanged();
    void selectionStartChanged();
    void selectionEndChanged();
private:
    int m_caretPos = -1;
    int m_selStart = -1;
    int m_selEnd   = -1;
};
```

- [ ] **Step 2: Implement the setters; forward to highlighter**

```cpp
void InlineHighlighterAttached::setCaretPosition(int qtPos) {
    if (m_caretPos == qtPos) return;
    m_caretPos = qtPos;
    if (m_highlighter) m_highlighter->setLocalCaretPosition(qtPos);
    Q_EMIT caretPositionChanged();
}
// (selection setters mirror this; both call setSelectionRange combined)
void InlineHighlighterAttached::setSelectionStart(int qtPos) {
    if (m_selStart == qtPos) return;
    m_selStart = qtPos;
    if (m_highlighter) m_highlighter->setSelectionRange(m_selStart, m_selEnd);
    Q_EMIT selectionStartChanged();
}
// ... selectionEnd similar
```

In `rebuildHighlighter`, after constructing the new `m_highlighter`, push the current state:

```cpp
m_highlighter->setLocalCaretPosition(m_caretPos);
m_highlighter->setSelectionRange(m_selStart, m_selEnd);
```

- [ ] **Step 3: Run existing E1 tests to confirm no regression**

```
ctest --test-dir build-dev -R 'tst_live_render_inline' --output-on-failure -j 8
```

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/include/markoff/live/InlineHighlighterAttached.h \
        libs/markoff-live/src/InlineHighlighterAttached.cpp
git commit -m "markoff-live: InlineHighlighterAttached caret + selection props (E2 C1)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task C2: Wire `ParagraphDelegate` to forward caret + selection

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`

- [ ] **Step 1: Edit the delegate's TextEdit**

Find the `InlineHighlighterAttached { ... }` block (currently lines 43-47 of `ParagraphDelegate.qml`). Extend it:

```qml
InlineHighlighterAttached {
    target: edit.textDocument
    spans: model.inlineSpans
    theme: root.liveBinding ? root.liveBinding.theme : null
    caretPosition: edit.activeFocus ? edit.cursorPosition : -1
    selectionStart: (edit.activeFocus && edit.selectionStart !== edit.selectionEnd)
                    ? edit.selectionStart : -1
    selectionEnd: (edit.activeFocus && edit.selectionStart !== edit.selectionEnd)
                  ? edit.selectionEnd : -1
}
```

The `edit.activeFocus` gate ensures peer-cursor focus events (handled elsewhere; not via QML focus) do not trigger reveal — only the local active-focus delegate exposes its caret position. Spec §4.4.

- [ ] **Step 2: Smoke-test by running the markoff-live-app**

```
cmake --build build-dev --target markoff-live-app -j 8
./build-dev/bin/markoff-live-app docs/specs/2026-05-08-e2-cursor-aware-view-design.md
```

Click into a paragraph. Expected: bold/italic/etc. markers near the caret reveal; markers far from caret hide. **This is a manual smoke test, not a gate** — the formal acceptance is in Phase I dogfood.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/qml/delegates/ParagraphDelegate.qml
git commit -m "markoff-live: ParagraphDelegate wires caret/selection to highlighter (E2 C2)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task C3: Wire other text-bearing delegates

**Files:**
- Modify: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`

- [ ] **Step 1: Apply the same `InlineHighlighterAttached` extension** as C2 to each delegate.

- [ ] **Step 2: Smoke-test app**: type into a heading, list-item, blockquote, code-block; confirm reveal/hide.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-live/qml/delegates/{Heading,ListItem,Blockquote,CodeBlock}Delegate.qml
git commit -m "markoff-live: text-bearing delegates wire caret/selection (E2 C3)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task C4: Cross-delegate sanity test

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_autohide_cross_delegate.cpp`

- [ ] **Step 1: Mirror the existing `tst_live_render_inline_cross_delegate.cpp` setup; add slots that exercise auto-hide for each delegate.**

(The existing E1 cross-delegate test is the right shape — it instantiates a QQuickView with a representative document and asserts highlight behavior across delegates. Extend with caret-position-driven assertions.)

- [ ] **Step 2: Run** — expect pass given C2/C3 wiring is correct.

- [ ] **Step 3: Commit**

---

### Task C5: Phase C green-tree gate

- [ ] **Run full markoff-live test suite**

```
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

---

## Phase D — `LiveNavigationController` skeleton

Goal: scaffold the controller, register it on the binding, and wire empty stubs that delegates can call. Real key handling lands in Phase E.

---

### Task D1: Create the `LiveNavigationController` class

**Files:**
- Create: `libs/markoff-live/include/markoff/live/LiveNavigationController.h`
- Create: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/CMakeLists.txt` (add new source)
- Modify: `libs/markoff-live/include/markoff/live/LiveListModelBinding.h` (expose accessor)
- Modify: `libs/markoff-live/src/LiveListModelBinding.cpp` (instantiate)
- Test: create `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp` (stub — empty test class for now)

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveNavigationController.h>

class TestE2NavArrows : public QObject {
    Q_OBJECT
private slots:
    void controller_is_exposed_on_binding() {
        // Construct a LiveListModelBinding via the same fixture pattern as
        // tst_live_render_paragraph_edit (parser, document, model, etc.).
        // Then:
        //   QVERIFY(binding.navigationController() != nullptr);
    }
};

QTEST_MAIN(TestE2NavArrows)
#include "tst_live_render_e2_nav_arrows.moc"
```

(Use the existing test-fixture pattern from `tst_live_render_paragraph_edit.cpp` to construct the binding.)

- [ ] **Step 2: Run to verify failure** — `LiveNavigationController` does not exist.

- [ ] **Step 3: Create the header**

`libs/markoff-live/include/markoff/live/LiveNavigationController.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <QObject>
#include <Qt>
#include <qqmlintegration.h>

class QQuickItem;

namespace Markoff::Live {

class LiveBlockModel;
class LiveCursorState;
class LiveSelectionView;
class BlockKindRegistry;

/// Cross-block keyboard navigation. Sibling to LiveStructuralKeyHandler
/// (which handles structural mutation keys); this class handles cursor
/// motion across blocks. Spec §2.2 / §3.2.
class MARKOFF_LIVE_EXPORT LiveNavigationController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveNavigationController is provided by LiveListModelBinding")

public:
    enum HandleResult { NotHandled = 0, Handled = 1 };
    Q_ENUM(HandleResult)

    explicit LiveNavigationController(const BlockKindRegistry *registry,
                                      LiveBlockModel          *model,
                                      LiveCursorState         *cursorState,
                                      LiveSelectionView       *selectionView,
                                      QObject                 *parent = nullptr);

    /// QML entry. Called by per-delegate Keys.onPressed. Returns
    /// HandleResult::Handled when the controller has dispatched the key
    /// (caller should set event.accepted = true). Returns NotHandled
    /// when the key should fall through to TextEdit's default.
    ///
    /// `editItem` is the delegate's TextEdit (QQuickItem *) so the
    /// controller can read cursorRectangle/positionAt. `qtPos`,
    /// `blockIndex`, `blockText` come from the delegate as in the
    /// existing structural-key dispatch.
    Q_INVOKABLE int tryHandle(int key, int modifiers,
                              int blockIndex, int qtPos,
                              QObject *editItem,
                              const QString &blockText);

private:
    int previousNavigableRow(int currentRow) const;
    int nextNavigableRow(int currentRow) const;
    bool isAtVisualTopLine(QObject *editItem) const;
    bool isAtVisualBottomLine(QObject *editItem) const;

    const BlockKindRegistry *m_registry;
    LiveBlockModel          *m_model;
    LiveCursorState         *m_cursorState;
    LiveSelectionView       *m_selectionView;
};

}  // namespace Markoff::Live
```

`libs/markoff-live/src/LiveNavigationController.cpp` — stub:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveNavigationController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/BlockKindRegistry.h>

namespace Markoff::Live {

LiveNavigationController::LiveNavigationController(
    const BlockKindRegistry *registry, LiveBlockModel *model,
    LiveCursorState *cursorState, LiveSelectionView *selectionView,
    QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_model(model)
    , m_cursorState(cursorState)
    , m_selectionView(selectionView)
{
}

int LiveNavigationController::tryHandle(int /*key*/, int /*modifiers*/,
                                        int /*blockIndex*/, int /*qtPos*/,
                                        QObject * /*editItem*/,
                                        const QString & /*blockText*/) {
    return NotHandled;  // Phase D stub. Phase E fills in arrow handlers.
}

int LiveNavigationController::previousNavigableRow(int currentRow) const {
    return currentRow > 0 ? currentRow - 1 : -1;
}
int LiveNavigationController::nextNavigableRow(int currentRow) const {
    if (!m_model) return -1;
    return currentRow + 1 < m_model->rowCount() ? currentRow + 1 : -1;
}

bool LiveNavigationController::isAtVisualTopLine(QObject * /*editItem*/) const {
    return false;  // Phase E.
}
bool LiveNavigationController::isAtVisualBottomLine(QObject * /*editItem*/) const {
    return false;  // Phase E.
}

}  // namespace Markoff::Live
```

- [ ] **Step 4: Add to CMakeLists**

`libs/markoff-live/CMakeLists.txt`:

```cmake
# Find the existing markoff_live_SOURCES list and add:
src/LiveNavigationController.cpp
include/markoff/live/LiveNavigationController.h
```

- [ ] **Step 5: Expose on binding**

`LiveListModelBinding.h`:

```cpp
class LiveNavigationController;

class MARKOFF_LIVE_EXPORT LiveListModelBinding : public QObject {
    // ...
    Q_PROPERTY(LiveNavigationController *navigationController
               READ navigationController CONSTANT)
public:
    LiveNavigationController *navigationController() const noexcept {
        return m_navigationController;
    }
private:
    LiveNavigationController *m_navigationController = nullptr;
};
```

`LiveListModelBinding.cpp` constructor — instantiate after cursor state, selection view, and registry are ready:

```cpp
m_navigationController = new LiveNavigationController(
    m_registry, m_model, m_cursorState, m_selectionView, this);
```

Add `#include <markoff/live/LiveNavigationController.h>` at top.

- [ ] **Step 6: Run test to verify pass**

```
cmake --build build-dev --target tst_live_render_e2_nav_arrows -j 8
ctest --test-dir build-dev -R tst_live_render_e2_nav_arrows --output-on-failure
```

Expected: PASS — controller exists; binding exposes it.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveNavigationController.h \
        libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/include/markoff/live/LiveListModelBinding.h \
        libs/markoff-live/src/LiveListModelBinding.cpp \
        libs/markoff-live/CMakeLists.txt \
        libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: LiveNavigationController skeleton (E2 D1)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task D2: Implement `previousNavigableRow` / `nextNavigableRow` (skip BlockKindRegistry, walk model)

The skeleton stub from D1 just walks ±1. The real version filters by which kinds are navigable (all kinds are navigable in E2 — text-bearing blocks accept TextCaret, non-text accept BlockSelected, both forms count for nav purposes).

The current stub IS correct for E2 (every model row is navigable since BlockKindRegistry admits one or another cursor variant on every kind). Confirm by writing a regression test.

**Files:**
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp`

- [ ] **Step 1: Add tests**

```cpp
void prev_navigable_row_walks_back() {
    // Construct binding with 5 rows.
    // Verify: nav.previousNavigableRow(3) == 2; nav.previousNavigableRow(0) == -1.
}
void next_navigable_row_walks_forward() {
    // Verify: nav.nextNavigableRow(3) == 4; nav.nextNavigableRow(4) == -1.
}
```

(Expose `previousNavigableRow` / `nextNavigableRow` as public for testing — they're const and stateless. Or wrap in `Q_INVOKABLE` if QML wants them later.)

- [ ] **Step 2: Run** — should pass via D1 stub.

- [ ] **Step 3: Commit**

---

## Phase E — Basic four-arrow nav

Goal: implement arrow-key cross-block dispatch so pressing Up at the visual-top line of paragraph N lands the caret on row N-1 at the column matching `cursorRectangle().x`. Likewise Down/Left/Right.

The phase has two halves:
- **C++ controller** — dispatch logic in `LiveNavigationController::tryHandle`.
- **QML wiring** — per-delegate `Keys.onPressed` forwards arrow keys to the controller.

---

### Task E1: Up at visual-top-line crosses to prev block (column-preserved)

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void up_at_visual_top_line_crosses_to_prev_block_at_matching_column() {
    // Construct a 2-block doc:
    //   Block 0: "Lorem ipsum dolor sit amet"
    //   Block 1: "Consectetur"
    // Place caret at qtPos 4 of block 1 ("Cons|"). visualX ≈ width of "Cons".
    // Simulate Up: tryHandle(Key_Up, 0, blockIndex=1, qtPos=4, editItem=block1Edit, blockText)
    // Expected:
    //   - return Handled
    //   - cursorState.desiredVisualX is set to ≈ width-of-"Cons"
    //   - cursorState.requestTextCaretAtRow called with (0, qtPosOnBlock0LastVisualLineAtSameX)
    //
    // Test plan: use a QQuickView with a synthetic LiveView to render the
    // delegates so cursorRectangle() returns real values. See
    // tst_live_render_inline_cross_delegate.cpp for the QQuickView setup.
}
```

- [ ] **Step 2: Run to verify failure** — stub returns NotHandled.

- [ ] **Step 3: Implement the visual-edge detection**

In `LiveNavigationController.cpp`, fill in `isAtVisualTopLine`:

```cpp
bool LiveNavigationController::isAtVisualTopLine(QObject *editItem) const {
    if (!editItem) return false;
    const QVariant rectV = editItem->property("cursorRectangle");
    if (!rectV.canConvert<QRectF>()) return false;
    const QRectF cursorRect = rectV.toRectF();
    bool ok = false;
    const qreal lineHeight = editItem->property("contentHeight").toReal(&ok);
    if (!ok || lineHeight <= 0) return false;
    // We're on the top visual line if cursorRect.y is close to 0.
    return cursorRect.y() < cursorRect.height() * 0.5;
}
```

(Property names — `cursorRectangle`, `contentHeight` — are QML TextEdit properties accessible from C++ via QObject::property.)

Symmetric `isAtVisualBottomLine` reads `editItem->property("contentHeight")` and compares against `cursorRect.bottom()`.

- [ ] **Step 4: Add `requestTextCaretAtRowVisualX` to `LiveCursorState`**

The existing `requestTextCaretAtRow(row, qtPos)` takes an explicit qtPos. We need a sibling method that says "place at the row, but use `desiredVisualX` + a visual-line hint to compute qtPos in the target delegate." Adding this as a new method (rather than overloading -1) avoids ambiguity with existing -1 usage in the codebase.

In `LiveCursorState.h`:

```cpp
public:
    enum class VisualLineHint { None, FirstLine, LastLine };
    Q_ENUM(VisualLineHint)

    /// Like requestTextCaretAtRow, but qtPos is computed by the destination
    /// delegate from `desiredVisualX` projected onto the line indicated by
    /// `hint`. Used by LiveNavigationController for Up/Down crosses.
    /// The destination delegate's focusEditAt() consults
    /// pendingVisualLineHint() and desiredVisualX() to position the caret.
    void requestTextCaretAtRowVisualX(int expectedRow, VisualLineHint hint);

    VisualLineHint pendingVisualLineHint() const noexcept { return m_pendingVlhint; }

Q_SIGNALS:
    void visualLineHintChanged();

private:
    VisualLineHint m_pendingVlhint = VisualLineHint::None;
```

Implementation in `LiveCursorState.cpp`:

```cpp
void LiveCursorState::requestTextCaretAtRowVisualX(int expectedRow, VisualLineHint hint) {
    m_pendingVlhint = hint;
    Q_EMIT visualLineHintChanged();
    // Use a sentinel row-only request; the delegate's focusEditAt
    // resolves qtPos via pendingVisualLineHint + desiredVisualX.
    requestTextCaretAtRow(expectedRow, 0);  // qtPos=0 is overridden by hint path
}
```

Test the new method (append to `tst_live_render_cursor.cpp`):

```cpp
void request_text_caret_at_row_visual_x_records_hint() {
    LiveCursorState cs(/* fixture args */);
    cs.setDesiredVisualX(123.0);
    cs.requestTextCaretAtRowVisualX(2, LiveCursorState::VisualLineHint::LastLine);
    QCOMPARE(cs.pendingVisualLineHint(), LiveCursorState::VisualLineHint::LastLine);
    QCOMPARE(cs.desiredVisualX(), 123.0);
}
```

- [ ] **Step 5: Implement Up dispatch using the new method**

```cpp
int LiveNavigationController::tryHandle(int key, int modifiers,
                                        int blockIndex, int qtPos,
                                        QObject *editItem,
                                        const QString &blockText) {
    Q_UNUSED(blockText);
    Q_UNUSED(qtPos);
    if (modifiers != Qt::NoModifier) return NotHandled;  // Shift/Ctrl handled later

    if (key == Qt::Key_Up) {
        if (!isAtVisualTopLine(editItem)) return NotHandled;
        // Reuse existing desiredVisualX if set (consecutive-Up preserves
        // column); otherwise sample from the current cursor.
        qreal desiredX = m_cursorState->desiredVisualX();
        if (desiredX < 0) {
            const QVariant rectV = editItem->property("cursorRectangle");
            desiredX = rectV.canConvert<QRectF>() ? rectV.toRectF().x() : 0.0;
            m_cursorState->setDesiredVisualX(desiredX);
        }
        const int targetRow = previousNavigableRow(blockIndex);
        if (targetRow < 0) return Handled;
        m_cursorState->requestTextCaretAtRowVisualX(
            targetRow, LiveCursorState::VisualLineHint::LastLine);
        return Handled;
    }

    return NotHandled;
}
```

This requires extending each delegate's `focusEditAt` to honor the visual-line hint when present. That's part of the QML wiring (E6), but we set it up here.

- [ ] **Step 6: Extend `focusEditAt` in `ParagraphDelegate.qml`**

```qml
function focusEditAt(qtPos) {
    edit.forceActiveFocus()
    const cs = root.liveBinding ? root.liveBinding.cursorState : null
    if (cs) {
        const hint = cs.pendingVisualLineHint  // 0=None, 1=FirstLine, 2=LastLine
        const desiredX = cs.desiredVisualX
        if (hint !== 0 && desiredX >= 0) {
            const targetY = (hint === 1)
                ? edit.lineHeight * 0.5
                : edit.contentHeight - edit.lineHeight * 0.5
            edit.cursorPosition = edit.positionAt(
                desiredX - edit.leftPadding, targetY)
            return
        }
    }
    if (qtPos >= 0 && qtPos <= edit.length)
        edit.cursorPosition = qtPos
}
```

(Apply this same `focusEditAt` extension to all 5 text-bearing delegates as part of E6.)

- [ ] **Step 7: Run to verify pass**

Expected: PASS for the Up-cross test.

- [ ] **Step 8: Commit**

```bash
git add libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp \
        libs/markoff-live/qml/delegates/ParagraphDelegate.qml \
        libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp
git commit -m "markoff-live: Up arrow crosses to prev block w/ column preserve (E2 E1)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task E2: Down at visual-bottom-line crosses to next block

Mirror Task E1 with `Key_Down`, `nextNavigableRow`, `VisualLineHint::FirstLine`. The `focusEditAt` extension from E1 already handles the FirstLine case.

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp`

- [ ] **Step 1: Write failing test**

```cpp
void down_at_visual_bottom_line_crosses_to_next_block_at_matching_column() {
    // Construct a 2-block doc:
    //   Block 0: "Lorem ipsum"
    //   Block 1: "Dolor sit amet consectetur"
    // Place caret at qtPos 4 of block 0 ("Lore|"). visualX ≈ width of "Lore".
    // Simulate Down: tryHandle(Key_Down, 0, blockIndex=0, qtPos=4, editItem=block0Edit, blockText)
    // Expected:
    //   - return Handled
    //   - cursorState.desiredVisualX is set to ≈ width-of-"Lore"
    //   - cursorState.pendingVisualLineHint == FirstLine
    //   - The destination's focusEditAt resolves qtPos to ~4 ("Dolo|" on first line)
}
```

- [ ] **Step 2: Run to verify failure**

```
ctest --test-dir build-dev -R tst_live_render_e2_nav_arrows --output-on-failure
```

Expected: FAIL — Down case not yet handled.

- [ ] **Step 3: Implement Down case in `tryHandle`**

Append to the existing `tryHandle` body, mirroring the Up case:

```cpp
if (key == Qt::Key_Down) {
    if (!isAtVisualBottomLine(editItem)) return NotHandled;
    qreal desiredX = m_cursorState->desiredVisualX();
    if (desiredX < 0) {
        const QVariant rectV = editItem->property("cursorRectangle");
        desiredX = rectV.canConvert<QRectF>() ? rectV.toRectF().x() : 0.0;
        m_cursorState->setDesiredVisualX(desiredX);
    }
    const int targetRow = nextNavigableRow(blockIndex);
    if (targetRow < 0) return Handled;
    m_cursorState->requestTextCaretAtRowVisualX(
        targetRow, LiveCursorState::VisualLineHint::FirstLine);
    return Handled;
}
```

- [ ] **Step 4: Run to verify pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp
git commit -m "markoff-live: Down arrow crosses to next block w/ column preserve (E2 E2)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task E3: Left at qtPos 0 crosses to prev block end

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp`

- [ ] **Step 1: Write failing test**

```cpp
void left_at_qtpos_0_crosses_to_prev_block_end_and_clears_visual_x() {
    // Two-block doc; place caret at qtPos 0 of block 1 with desiredVisualX = 99.
    // Simulate Left: tryHandle(Key_Left, 0, blockIndex=1, qtPos=0, editItem, blockText)
    // Expected:
    //   - return Handled
    //   - cursorState.desiredVisualX == -1 (cleared)
    //   - cursorState.requestTextCaretAtRow(0, block0.text.length()) called
}
```

- [ ] **Step 2: Run to verify failure**

- [ ] **Step 3: Implement Left case in `tryHandle`**

```cpp
if (key == Qt::Key_Left) {
    if (qtPos > 0) return NotHandled;
    m_cursorState->clearDesiredVisualX();
    const int targetRow = previousNavigableRow(blockIndex);
    if (targetRow < 0) return Handled;
    const int targetLen = m_model->recordAt(targetRow).text.length();
    m_cursorState->requestTextCaretAtRow(targetRow, targetLen);
    return Handled;
}
```

- [ ] **Step 4: Run to verify pass**

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp
git commit -m "markoff-live: Left at qtPos 0 crosses to prev block end (E2 E3)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task E4: Right at qtPos length crosses to next block start

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp`

- [ ] **Step 1: Write failing test**

```cpp
void right_at_qtpos_length_crosses_to_next_block_start_and_clears_visual_x() {
    // Two-block doc; place caret at qtPos == block0.text.length() with
    // desiredVisualX = 99.
    // Simulate Right: tryHandle(Key_Right, 0, blockIndex=0, qtPos=blockLen,
    //                            editItem, blockText)
    // Expected:
    //   - return Handled
    //   - cursorState.desiredVisualX == -1
    //   - cursorState.requestTextCaretAtRow(1, 0) called
}
```

- [ ] **Step 2: Run to verify failure**

- [ ] **Step 3: Implement Right case**

```cpp
if (key == Qt::Key_Right) {
    if (qtPos < blockText.length()) return NotHandled;
    m_cursorState->clearDesiredVisualX();
    const int targetRow = nextNavigableRow(blockIndex);
    if (targetRow < 0) return Handled;
    m_cursorState->requestTextCaretAtRow(targetRow, 0);
    return Handled;
}
```

- [ ] **Step 4: Run to verify pass**

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/tests/tst_live_render_e2_nav_arrows.cpp
git commit -m "markoff-live: Right at qtPos length crosses to next block start (E2 E4)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task E5: Column preservation across consecutive Up/Down

A regression-style test that issues 3 consecutive Up presses through blocks of varying width and verifies the final caret column matches the starting column within ±1 character.

- [ ] **Files:** `libs/markoff-live/tests/tst_live_render_e2_nav_column_preservation.cpp`

- [ ] **Step 1: Write the test**

(Use the binding fixture; programmatically dispatch tryHandle 3 times; verify desiredVisualX is preserved across calls and the final caret position aligns.)

- [ ] **Step 2: Run** (fail if `desiredVisualX` is cleared mid-stream)

- [ ] **Step 3: Verify the controller does NOT clear desiredVisualX between consecutive Up calls**

In `tryHandle`, only `Key_Left`/`Key_Right`/`Key_Home`/`Key_End` clear `desiredVisualX`. `Key_Up`/`Key_Down` reuse the existing value if already set:

```cpp
if (key == Qt::Key_Up) {
    if (!isAtVisualTopLine(editItem)) return NotHandled;
    qreal desiredX = m_cursorState->desiredVisualX();
    if (desiredX < 0) {
        const QVariant rectV = editItem->property("cursorRectangle");
        desiredX = rectV.canConvert<QRectF>() ? rectV.toRectF().x() : 0.0;
        m_cursorState->setDesiredVisualX(desiredX);
    }
    // ...
}
```

- [ ] **Step 4: Run** (pass)
- [ ] **Step 5: Commit**

---

### Task E6: Wire arrow keys in delegate `Keys.onPressed`

Now that the controller dispatches correctly, route the keys from each text-bearing delegate.

**Files:**
- Modify: `libs/markoff-live/qml/delegates/ParagraphDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/ListItemDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/BlockquoteDelegate.qml`
- Modify: `libs/markoff-live/qml/delegates/CodeBlockDelegate.qml`

- [ ] **Step 1: Edit `ParagraphDelegate.qml`'s `Keys.onPressed`**

Currently (lines 53-73):

```qml
Keys.priority: Keys.BeforeItem
Keys.onPressed: (event) => {
    const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
    if (!handler) { event.accepted = false; return }

    const k = event.key
    if (k !== Qt.Key_Return && k !== Qt.Key_Enter
        && k !== Qt.Key_Escape && k !== Qt.Key_Backspace && k !== Qt.Key_Delete) {
        return
    }

    const handled = handler.tryHandle(...)
    event.accepted = handled
}
```

Extend:

```qml
Keys.priority: Keys.BeforeItem
Keys.onPressed: (event) => {
    if (!root.liveBinding) { event.accepted = false; return }

    const k = event.key
    const mods = event.modifiers
    const isStructural = (k === Qt.Key_Return || k === Qt.Key_Enter
                       || k === Qt.Key_Escape || k === Qt.Key_Backspace
                       || k === Qt.Key_Delete)
    const isNav = (k === Qt.Key_Up || k === Qt.Key_Down
                || k === Qt.Key_Left || k === Qt.Key_Right
                || k === Qt.Key_Home || k === Qt.Key_End
                || k === Qt.Key_PageUp || k === Qt.Key_PageDown)

    if (isStructural) {
        const sh = root.liveBinding.structuralKeyHandler
        if (!sh) return
        event.accepted = sh.tryHandle(k, mods, root.modelIndex,
                                       edit.cursorPosition,
                                       edit.selectionStart === edit.selectionEnd,
                                       model.text)
        return
    }
    if (isNav) {
        const nh = root.liveBinding.navigationController
        if (!nh) return
        event.accepted = (nh.tryHandle(k, mods, root.modelIndex,
                                        edit.cursorPosition,
                                        edit, model.text) === 1)
        return
    }
    // not for us; fall through to TextEdit's default
}
```

- [ ] **Step 2: Smoke-test in markoff-live-app**

```
cmake --build build-dev --target markoff-live-app -j 8
./build-dev/bin/markoff-live-app /path/to/test.md
```

Type into a paragraph; press Up at the top line. Caret should jump to the previous block. Press Down at the bottom line. Caret should jump forward.

- [ ] **Step 3: Apply same edit to the other 4 delegates** (Heading, ListItem, Blockquote, CodeBlock).

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/qml/delegates/{Paragraph,Heading,ListItem,Blockquote,CodeBlock}Delegate.qml
git commit -m "markoff-live: text-bearing delegates forward nav keys (E2 E6)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task E7: Phase E green-tree gate

```
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

---

## Phase F — Extended nav (Home/End/Ctrl+arrows/Page-Up/Down)

---

### Task F1: Home / End (Qt default; verify pass-through)

Home and End inside a block should use Qt's default behavior (start/end of visual line). The controller returns NotHandled for them when no Ctrl modifier is present.

- [ ] **Step 1: Write a regression test confirming Home/End at qtPos 0/length stays in block**

(In `tst_live_render_e2_nav_home_end.cpp`.)

- [ ] **Step 2: Verify no controller change needed** (should already be NotHandled).

- [ ] **Step 3: Commit**

---

### Task F2: Ctrl+Home / Ctrl+End

Ctrl+Home: caret at qtPos 0 of first text-bearing row. Ctrl+End: end of last text-bearing row.

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/include/markoff/live/LiveNavigationController.h` (add private helpers)
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_home_end.cpp`

- [ ] **Step 1: Write failing test**

```cpp
void ctrl_home_lands_at_first_text_block_qtpos_0() {
    // Build doc with: HR, paragraph "first", HR, paragraph "second"
    // Place caret in "second"
    // Simulate: tryHandle(Key_Home, ControlModifier, ...)
    // Expect: requestTextCaretAtRow(1 /*first paragraph*/, 0) called.
}

void ctrl_end_lands_at_last_text_block_end() {
    // Same doc; caret in "first"
    // Simulate: tryHandle(Key_End, ControlModifier, ...)
    // Expect: requestTextCaretAtRow(3 /*second paragraph*/, len("second")) called.
}

void ctrl_home_skips_leading_non_text_blocks() {
    // Doc starting with HR, then paragraph
    // Verify ctrl_home lands at row 1 not row 0.
}
```

- [ ] **Step 2: Run to verify failure**

- [ ] **Step 3: Implement helpers and Ctrl+Home/End**

In header (private):

```cpp
int findFirstTextBearingRow() const;
int findLastTextBearingRow() const;
bool isTextBearing(int row) const;
```

In source:

```cpp
bool LiveNavigationController::isTextBearing(int row) const {
    if (!m_model || !m_registry) return false;
    if (row < 0 || row >= m_model->rowCount()) return false;
    const auto rec = m_model->recordAt(row);
    const auto desc = m_registry->descriptorFor(rec.kind);
    return desc.supportedCursorVariants.contains(QStringLiteral("TextCaret"));
}
int LiveNavigationController::findFirstTextBearingRow() const {
    if (!m_model) return -1;
    for (int r = 0; r < m_model->rowCount(); ++r)
        if (isTextBearing(r)) return r;
    return -1;
}
int LiveNavigationController::findLastTextBearingRow() const {
    if (!m_model) return -1;
    for (int r = m_model->rowCount() - 1; r >= 0; --r)
        if (isTextBearing(r)) return r;
    return -1;
}
```

In `tryHandle`, add the Ctrl-modifier branch:

```cpp
if (modifiers == Qt::ControlModifier) {
    if (key == Qt::Key_Home) {
        const int firstRow = findFirstTextBearingRow();
        if (firstRow >= 0) {
            m_cursorState->clearDesiredVisualX();
            m_cursorState->requestTextCaretAtRow(firstRow, 0);
        }
        return Handled;
    }
    if (key == Qt::Key_End) {
        const int lastRow = findLastTextBearingRow();
        if (lastRow >= 0) {
            const int len = m_model->recordAt(lastRow).text.length();
            m_cursorState->clearDesiredVisualX();
            m_cursorState->requestTextCaretAtRow(lastRow, len);
        }
        return Handled;
    }
}
```

- [ ] **Step 4: Run to verify pass**

```
ctest --test-dir build-dev -R tst_live_render_e2_nav_home_end --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveNavigationController.h \
        libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/tests/tst_live_render_e2_nav_home_end.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: Ctrl+Home / Ctrl+End nav (E2 F2)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task F3: Ctrl+Left / Ctrl+Right (word boundaries)

The delegate exposes `Q_INVOKABLE int computePrevWordPos(int qtPos)` and `Q_INVOKABLE int computeNextWordPos(int qtPos)` that wrap `QTextCursor::movePosition(QTextCursor::PreviousWord/NextWord)` against `edit.textDocument`.

**Files:**
- Modify: each text-bearing delegate (add the Q_INVOKABLE fns at the delegate root)
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`

- [ ] **Step 1: Failing test** in `tst_live_render_e2_nav_word.cpp`

- [ ] **Step 2: Add Q_INVOKABLE fns to each text-bearing delegate**

```qml
function computePrevWordPos(qtPos) {
    // QTextCursor over edit.textDocument
    // Returns the qtPos of the previous word boundary, or 0 if none.
    if (!edit || !edit.textDocument) return 0
    return edit.textDocument.computePrevWordPos
           ? edit.textDocument.computePrevWordPos(qtPos)
           : Math.max(0, qtPos - 1)  // fallback: char-step
}
```

(QQuickTextDocument doesn't expose a Q_INVOKABLE method for word boundaries directly — we may need a small C++ helper that takes a `QQuickTextDocument *` and a qtPos and returns the boundary using `QTextCursor`. Add it to a new utility file or to `Coordinates.cpp` since that's the existing coord-translation helper.)

Realistically: add `Q_INVOKABLE int Markoff::Live::Coordinates::prevWordPos(QQuickTextDocument *doc, int qtPos)` and call it from the delegate.

- [ ] **Step 3: Implement Ctrl+Left/Right in tryHandle**

```cpp
if (modifiers == Qt::ControlModifier && key == Qt::Key_Left) {
    if (qtPos > 0) {
        const int target = invokeOnDelegate(editItem, "computePrevWordPos", qtPos);
        m_cursorState->requestTextCaretAtRow(blockIndex, target);
        return Handled;
    }
    // qtPos == 0: cross to prev block's last word.
    const int targetRow = previousNavigableRow(blockIndex);
    if (targetRow < 0) return Handled;
    if (!isTextBearing(targetRow)) {
        // fall back to plain Left semantics (BlockSelected on prev row)
        m_cursorState->requestBlockSelected(targetRow);
        return Handled;
    }
    // Need access to the target delegate's TextEdit. Do this asynchronously:
    // first focus the target row at end-of-text, then once the delegate is
    // active, ask it to compute prevWordPos from end. The simplest reliable
    // path: request caret at end first, then queue a follow-up via the new
    // signal `cursorState::cursorChanged`. To avoid race conditions we add a
    // pending "post-place qtPos" mechanism on cursorState. (See E5's
    // visualLineHint pattern for precedent.)
    const int targetLen = m_model->recordAt(targetRow).text.length();
    m_cursorState->requestTextCaretAtRow(targetRow, targetLen);
    m_cursorState->setPendingPostPlaceAction(
        LiveCursorState::PostPlaceAction::PrevWordFromHere);
    return Handled;
}
```

(`PendingPostPlaceAction` is a small enum we add to `LiveCursorState`. The delegate, on becoming active focus, consults the action and adjusts qtPos accordingly via its computePrevWordPos. Implementation parallels the existing `requestTextCaretAtAnchor` pending mechanism.)

- [ ] **Step 4: Run to verify pass**

```
ctest --test-dir build-dev -R tst_live_render_e2_nav_word --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/Coordinates.cpp \
        libs/markoff-live/include/markoff/live/Coordinates.h \
        libs/markoff-live/include/markoff/live/LiveCursorState.h \
        libs/markoff-live/src/LiveCursorState.cpp \
        libs/markoff-live/include/markoff/live/LiveNavigationController.h \
        libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/qml/delegates/{Paragraph,Heading,ListItem,Blockquote,CodeBlock}Delegate.qml \
        libs/markoff-live/tests/tst_live_render_e2_nav_word.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: Ctrl+Left / Ctrl+Right word boundary nav across blocks (E2 F3)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task F4: Page-Up / Page-Down via `LiveView.hit()`

Page jumps need access to the ListView (`LiveView`) to hit-test at the offset position. The controller doesn't currently know about the ListView. Options:

- α: add a `setListViewItem(QQuickItem *)` method on the controller; have `LiveView.qml` push itself in via `Component.onCompleted`.
- β: per-delegate Keys.onPressed for Page keys forwards to a different callable on the controller that takes the ListView item as an argument.

α is cleaner.

**Files:**
- Modify: `libs/markoff-live/include/markoff/live/LiveNavigationController.h` (`setListView(QQuickItem *)`)
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/qml/LiveView.qml` (push self into controller on init)

- [ ] **Step 1: Failing test** in `tst_live_render_e2_nav_page.cpp`

(Uses a QQuickView with a real LiveView so `hit()` works.)

- [ ] **Step 2: Add controller field + setter**

- [ ] **Step 3: Implement Page-Up/Page-Down**

```cpp
if (modifiers == Qt::NoModifier && (key == Qt::Key_PageUp || key == Qt::Key_PageDown)) {
    if (!m_listView) return NotHandled;
    const QVariant rectV = editItem->property("cursorRectangle");
    if (!rectV.canConvert<QRectF>()) return NotHandled;
    const QRectF cursorRect = rectV.toRectF();
    // Map cursorRect to LiveView coords
    QObject *delegate = editItem; // walk up to delegate root if needed
    const QPointF cursorPosInView =
        invokeMapToItem(delegate, m_listView, cursorRect.topLeft());
    const qreal viewH = m_listView->property("height").toReal();
    const qreal targetY = cursorPosInView.y()
                        + (key == Qt::Key_PageDown ? viewH : -viewH);
    const QPointF probe(cursorPosInView.x(), targetY);

    QVariant hitResult;
    QMetaObject::invokeMethod(m_listView, "hit",
        Qt::DirectConnection, Q_RETURN_ARG(QVariant, hitResult),
        Q_ARG(QVariant, probe.x()), Q_ARG(QVariant, probe.y()));
    if (!hitResult.canConvert<QVariantMap>()) return Handled;
    const QVariantMap hit = hitResult.toMap();
    const int hitRow = hit.value("blockIndex", -1).toInt();
    const int hitQtPos = hit.value("qtPos", -1).toInt();
    if (hitRow < 0) return Handled;
    if (isTextBearing(hitRow))
        m_cursorState->requestTextCaretAtRow(hitRow, std::max(0, hitQtPos));
    else
        m_cursorState->requestBlockSelected(hitRow);
    return Handled;
}
```

- [ ] **Step 4: Wire LiveView**

In `LiveView.qml`:

```qml
Component.onCompleted: {
    if (binding && binding.navigationController) {
        binding.navigationController.setListView(root)
    }
}
```

- [ ] **Step 5: Run to verify pass**

```
ctest --test-dir build-dev -R tst_live_render_e2_nav_page --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live/include/markoff/live/LiveNavigationController.h \
        libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/qml/LiveView.qml \
        libs/markoff-live/tests/tst_live_render_e2_nav_page.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: Page-Up/Page-Down via LiveView.hit() (E2 F4)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task F5: Phase F green-tree gate

```
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

---

## Phase G — Selection extension (Shift+arrow)

---

### Task G1: Shift+Arrow → `LiveSelectionView::extend`

When the modifier is `ShiftModifier`, the controller computes the same target as the corresponding non-Shift key, but instead of `requestTextCaretAtRow`, it calls `m_selectionView->extend(targetRow, targetQtPos)` AND moves the caret to the new active end.

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Test: `libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp`

- [ ] **Step 1: Failing test**

```cpp
void shift_down_at_block_bottom_extends_selection() {
    // Construct binding with 3 paragraph blocks.
    // Place caret in middle of block 0.
    // Call tryHandle(Down, ShiftModifier, ...).
    // Verify: selectionView.hasSelection == true; anchor row == 0;
    //         active row == 1.
}
void shift_down_twice_extends_through_three_blocks() {
    // Press Shift+Down twice.
    // Verify selection covers (block 0 active row → block 1 → block 2 head).
}
```

- [ ] **Step 2: Implement Shift cases**

Refactor the dispatch so each "compute target" path is shared between non-Shift and Shift variants. Then:

```cpp
const bool extending = (modifiers & Qt::ShiftModifier);
// ... compute (targetRow, targetQtPos) same as Flow B ...
if (extending) {
    m_selectionView->extend(targetRow, targetQtPos);
    m_cursorState->requestTextCaretAtRow(targetRow, targetQtPos);
} else {
    m_cursorState->requestTextCaretAtRow(targetRow, targetQtPos);
}
```

- [ ] **Step 3: Run to verify pass**

```
ctest --test-dir build-dev -R tst_live_render_e2_nav_shift_extend --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp \
        libs/markoff-live/tests/CMakeLists.txt
git commit -m "markoff-live: Shift+Arrow extends LiveSelectionView across blocks (E2 G1)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task G2: Ctrl+Shift+Left/Right (word-extend across blocks)

**Files:**
- Modify: `libs/markoff-live/src/LiveNavigationController.cpp`
- Modify: `libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp`

- [ ] **Step 1: Write failing test**

```cpp
void ctrl_shift_left_at_qtpos_0_word_extends_into_prev_block() {
    // Doc: "first paragraph", "second paragraph"
    // Place caret at qtPos 0 of block 1.
    // Simulate: tryHandle(Key_Left, ControlModifier|ShiftModifier,
    //                     blockIndex=1, qtPos=0, ...)
    // Expected:
    //   - return Handled
    //   - selectionView.extend(0, prevWordPosFromEnd) called
    //   - cursorState.requestTextCaretAtRow(0, prevWordPosFromEnd) called
}
```

- [ ] **Step 2: Run to verify failure**

- [ ] **Step 3: Implement Ctrl+Shift cases**

Refactor: pull the word-boundary computation in F3 into a helper that returns `(targetRow, targetQtPos)` regardless of whether the caller will use it for caret-place or selection-extend. Then in tryHandle:

```cpp
if ((modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier)) {
    if (key == Qt::Key_Left) {
        const auto target = computeCtrlLeftTarget(blockIndex, qtPos, editItem);
        if (!target) return Handled;
        m_selectionView->extend(target->row, target->qtPos);
        m_cursorState->requestTextCaretAtRow(target->row, target->qtPos);
        return Handled;
    }
    if (key == Qt::Key_Right) {
        const auto target = computeCtrlRightTarget(blockIndex, qtPos, editItem, blockText);
        if (!target) return Handled;
        m_selectionView->extend(target->row, target->qtPos);
        m_cursorState->requestTextCaretAtRow(target->row, target->qtPos);
        return Handled;
    }
}
```

(The non-Shift Ctrl+Left/Right path from F3 also calls these `compute*Target` helpers, so the refactor unifies the logic.)

- [ ] **Step 4: Run to verify pass**

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live/src/LiveNavigationController.cpp \
        libs/markoff-live/include/markoff/live/LiveNavigationController.h \
        libs/markoff-live/tests/tst_live_render_e2_nav_shift_extend.cpp
git commit -m "markoff-live: Ctrl+Shift+Left/Right word-extend across blocks (E2 G2)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task G3: Phase G green-tree gate

```
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure -j 8
```

Expected: all green.

---

## Phase H — Performance benchmarks

---

### Task H1: Caret-move benchmark

**Files:**
- Create: `libs/markoff-live/tests/tst_live_render_e2_perf_caret_move.cpp`

- [ ] **Step 1: Write the benchmark**

Construct a 100-block doc, 10 spans/block. Issue 100 caret moves. Measure p99 latency. Assert p99 < 5ms.

(See `tst_live_render_inline_typing_perf.cpp` for the QBENCHMARK_ONCE pattern.)

- [ ] **Step 2: Run**

```
cmake --build build-dev --target tst_live_render_e2_perf_caret_move -j 8
./build-dev/bin/tst_live_render_e2_perf_caret_move
```

- [ ] **Step 3: Commit**

---

### Task H2: Typing-perf no-regression

- [ ] **Step 1: Re-run E1's typing-perf benchmark; ensure it still passes the < 33ms p99 CI gate.**

- [ ] **Step 2: Add an E2-flavored benchmark that exercises caret motion across a span-rich block during typing.**

- [ ] **Step 3: Commit**

---

## Phase I — Dogfood + tag

---

### Task I1: Manual dogfood pass

- [ ] **Step 1: Build and run markoff-live-app on a representative document**

```
cmake --build build-dev --target markoff-live-app -j 8
./build-dev/bin/markoff-live-app /path/to/test-doc-with-everything.md
```

The test doc should contain:
- All 8 inline kinds
- Heading 1-6
- List items (bulleted + numbered)
- Blockquote (incl. nested)
- Code block (with language tag)
- Horizontal rule
- Image
- Links and wikilinks (some with alias)
- Tags
- Selection scenarios

- [ ] **Step 2: Walk the spec §5 acceptance bullets one by one, manually verifying each in the app.**

For each, note pass/fail. Any fail blocks the tag.

- [ ] **Step 3: If any fail, fix, re-run, repeat until all pass.**

---

### Task I2: Update phase board + recent-changes log

- [ ] **Step 1: Edit `docs/e-arc/e-arc-status.md`**

- Phase board E2 row: status → `complete` (2026-XX-XX, tag `v0.7.0-e2`).
- Append recent-changes entry: "**E2 complete:** auto-hide + cross-block nav both ship. N/N tests pass. Dogfood signed off. Tag `v0.7.0-e2`. Phase board E2 → `complete`. E3 (Obsidian affordances) is next."
- Update Last-updated date.

- [ ] **Step 2: Edit `CLAUDE.md` worktree banner**

Replace the spec-approved banner with a `complete` line.

- [ ] **Step 3: Commit**

```bash
git add docs/e-arc/e-arc-status.md CLAUDE.md
git commit -m "docs: E2 complete (auto-hide + cross-block nav)

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task I3: Tag `v0.7.0-e2`

- [ ] **Step 1: Confirm full ctest suite green**

```
ctest --test-dir build-dev -E 'tst_realistic|tst_benchmark' --output-on-failure -j 8
```

Expected: all green.

- [ ] **Step 2: Tag**

```bash
git tag -a v0.7.0-e2 -m "E2: cursor-aware view (auto-hide + cross-block keyboard nav)"
```

(Do NOT push the tag without explicit user authorization.)

- [ ] **Step 3: Append final recent-changes log entry pointing at the tag.**

---

## Closeout checklist

- [ ] All 33 tasks above completed
- [ ] Phase A green-tree, Phase B green-tree, Phase C green-tree, Phase D green-tree, Phase E green-tree, Phase F green-tree, Phase G green-tree, Phase H benchmarks pass
- [ ] Phase I dogfood signed off (every spec §5 acceptance bullet manually verified)
- [ ] Phase board E2 → `complete`
- [ ] Tag `v0.7.0-e2` created (not pushed)
- [ ] CLAUDE.md banner updated
- [ ] e-arc-status.md recent-changes log updated

The phase is done when this checklist's last box is ticked.
