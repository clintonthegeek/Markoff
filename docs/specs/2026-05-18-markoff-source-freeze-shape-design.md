# markoff-source — freeze-shape design

**Date:** 2026-05-18
**Branch:** `exploration/new-foundation`
**Status:** design approved 2026-05-18; implementation plan pending.
**Companions:** `docs/2026-05-18-public-api-surface-audit.md` §markoff-source, `docs/handoff/2026-05-07-pivot-to-d5-first.md` §4.6.

## Purpose

`markoff-source` is the smallest of the three shipping leaves (two public headers). The audit (`docs/2026-05-18-public-api-surface-audit.md`) surfaced five shape questions for the §4.6 API freeze; this spec captures the decisions on each and the migration plan.

The decisions also establish the **pattern** for the larger leaves' freeze passes — symmetric two-level namespaces, `Detail::` for internal types, MarkdownView contract methods implemented rather than stubbed.

## Scope

In scope:
- Public surface of `libs/markoff-source/include/markoff/source/` (`Editor.h`, `FindBar.h`).
- The internal `Gutter` class (used in `Editor.cpp`, declared in `src/`).
- `MarkdownView::showFindBar` / `hideFindBar` / `showReplaceBar` contract — currently virtual stubs on the base class — gets a real implementation on the source-side Editor.

Out of scope (tracked separately):
- Symmetric `showFindBar`/`hideFindBar` implementation on `markoff-live`. The cross-leaf consistency follow-on is referenced in the Open questions section and will need its own spec.
- A real `ReplaceBar` widget. `showReplaceBar()` remains a no-op stub; pinned as a future feature.
- Find-bar positioning configurability (top vs bottom). Locked to bottom for v1.
- `MarkdownView` base class signatures. The virtuals (`showFindBar`, `hideFindBar`, `showReplaceBar`) keep their current default no-op implementations on the base; this spec only changes that `Editor` *overrides* them with real implementations. Leaves that haven't migrated (today: `markoff-live`) continue to inherit the no-op default until their own freeze pass.

## Decisions

### D1. `plainTextEdit()` is the canonical escape hatch; forwarders are removed

**Decision:** `Editor::plainTextEdit() const -> QPlainTextEdit *` stays public as the documented escape hatch for consumers needing raw Qt-level access. The six forwarder methods (`toPlainText`, `extraSelections`, `setExtraSelections`, `textCursor`, `setTextCursor`, `ensureCursorVisible`) are removed.

**Why:** The forwarders duplicate the accessor's surface; consumers had two ways to do the same thing, both pre-freeze. `Editor` is intentionally a thin shell around `QPlainTextEdit + KSyntaxHighlighting + SourceTextDocumentBinding + Gutter` (per CLAUDE.md "Fully-owned QtWidgets Source view on `markoff-core`"). Hiding the accessor would force re-implementing QPlainTextEdit method-by-method as forwarders grow; that's the wrong direction for a "thin shell" widget.

**How to apply:**
- Header docstring on `plainTextEdit()`: "Escape hatch for raw `QPlainTextEdit` access. Consumers may use any method on the returned pointer **except** `setPlainText`, `setDocument`, or any state-corrupting mutator that bypasses `SourceTextDocumentBinding`. The polymorphic `MarkdownView` contract (`setDocument`, `cursorPosition`, etc.) is the safe curated surface."
- All in-tree callers (Gutter, FindBar, tests) reach through `plainTextEdit()->X()`.

### D2. `FindBar` is host-instantiable AND Editor self-hosts via the MarkdownView contract

**Decision:** Two paths coexist:
1. **Editor-managed (canonical).** `Editor::showFindBar()` lazily instantiates a `FindBar` as a child widget, positions it at the bottom of the Editor's client area, manages show/hide lifecycle. `Editor::hideFindBar()` deactivates and hides. Consumer calls these via the polymorphic `MarkdownView` pointer or directly on the Editor.
2. **Host-managed (advanced).** `FindBar(Editor *, QWidget *parent = nullptr)` constructor stays public. A host that wants the find UI in a sidebar, a different layout position, or with custom keyboard shortcuts can instantiate directly and ignore `showFindBar`.

**Why:** Today's reality is that `showFindBar` was a `/* find bar integration: v1.1 */` stub — the contract is aspirational. Implementing it for `Editor` lights up polymorphic find UX through the MarkdownView pointer for source-typed views; the symmetric `markoff-live` impl (out of this spec's scope) completes the cross-view story. Leaving the FindBar host-instantiable preserves the advanced use case for hosts who want custom placement.

**How to apply:**
- `Editor` gains a `m_findBar` member (`FindBar *`, lazy-initialized).
- `Editor::showFindBar()`:
  - If `m_findBar` is null: instantiate `new FindBar(this, this)`, position at bottom via `resizeEvent` logic, connect `FindBar::closed` → `Editor::hideFindBar`.
  - Call `m_findBar->activate()` (existing public slot: show + focus + re-search current needle).
- `Editor::hideFindBar()`:
  - If `m_findBar` is null: no-op.
  - Else: call `m_findBar->deactivate()` (clears highlights + hides via existing logic).
- `Editor::showReplaceBar()` remains a stub; documented as "future API; calls showFindBar for now or no-ops" — pick no-op to avoid surprising the host with a find bar when they asked for replace. Will be designed in a future ReplaceBar spec.
- `Editor::resizeEvent` lays out the bar at the bottom edge when visible. Reserve `m_findBar->sizeHint().height()` of vertical space from the inner `QPlainTextEdit` when shown; reclaim when hidden.
- **Ctrl+F shortcut stays host-orchestrated.** Editor does NOT install its own Ctrl+F. Hosts wire QAction → `editor->showFindBar()` from menus or keyboard shortcuts. Rationale: host owns action arbitration; multiple Ctrl+F contexts in an app shouldn't fight Editor's internal handler.

### D3. Namespace flattens to `Markoff::Source::*`

**Decision:** Rename `Markoff::Source::Widget::Editor` → `Markoff::Source::Editor`. Rename `Markoff::Source::Widget::FindBar` → `Markoff::Source::FindBar`. The `Widget` namespace level is removed.

**Why:** The `Widget` level was reserved for a hypothetical `Markoff::Source::Headless` partition that never materialized. Two-level namespace is symmetric with `Markoff::Live::*` and is the project-wide pattern. Three-level nesting adds no information.

**How to apply:**
- Mechanical sweep: `Markoff::Source::Widget::` → `Markoff::Source::` across the worktree.
- Files touched: `libs/markoff-source/include/markoff/source/Editor.h`, `FindBar.h`; `libs/markoff-source/src/Editor.cpp`, `FindBar.cpp`, `Gutter.h`, `Gutter.cpp`; all tests in `libs/markoff-source/tests/`; the v10 contract test in `libs/markoff-source/tests/tst_v10_source_editor_view_contract.cpp`.
- **Test file names stay as-is** (`tst_source_widget_editor.cpp`, `tst_source_widget_findbar.cpp`, `tst_source_widget_binding_roundtrip.cpp`). The `source_widget` prefix is a project test-prefix convention (`libs/markoff-source/CLAUDE.md`: "Tests prefix `tst_source_widget_*`."). Renaming the convention is out of scope here; only the C++ namespace inside the files changes.
- CMake target names (`markoff_source`, `tst_source_widget_*`) are namespace-free; no CMakeLists.txt update needed.
- Verify with `grep -rn 'Markoff::Source::Widget' libs/ apps/` post-rename — should return zero hits.

### D4. `Gutter` moves to `Markoff::Source::Detail::Gutter`

**Decision:** The internal child widget `Gutter` moves to the `Markoff::Source::Detail::` namespace. The forward declaration in `Editor.h` becomes `namespace Detail { class Gutter; }`; the friend declaration becomes `friend class Markoff::Source::Detail::Gutter`.

**Why:** Matches the project-wide `Markoff::Detail::` convention (`markoff-core` CLAUDE.md: "Foundation-internal helpers go in `Markoff::Detail` namespace"). The `Detail` segment is a clear consumer signal ("this is not for you") without resorting to PIMPL heap-indirection boilerplate.

**How to apply:**
- Edit `libs/markoff-source/src/Gutter.h`: wrap class in `namespace Markoff::Source::Detail { ... }`.
- Edit `libs/markoff-source/src/Gutter.cpp`: namespace open block.
- Edit `Editor.h`: forward-decl + friend updated per above.
- Edit `Editor.cpp`: `Gutter` references become `Detail::Gutter` (or qualified `Markoff::Source::Detail::Gutter` depending on resolution context).

### D5. Forwarders deletion + showFindBar implementation order

**Decision:** Implement in three commits, each green:
1. Commit A — D3 (namespace flatten). Mechanical rename. No behavior change.
2. Commit B — D4 (Gutter to Detail). Mechanical. No behavior change.
3. Commit C — D1 (forwarders removed) + D2 (showFindBar/hideFindBar implemented). The behavior commit.

Each commit's gate: full fast test suite green (217+ tests, `tst_realistic` + `tst_benchmark` not required between commits since neither D1/D2/D3/D4 touch the live-render hot path).

## New public surface (post-freeze)

### `Markoff::Source::Editor`

```cpp
class Editor : public Markoff::MarkdownView {
    Q_OBJECT
    Q_PROPERTY(Markoff::Theme theme READ theme
               WRITE setTheme NOTIFY themeChanged)
public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    // MarkdownView contract (all implemented)
    void setDocument(Markoff::MarkoffDocument *doc) override;
    Markoff::CursorPos cursorPosition() const override;
    void setCursorPosition(Markoff::CursorPos pos) override;
    float scrollPositionVisualLine() const override;
    void  setScrollPositionVisualLine(float pos) override;
    void setReadOnly(bool ro) override;
    bool isReadOnly() const override;
    bool hasCursor()  const override { return true; }
    bool hasEditing() const override { return !isReadOnly(); }
    void showFindBar() override;        // lazy-instantiates internal FindBar; activates
    void showReplaceBar() override;     // no-op for v1; reserved
    void hideFindBar() override;        // deactivates + hides internal FindBar

    // Theme
    Markoff::Theme theme() const;
    void setTheme(const Markoff::Theme &);

    // Escape hatch for raw QPlainTextEdit access. Consumers may use any
    // method on the returned pointer EXCEPT setPlainText, setDocument, or
    // other mutators that bypass SourceTextDocumentBinding.
    QPlainTextEdit *plainTextEdit() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *e) override;

Q_SIGNALS:
    void themeChanged();

private:
    // ... members ...
    friend class Markoff::Source::Detail::Gutter;
};
```

### `Markoff::Source::FindBar`

```cpp
class FindBar : public QWidget {
    Q_OBJECT
public:
    explicit FindBar(Editor *editor, QWidget *parent = nullptr);

public Q_SLOTS:
    void activate();   // show + focus + (re-search current needle)
    void deactivate(); // hide + clear highlights

Q_SIGNALS:
    void closed();

    // ... private slots / members unchanged ...
};
```

### `Markoff::Source::Detail::Gutter`

Internal. Declared in `src/Gutter.h`, not in the public include path. Forward-decl and friend in `Editor.h` are the only public-header presence.

## Testing strategy

### Mechanical rename (D3, D4)

- Update all tests under `libs/markoff-source/tests/` for the new namespace.
- `tst_v10_source_editor_view_contract` updates `Markoff::Source::Widget::Editor` → `Markoff::Source::Editor`. The cursor round-trip slot (just landed via the B1 source-binding fix) continues to pass.
- `grep -rn 'Markoff::Source::Widget' libs/ apps/` returns zero post-rename.

### Behavior change (D1, D2)

New slots on `tst_source_widget_editor` (file rename to `tst_source_editor` is OK as part of the namespace sweep):

- `show_findbar_creates_visible_bar`:
  - Construct Editor, attach a MarkoffDocument with multi-block content.
  - `editor->showFindBar()`.
  - Assert: bar is non-null (use a Q_INVOKABLE `findBarForTest()` or a friend hook), `bar->isVisible()` is true, bar is positioned at the bottom of the Editor's rect.
- `hide_findbar_clears_highlights`:
  - `editor->showFindBar()`, populate find input via Qt key events, assert highlights present (via `editor->plainTextEdit()->extraSelections()` non-empty).
  - `editor->hideFindBar()`.
  - Assert: bar is hidden, `extraSelections()` is empty.
- `showFindBar_is_idempotent`:
  - Two consecutive `showFindBar()` calls don't create two bars; the second activates the existing instance.
- `findbar_close_signal_hides`:
  - `editor->showFindBar()`, emit `FindBar::closed`.
  - Assert: bar is hidden (routes to `Editor::hideFindBar`).

The forwarder removal (D1) needs no test — compile errors at any external caller catch it.

### Cross-leaf check

The "implement `MarkdownView::showFindBar` on `markoff-live` too" follow-on (out of this spec's scope) will need symmetric tests; the markoff-source pattern shown here is the template.

## Migration / commit plan

| Step | Files | Tests run |
|---|---|---|
| 1. Namespace flatten (D3) | Editor.h, FindBar.h, Editor.cpp, FindBar.cpp, Gutter.h, Gutter.cpp, all `libs/markoff-source/tests/*.cpp`, `tst_v10_source_editor_view_contract.cpp` | `scripts/run-tests.sh -R 'source\|v10_source'` |
| 2. Gutter to Detail (D4) | Editor.h, Editor.cpp, Gutter.h, Gutter.cpp | same |
| 3. Drop forwarders + impl showFindBar/hideFindBar (D1 + D2) | Editor.h, Editor.cpp, `tst_source_widget_editor.cpp` (new slots), Gutter/FindBar callsite updates | same + verify `tst_source_widget_findbar` still passes |

After step 3, full fast suite (`scripts/run-tests.sh -E 'tst_realistic\|tst_benchmark'`) must remain at 216/216.

## Risks

1. **`Editor::resizeEvent` complexity grows.** Currently it lays out the inner `QPlainTextEdit` and `Gutter`; adding `FindBar` makes it a three-way layout. Mitigate: extract a `relayout()` private method called from `resizeEvent` + `showFindBar`/`hideFindBar`.
2. **External callers of forwarder methods.** Only this repo's code today; `grep` confirms no other consumers. Corbomite is not yet on this branch. Safe.
3. **`FindBar`'s constructor takes a raw `Editor *`.** Currently it reaches into `Editor::plainTextEdit()` to run searches. After D1 the accessor is still public; FindBar's implementation continues to work unchanged. No risk.
4. **`showReplaceBar()` left as no-op.** A consumer expecting it to do something will be surprised. Mitigate: docstring explicitly notes "v1: no-op; reserved for future ReplaceBar widget."

## Open questions / decisions deferred

1. **Cross-leaf symmetry.** `markoff-live` should grow `showFindBar`/`hideFindBar` impls too. Not in this spec's scope; the freeze of `markoff-live` will handle it. The MarkdownView base class's default no-op stays; views opt in by overriding.
2. **`ReplaceBar` design.** A future spec; outside §4.6 freeze scope.
3. **Find-bar position (top vs bottom).** Locked to bottom for v1. If a future host wants top, either configure via a setter or use the host-managed FindBar path.
