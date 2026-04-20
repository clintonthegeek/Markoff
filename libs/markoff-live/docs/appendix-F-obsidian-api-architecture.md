# Appendix F: Obsidian API Architecture

Agent report: complete analysis of the Obsidian API type definitions (`obsidian.d.ts`, 7,517 lines) and developer documentation. Reveals the editor's internal data model and architecture.

---

## Top-Level App Object

**`App`** is the root service locator:

- `keymap: Keymap` — manages scope stack for keyboard events
- `scope: Scope` — the root scope for hotkeys
- `workspace: Workspace` — layout/pane management
- `vault: Vault` — file I/O
- `metadataCache: MetadataCache` — parsed/indexed file metadata
- `fileManager: FileManager` — user-facing file operations (rename with link updates, frontmatter processing, trash)
- `lastEvent: UserEvent | null` — last known input event (for modifier key detection)
- `renderContext: RenderContext` — (since 1.10.0) for rendering Values
- `secretStorage: SecretStorage` — (since 1.11.4) secure credential storage

---

## View Hierarchy

```
Component
  -> View                    (abstract: leaf, containerEl, scope, navigation)
    -> ItemView              (adds contentEl, addAction for toolbar buttons)
      -> FileView            (adds file: TFile, onLoadFile/onUnloadFile/onRename)
        -> EditableFileView  (marker class)
          -> TextFileView    (adds data: string, requestSave(), getViewData/setViewData/clear)
            -> MarkdownView  (the main markdown editor view)
```

### MarkdownView

- `editor: Editor` — the text editing abstraction
- `previewMode: MarkdownPreviewView` — the reading mode renderer
- `currentMode: MarkdownSubView` — whichever sub-view is active
- `getMode(): 'source' | 'preview'`
- `showSearch(replace?: boolean)` — opens the search bar

### MarkdownSubView Interface

Shared by both edit and preview modes:
- `getScroll(): number`
- `applyScroll(scroll: number)`
- `get(): string`
- `set(data: string, clear: boolean)`

---

## Editor Abstraction

**`Editor`** — abstract class bridging CM5 and CM6:

### Core Operations
- `getValue() / setValue(content)` — full document
- `getLine(line) / setLine(n, text)` — single line (0-indexed)
- `lineCount() / lastLine()`
- `getSelection() / replaceSelection(replacement, origin?)`
- `getRange(from, to) / replaceRange(replacement, from, to?, origin?)`
- `getCursor(side?) / setCursor(pos)` — cursor positions
- `listSelections() / setSelection(anchor, head?) / setSelections(ranges, main?)`
- `focus() / blur() / hasFocus()`
- `getScrollInfo() / scrollTo(x?, y?) / scrollIntoView(range, center?)`
- `undo() / redo()`
- `exec(command: EditorCommandName)` — execute built-in commands
- `transaction(tx: EditorTransaction, origin?)` — atomic batch operations
- `wordAt(pos) / posToOffset(pos) / offsetToPos(offset)`
- `processLines(read, write, ignoreEmpty?)` — bulk line processing
- `somethingSelected() / getDoc()`

### EditorCommandName

```typescript
'goUp' | 'goDown' | 'goLeft' | 'goRight' |
'goStart' | 'goEnd' |
'goWordLeft' | 'goWordRight' |
'indentMore' | 'indentLess' |
'newlineAndIndent' |
'swapLineUp' | 'swapLineDown' |
'deleteLine' |
'toggleFold' | 'foldAll' | 'unfoldAll'
```

### Editor Types

- `EditorPosition` = `{ line: number; ch: number }`
- `EditorRange` = `{ from: EditorPosition; to: EditorPosition }`
- `EditorSelection` = `{ anchor: EditorPosition; head: EditorPosition }`
- `EditorTransaction` = `{ replaceSelection?; changes?: EditorChange[]; selections?; selection? }`
- `EditorChange` extends `EditorRangeOrCaret` with `text: string`

---

## CodeMirror 6 Integration

Obsidian exposes CM6 through imports and StateFields:

```typescript
import { Extension, StateField } from '@codemirror/state';
import { EditorView, ViewPlugin } from '@codemirror/view';
```

### Key StateFields

- **`editorEditorField: StateField<EditorView>`** — access the CM6 EditorView
- **`editorInfoField: StateField<MarkdownFileInfo>`** — access the current file/editor info
- **`editorLivePreviewField: StateField<boolean>`** — check if Live Preview is active
- **`livePreviewState: ViewPlugin<LivePreviewStateType>`** — tracks mouse state during Live Preview

### Accessing CM6 from MarkdownView (undocumented)

```typescript
// @ts-expect-error, not typed
const editorView = view.editor.cm as EditorView;
```

---

## MarkdownEditView

The actual editing component inside MarkdownView. Implements `MarkdownSubView`, `HoverParent`, `MarkdownFileInfo`:
- `app: App`
- `constructor(view: MarkdownView)`
- `clear() / get() / set(data, clear)`
- `file: TFile` (getter)
- `getSelection()`
- `getScroll() / applyScroll(scroll)`

---

## Markdown Rendering & Post-Processing

### Two Completely Separate Pipelines

1. **Source/Live Preview**: CM6 editor with extensions (StateFields, ViewPlugins, Decorations)
2. **Reading view**: HTML post-processing pipeline via `MarkdownPostProcessor`

### MarkdownRenderer

```typescript
// Renders markdown string to HTML element
static render(app, markdown, el, sourcePath, component): Promise<void>
```

### MarkdownPostProcessor

```typescript
(el: HTMLElement, ctx: MarkdownPostProcessorContext) => Promise<any> | void
```
With optional `sortOrder?: number` (lower runs first, default 0).

### MarkdownPostProcessorContext

- `docId: string`
- `sourcePath: string` — for resolving relative links
- `frontmatter: any | null`
- `addChild(child: MarkdownRenderChild)` — lifecycle management
- `getSectionInfo(el): MarkdownSectionInformation | null`

### MarkdownSectionInformation

```typescript
{ text: string; lineStart: number; lineEnd: number }
```

### MarkdownPreviewRenderer (static class)

- `registerPostProcessor(postProcessor, sortOrder?)`
- `unregisterPostProcessor(postProcessor)`
- `createCodeBlockPostProcessor(language, handler)`

---

## Workspace Architecture

### Layout Tree

- `leftSplit: WorkspaceSidedock | WorkspaceMobileDrawer`
- `rightSplit: WorkspaceSidedock | WorkspaceMobileDrawer`
- `leftRibbon: WorkspaceRibbon`
- `rootSplit: WorkspaceRoot`

### Workspace Item Hierarchy

```
WorkspaceItem (abstract)
  -> WorkspaceParent (abstract)
    -> WorkspaceSplit
      -> WorkspaceContainer
        -> WorkspaceRoot        (main window)
        -> WorkspaceWindow      (popout window)
      -> WorkspaceSidedock      (collapsed, toggle/collapse/expand)
    -> WorkspaceTabs
    -> WorkspaceFloating
    -> WorkspaceMobileDrawer
  -> WorkspaceLeaf              (parent, view, openFile, getViewState/setViewState)
```

### WorkspaceLeaf

- `parent: WorkspaceTabs | WorkspaceMobileDrawer`
- `view: View` — the associated view
- `openFile(file, openState?)` — open a file in this leaf
- `open(view)` — open a specific view
- `getViewState() / setViewState(viewState, eState?)`
- `isDeferred: boolean` — lazy-loaded tabs (since 1.7.2)
- `togglePinned() / setPinned(pinned)`
- `setGroup(group) / setGroupMember(other)` — linked views
- `detach()` — remove from workspace

### Leaf Management

- `getLeaf(newLeaf?: PaneType | boolean)` — PaneType = `'tab' | 'split' | 'window'`
- `createLeafBySplit(leaf, direction?, before?)`
- `duplicateLeaf(leaf, leafType, direction?)`
- `moveLeafToPopout(leaf, data?)`
- `setActiveLeaf(leaf, params?)`

### Workspace Events

- `'active-leaf-change'` — active leaf changed
- `'file-open'` — new file opened
- `'layout-change'` — layout structure changed
- `'css-change'` — CSS changed
- `'editor-menu'` — context menu in editor
- `'editor-change'` — editor content changed
- `'editor-paste' / 'editor-drop'` — clipboard/drag events
- `'file-menu' / 'files-menu'` — context menu on files
- `'url-menu'` — context menu on external URLs
- `'window-open' / 'window-close'` — popout window lifecycle
- `'resize'` — layout/size changed
- `'quit'` — app closing

---

## Vault

**`Vault`** extends `Events`:

### File Retrieval
- `getFileByPath(path): TFile | null`
- `getFolderByPath(path): TFolder | null`
- `getAbstractFileByPath(path): TAbstractFile | null`
- `getRoot(): TFolder`
- `getAllLoadedFiles(): TAbstractFile[]`
- `getMarkdownFiles(): TFile[]`
- `getFiles(): TFile[]`

### CRUD
- `create(path, data, options?) / createBinary(path, data, options?)` — returns TFile
- `createFolder(path)` — returns TFolder
- `read(file) / cachedRead(file) / readBinary(file)` — cachedRead more performant
- `modify(file, data, options?) / modifyBinary(file, data, options?)`
- `append(file, data, options?)`
- `process(file, fn, options?)` — atomic read-modify-write
- `copy(file, newPath)`
- `rename(file, newPath)` — does NOT update links (use FileManager.renameFile)
- `delete(file, force?) / trash(file, system)`
- `getResourcePath(file)` — browser-usable URI

### File Types
- `TAbstractFile`: `{ vault; path; name; parent }`
- `TFile` extends: `{ stat: FileStats; basename; extension }`
- `TFolder` extends: `{ children; isRoot() }`
- `FileStats`: `{ ctime; mtime; size }` (millisecond timestamps)

---

## MetadataCache

### Lookup
- `getFirstLinkpathDest(linkpath, sourcePath): TFile | null` — resolve a link
- `getFileCache(file): CachedMetadata | null`
- `getCache(path): CachedMetadata | null`
- `fileToLinktext(file, sourcePath, omitMdExtension?)` — shortest unambiguous link text

### CachedMetadata

```typescript
{
  links?: LinkCache[];           // internal links
  embeds?: EmbedCache[];         // embedded content
  tags?: TagCache[];
  headings?: HeadingCache[];
  footnotes?: FootnoteCache[];
  footnoteRefs?: FootnoteRefCache[];
  referenceLinks?: ReferenceLinkCache[];
  sections?: SectionCache[];     // root-level blocks
  listItems?: ListItemCache[];
  frontmatter?: FrontMatterCache;
  frontmatterPosition?: Pos;
  frontmatterLinks?: FrontmatterLinkCache[];
  blocks?: Record<string, BlockCache>;
}
```

### Section Types

```typescript
SectionCache.type =
  'blockquote' | 'callout' | 'code' | 'element' |
  'footnoteDefinition' | 'heading' | 'html' | 'list' |
  'paragraph' | 'table' | 'text' | 'thematicBreak' | 'yaml'
```

### ListItemCache

```typescript
{ position: Pos; id?: string; task?: string; parent: number }
```
- `task` is the checkbox character (undefined = not a task, `' '` = unchecked, `'x'` = checked, etc.)
- `parent` is line number of parent list item

### Position Types

- `Pos` = `{ start: Loc; end: Loc }`
- `Loc` = `{ line: number; col: number; offset: number }` (0-based)

---

## Command System

### Command

```typescript
{
  id: string;           // globally unique
  name: string;         // human-readable
  icon?: IconName;
  mobileOnly?: boolean;
  repeatable?: boolean; // fire repeatedly while hotkey held
  callback?: () => any;
  checkCallback?: (checking: boolean) => boolean | void;
  editorCallback?: (editor, ctx) => any;
  editorCheckCallback?: (checking, editor, ctx) => boolean | void;
  hotkeys?: Hotkey[];
}
```

### Hotkey

```typescript
{ modifiers: Modifier[]; key: string }
```

### Modifier

```typescript
'Mod' | 'Ctrl' | 'Meta' | 'Shift' | 'Alt'
```
`Mod` = Cmd on macOS, Ctrl elsewhere.

---

## Keymap / Scope System

### Keymap

- `pushScope(scope) / popScope(scope)` — scope stack
- `static isModifier(evt, modifier): boolean`
- `static isModEvent(evt?): PaneType | boolean` — Cmd/Ctrl=tab, Cmd/Ctrl+Alt=split, Cmd/Ctrl+Alt+Shift=window

### Scope

- Hierarchical scopes with `constructor(parent?: Scope)`
- `register(modifiers, key, func): KeymapEventHandler`
- Return `false` from handler to preventDefault

---

## EditorSuggest (Live Autocomplete)

```typescript
abstract class EditorSuggest<T> extends PopoverSuggest {
  context: EditorSuggestContext | null;
  limit: number;
  abstract onTrigger(cursor, editor, file): EditorSuggestTriggerInfo | null;
  abstract getSuggestions(context): T[] | Promise<T[]>;
}
```

- `onTrigger` — called every keypress, must be fast
- `EditorSuggestTriggerInfo` = `{ start; end; query }`

---

## FileManager

- `getNewFileParent(sourcePath, newFilePath?)` — respects user's preference
- `renameFile(file, newPath)` — updates all links
- `generateMarkdownLink(file, sourcePath, subpath?, alias?)` — generates proper link syntax
- `processFrontMatter(file, fn, options?)` — atomic frontmatter editing
- `getAvailablePathForAttachment(filename, sourcePath?)` — deduped path

---

## Canvas File Format (canvas.d.ts)

### CanvasData
```typescript
{ nodes: AllCanvasNodeData[]; edges: CanvasEdgeData[] }
```

### Node Types

- **`CanvasFileData`**: `{ type: 'file'; file: string; subpath?: string }`
- **`CanvasTextData`**: `{ type: 'text'; text: string }`
- **`CanvasLinkData`**: `{ type: 'link'; url: string }`
- **`CanvasGroupData`**: `{ type: 'group'; label?; background?; backgroundStyle? }`

Common fields: `{ id; x; y; width; height; color? }`

### Edges

```typescript
{ id; fromNode; fromSide?; fromEnd?; toNode; toSide?; toEnd?; color?; label? }
```

- `NodeSide` = `'top' | 'right' | 'bottom' | 'left'`
- `EdgeEnd` = `'none' | 'arrow'`
- `BackgroundStyle` = `'cover' | 'ratio' | 'repeat'`
- `CanvasColor` = string (`'1'`-`'6'` for preset, or hex `'#FFFFFF'`)

---

## Rendering Utilities (Global Functions)

- `loadMathJax()` / `finishRenderMath()` / `renderMath(source, display)` — LaTeX
- `loadMermaid()` — diagrams
- `loadPdfJs()` — PDF rendering
- `loadPrism()` — syntax highlighting
- `htmlToMarkdown(html): string` — HTML -> Markdown
- `parseLinktext(linktext): { path; subpath }`
- `parseYaml(yaml) / stringifyYaml(obj)`
- `normalizePath(path)` — vault path normalization
- `parseFrontMatterAliases/Tags/Entry/StringArray(frontmatter)` — frontmatter helpers
- `prepareFuzzySearch(query) / prepareSimpleSearch(query)` — search callbacks
- `addIcon(iconId, svgContent) / removeIcon(iconId) / setIcon(parent, iconId)` — icons

---

## CSS Variables Architecture (from developer docs)

### Foundations
- `--color-base-00` through `--color-base-100` (neutral palette)
- Accent HSL: `--accent-h/s/l`
- Extended colors: red through pink, each with `-rgb` variant
- Surface colors: `--background-primary/secondary`, modifiers for hover/active/border/error/success
- Interactive colors
- Text: `--text-normal/muted/faint/accent/error/warning/success`

### Editor Variables
- Per-heading: `--h1-` through `--h6-` for color, font, line-height, size, style, variant, weight
- Code: `--code-background/white-space/size` plus syntax colors (`--code-normal/comment/function/important/keyword/operator/property/punctuation/string/tag/value`)
- Variables for callouts, blockquotes, lists, tables, links, tags, embeds, properties, footnotes, horizontal rules, inline title

**Note**: "Obsidian uses two different libraries for syntax highlighting — one for Editing view and another for Reading view — styling may not match perfectly between the two."

---

## Key Architectural Patterns

1. **Component lifecycle**: Everything inherits from `Component` with `load()/onload()/unload()/onunload()`, child management via `addChild/removeChild`, automatic cleanup via `register(cb)`, `registerEvent(eventRef)`, `registerDomEvent(...)`, `registerInterval(id)`.

2. **Event system**: `Events` class with `on(name, callback, ctx?)` returning `EventRef`, `off(name, callback)`, `offref(ref)`, `trigger(name, ...data)`.

3. **Deferred views** (since 1.7.2): Background leaves defer loading via `DeferredView` placeholder until `loadIfDeferred()` or `revealLeaf()`.

4. **Multi-window**: Supports popout windows (`WorkspaceWindow`), with `activeWindow` / `activeDocument` globals.

5. **Bases** (since 1.10.0): Database-like query system over vault properties, with value hierarchy: `NullValue`, `BooleanValue`, `NumberValue`, `StringValue`, `DateValue`, `ListValue`, `ObjectValue`, `LinkValue`, `TagValue`, `FileValue`, `HTMLValue`, `UrlValue`, `RegExpValue`, `ErrorValue`.

6. **Plugin registration**: Plugins register CM6 extensions for Live Preview, post-processors for Reading view, EditorSuggest instances for autocomplete, commands for the hotkey system, and views for custom pane types.
