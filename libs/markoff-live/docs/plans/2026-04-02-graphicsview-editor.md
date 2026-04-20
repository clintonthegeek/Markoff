# GraphicsView Editor Implementation Plan

> **Status: IMPLEMENTED** — Editor is now QGraphicsView-based with
> SceneCoordinator, MarkdownSplitter, and SelectionManager.

**Goal:** Replace the QAbstractScrollArea-based Editor with a QGraphicsView-based editor that splits markdown at block boundaries and supports cross-boundary selection.

**Architecture:** New `Editor` is a QGraphicsView wrapping a SelectionScene. A `MarkdownSplitter` uses TreeSitterParser to find `pipe_table` and `fenced_code_block` nodes and split markdown into segments. A `SceneCoordinator` manages the ordered item list, positions items vertically, and handles re-splitting on edit. Each MarkdownTextItem gets its own MarkdownHighlighter.

**Tech Stack:** C++20, Qt6, tree-sitter, KSyntaxHighlighting, our forked TextControl.

**Specs:** `docs/specs/2026-04-02-graphicsview-editor-design.md`, `docs/specs/2026-04-02-cross-boundary-selection-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `include/markoff/Editor.h` | **Rewrite** | QGraphicsView subclass, same public API |
| `src/Editor.cpp` | **Rewrite** | Editor implementation |
| `src/Editor_p.h` | **Delete** | Old private header |
| `src/MarkdownSplitter.h` | **Create** | Split markdown into text/block segments |
| `src/MarkdownSplitter.cpp` | **Create** | TreeSitterParser-based boundary detection |
| `src/SceneCoordinator.h` | **Create** | Manage item list, positioning, split/merge |
| `src/SceneCoordinator.cpp` | **Create** | Reparse, re-split, serialization |
| `src/MarkdownTextItem.h` | **Modify** | Add key events, width setter, focus transfer |
| `src/MarkdownTextItem.cpp` | **Modify** | Key event forwarding, cursor boundary detection |
| `src/TreeSitterParser.h` | **Modify** | Add findBlockBoundaries() method |
| `src/TreeSitterParser.cpp` | **Modify** | Implement block boundary detection |
| `tests/tst_splitter.cpp` | **Create** | Tests for MarkdownSplitter |
| `tests/tst_selection.cpp` | **Modify** | Fix any compile issues from Editor change |
| `tests/CMakeLists.txt` | **Modify** | Add splitter test |
| `CMakeLists.txt` | **Modify** | Update source list |
| `app/MainWindow.h` | **Modify** | Use new Editor API |
| `app/MainWindow.cpp` | **Modify** | Use new Editor API |

## Tasks

### Task 1: Delete old Editor, create shell
### Task 2: MarkdownSplitter + tests
### Task 3: Key events + cursor boundary in MarkdownTextItem
### Task 4: SceneCoordinator
### Task 5: New Editor widget
### Task 6: Per-item highlighting + reparse
### Task 7: Mode switching
### Task 8: Update test app
### Task 9: Integration test + polish
