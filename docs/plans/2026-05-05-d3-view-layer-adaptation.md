# D3 — View-layer adaptation — Implementation Plan (Part 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the view-layer rebuild that D2's Phase 11 began: cursor delivery redesign, inline span/attrs population, kind-transition detection, L6 full delegates, L7 (ListItem/Blockquote), L8 (Math + BlockInternalEdit), per-block undo UI.

**Architecture:** Foundation gets a thin `BlockSerializerRegistry` abstract interface and `AttrNames` namespace. `LiveListModelBinding` emits structural row-change signals; `LiveCursorState` subscribes to those instead of the parse-cycle path. Kind-transition detection runs synchronously in `onD2Changed`. New QML delegates cover all block kinds.

**Tech Stack:** C++20, Qt 6.8, QML, CMake 3.19, KF6::SyntaxHighlighting, jkqtmathtext.

**Spec:** `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md`

**Part 2:** `docs/plans/2026-05-05-d3-view-layer-adaptation-part2.md` (L6 delegate details, L7, L8, undo UI, tests)

---

## File map

### New files
| File | Responsibility |
|---|---|
| `libs/markoff-core/include/markoff-foundation/BlockSerializerRegistry.h` | Abstract `Markoff::BlockSerializerRegistry` interface |
| `libs/markoff-core/include/markoff-foundation/AttrNames.h` | `Markoff::AttrNames` inline constants |
| `libs/markoff-live-render/src/KindTransition.h` | `inferBlockKind` free function declaration |
| `libs/markoff-live-render/src/KindTransition.cpp` | `inferBlockKind` implementation |

### Modified files
| File | Change |
|---|---|
| `libs/markoff-core/include/markoff-foundation/BlockSerializer.h` | Rename `BlockSerializerRegistry` → `BuiltinBlockSerializerRegistry` |
| `libs/markoff-core/src/BlockSerializer.cpp` | Update class name |
| `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h` | Add `BlockSerializerRegistry*` ctor param + `Q_INVOKABLE` undo methods |
| `libs/markoff-core/src/MarkoffDocument.cpp` | Implement new methods; fix `d2InsertBlock`/`d2RemoveBlock` |
| `libs/markoff-live-render/include/markoff/live-render/BlockKind.h` | Add `ListItem`, `Blockquote`, `Math` constants |
| `libs/markoff-live-render/src/BlockKind.cpp` | Add string definitions |
| `libs/markoff-live-render/include/markoff/live-render/BlockRecord.h` | Add `attrs` field |
| `libs/markoff-live-render/include/markoff/live-render/LiveBlockModel.h` | Add `BlockAttrsRole` |
| `libs/markoff-live-render/src/LiveBlockModel.cpp` | Implement `BlockAttrsRole` |
| `libs/markoff-live-render/include/markoff/live-render/BlockKindDescriptor.h` | Add `serializer` callback field |
| `libs/markoff-live-render/include/markoff/live-render/BlockKindRegistry.h` | Inherit `Markoff::BlockSerializerRegistry` |
| `libs/markoff-live-render/src/BlockKindRegistry.cpp` | Implement `serialize()`; register ListItem/Blockquote/Math |
| `libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h` | Add `structuralRowsInserted`/`structuralRowRemoved` signals |
| `libs/markoff-live-render/src/LiveListModelBinding.cpp` | Emit structural signals; populate spans/attrs; kind-transition; registry injection |
| `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h` | Remove parse-cycle path; add structural signal wiring |
| `libs/markoff-live-render/src/LiveCursorState.cpp` | Implement new structural signal handlers |
| `libs/markoff-live-render/CMakeLists.txt` | Add `KindTransition.cpp` to SOURCES; add jkqtmathtext to link deps |
| `CMakeLists.txt` (root) | Add `add_subdirectory(libs/jkqtmathtext)` |

### Deleted files
| File |
|---|
| `libs/markoff-live-render/src/BlockWalker.h` |
| `libs/markoff-live-render/src/BlockWalker.cpp` |

---

## Phase 1 — Foundation amendments

### Task 1: Rename `BlockSerializerRegistry` singleton → `BuiltinBlockSerializerRegistry`; add abstract interface

The existing `BlockSerializerRegistry` in `BlockSerializer.h` is a concrete singleton. The spec adds an abstract interface with the same name. Rename the concrete one first to clear the name.

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/BlockSerializer.h`
- Modify: `libs/markoff-core/src/BlockSerializer.cpp`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp` (two call sites)
- Create: `libs/markoff-core/include/markoff-foundation/BlockSerializerRegistry.h`

- [ ] **Step 1: Rename in header**

In `BlockSerializer.h`, change `class MARKOFF_FOUNDATION_EXPORT BlockSerializerRegistry` → `class MARKOFF_FOUNDATION_EXPORT BuiltinBlockSerializerRegistry`. Update `instance()` return type accordingly. Keep `using BlockSerializer = std::function<...>` unchanged.

Full new header:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <QByteArray>
#include <QHash>
#include <functional>

namespace Markoff {

using BlockSerializer = std::function<QByteArray(BlockKind,
                                                  const QHash<AttrName, AttrValue> &,
                                                  const QByteArray &content)>;

class MARKOFF_FOUNDATION_EXPORT BuiltinBlockSerializerRegistry {
public:
    static BuiltinBlockSerializerRegistry &instance();
    void registerSerializer(BlockKind kind, BlockSerializer fn);
    BlockSerializer get(BlockKind kind) const;
    void registerBuiltins();
private:
    QHash<uint8_t, BlockSerializer> m_serializers;
    bool m_builtinsRegistered = false;
};

}  // namespace Markoff
```

- [ ] **Step 2: Update BlockSerializer.cpp**

Replace all occurrences of `BlockSerializerRegistry` with `BuiltinBlockSerializerRegistry` in `src/BlockSerializer.cpp`.

- [ ] **Step 3: Update MarkoffDocument.cpp call sites**

```bash
grep -n "BlockSerializerRegistry" libs/markoff-core/src/MarkoffDocument.cpp
```

Expected: lines referencing `BlockSerializerRegistry::instance()`. Change both to `BuiltinBlockSerializerRegistry::instance()`.

- [ ] **Step 4: Create abstract interface header**

Create `libs/markoff-core/include/markoff-foundation/BlockSerializerRegistry.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <QByteArray>
#include <QHash>

namespace Markoff {

class MARKOFF_FOUNDATION_EXPORT BlockSerializerRegistry {
public:
    virtual ~BlockSerializerRegistry() = default;
    virtual QByteArray serialize(BlockKind kind,
                                 const QByteArray &text,
                                 const QHash<AttrName, AttrValue> &attrs) const = 0;
};

}  // namespace Markoff
```

- [ ] **Step 5: Build to verify rename compiles**

```bash
cmake --build build-dev --target markoff_core -j 8
```
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/BlockSerializer.h \
        libs/markoff-core/include/markoff-foundation/BlockSerializerRegistry.h \
        libs/markoff-core/src/BlockSerializer.cpp \
        libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "refactor(foundation): rename BlockSerializerRegistry singleton → BuiltinBlockSerializerRegistry; add abstract interface

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 2: Add `AttrNames.h`

**Files:**
- Create: `libs/markoff-core/include/markoff-foundation/AttrNames.h`

- [ ] **Step 1: Create header**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/BlockAttrsMap.h>

namespace Markoff::AttrNames {
    inline const AttrName Level       = "level";        // Heading: int 1–6
    inline const AttrName InfoString  = "infoString";   // CodeBlock: QString
    inline const AttrName MarkerStyle = "markerStyle";  // ListItem: QString (e.g. "-", "*", "1.")
    inline const AttrName IndentLevel = "indentLevel";  // ListItem: int 0-based
    inline const AttrName Checked     = "checked";      // ListItem: bool (task list checkbox)
    inline const AttrName Src         = "src";          // Image: QString URL/path
    inline const AttrName Alt         = "alt";          // Image: QString alt text
    inline const AttrName Title       = "title";        // Image: QString (optional)
    inline const AttrName DisplayMode = "displayMode";  // Math: bool (true = display, false = inline)
}  // namespace Markoff::AttrNames
```

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target markoff_core -j 8
```
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/AttrNames.h
git commit -m "feat(foundation): add AttrNames namespace with inline AttrName constants

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 3: `MarkoffDocument` — constructor param + Q_INVOKABLE undo methods

**Files:**
- Modify: `libs/markoff-core/include/markoff-foundation/MarkoffDocument.h`
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Write failing test**

In `libs/markoff-core/tests/tst_foundation_d2.cpp` (or whichever D2 test file exists), add:

```cpp
void canUndoForBlock_returns_false_when_no_history() {
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello");
    auto ids = doc.iterateBlocks();
    QVERIFY(!ids.empty());
    Markoff::BlockAnchor anchor = ids[0];
    QVERIFY(!doc.canUndoForBlock(anchor));
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build-dev --target markoff_core -j 8 2>&1 | grep -E "error:|canUndo"
```
Expected: compile error — `canUndoForBlock` does not exist.

- [ ] **Step 3: Add constructor param + new methods to header**

In `MarkoffDocument.h`, change the constructor declaration and add methods:

```cpp
// Constructor: optional registry for custom-kind serialization.
explicit MarkoffDocument(quint16 replicaId,
                         const Markoff::BlockSerializerRegistry *registry = nullptr,
                         QObject *parent = nullptr);

// Registry accessor (may return nullptr if none was provided).
const Markoff::BlockSerializerRegistry *serializerRegistry() const;

// Per-block undo check + action (Q_INVOKABLE = callable from QML).
Q_INVOKABLE bool canUndoForBlock(Markoff::BlockAnchor blockAnchor) const;
Q_INVOKABLE void undoForBlock(Markoff::BlockAnchor blockAnchor);
```

Also add `#include <markoff-foundation/BlockSerializerRegistry.h>` to the header.

- [ ] **Step 4: Implement in MarkoffDocument.cpp**

In `MarkoffDocument::Private`, add:
```cpp
const Markoff::BlockSerializerRegistry *serializerRegistry = nullptr;
```

Update constructor:
```cpp
MarkoffDocument::MarkoffDocument(quint16 replicaId,
                                 const BlockSerializerRegistry *registry,
                                 QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->replicaId = replicaId;
    d->serializerRegistry = registry;
    // ... rest of existing constructor body unchanged ...
}
```

Add accessor:
```cpp
const BlockSerializerRegistry *MarkoffDocument::serializerRegistry() const
{
    return d->serializerRegistry;
}
```

Add `canUndoForBlock`:
```cpp
bool MarkoffDocument::canUndoForBlock(BlockAnchor blockAnchor) const
{
    BlockId id = BlockId(blockAnchor);
    return d->undoLog.hasEntryForBlock(id);
}
```

Add `undoForBlock` (BlockAnchor overload routing to existing `BlockId` overload):
```cpp
void MarkoffDocument::undoForBlock(BlockAnchor blockAnchor)
{
    undoForBlock(BlockId(blockAnchor));
}
```

Note: `BlockAnchor` and `BlockId` are the same underlying type (D2 premise). Check the existing `undoForBlock(BlockId)` signature and wire accordingly.

Check if `UndoLog::hasEntryForBlock` exists:
```bash
grep -n "hasEntryForBlock\|hasEntry" libs/markoff-core/include/markoff-foundation/UndoLog.h
```
If absent, add it:
```cpp
// In UndoLog.h public section:
bool hasEntryForBlock(BlockId id) const;
```
Implementation in `UndoLog.cpp`:
```cpp
bool UndoLog::hasEntryForBlock(BlockId id) const
{
    for (const auto &entry : m_entries) {
        if (entry.touchesBlock(id)) return true;
    }
    return false;
}
```
(Adapt to actual `UndoLog` internals — look at how `undoForBlock(BlockId)` iterates entries.)

- [ ] **Step 5: Run test**

```bash
cmake --build build-dev --target markoff_core -j 8 && \
ctest --test-dir build-dev -R tst_foundation -j 8 --output-on-failure
```
Expected: all foundation tests pass including new `canUndoForBlock_returns_false_when_no_history`.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-core/include/markoff-foundation/MarkoffDocument.h \
        libs/markoff-core/include/markoff-foundation/UndoLog.h \
        libs/markoff-core/src/MarkoffDocument.cpp \
        libs/markoff-core/src/UndoLog.cpp
git commit -m "feat(foundation): MarkoffDocument accepts BlockSerializerRegistry*; add Q_INVOKABLE canUndoForBlock/undoForBlock

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 4: Fix `d2InsertBlock`/`d2RemoveBlock` to fire `idListProxy->notifyChanged()`

**Files:**
- Modify: `libs/markoff-core/src/MarkoffDocument.cpp`

- [ ] **Step 1: Write failing test**

In a foundation test file, add:
```cpp
void d2InsertBlock_fires_idListProxy_notifyChanged() {
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello");

    QSignalSpy spy(doc.idListProxy(), &Markoff::IdListProxy::structureChanged);

    auto t = doc.d2UndoLog().beginTransaction();
    auto ids = doc.iterateBlocks();
    doc.d2InsertBlock(ids[0], Markoff::BlockKind::Paragraph, t);
    t.commit();

    QCOMPARE(spy.count(), 1);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build-dev --target markoff_core -j 8 && \
ctest --test-dir build-dev -R tst_foundation -j 8 --output-on-failure 2>&1 | grep -E "FAIL|PASS|spy"
```
Expected: test fails (`spy.count()` is 0).

- [ ] **Step 3: Fix d2InsertBlock**

In `MarkoffDocument.cpp`, find `BlockId MarkoffDocument::d2InsertBlock(...)`. After `scheduleD2Changed();` add:
```cpp
d->idListProxy->notifyChanged();
d->kindTagMapProxy->notifyChanged();
```

Fix `d2RemoveBlock` the same way — after `scheduleD2Changed();` add:
```cpp
d->idListProxy->notifyChanged();
d->kindTagMapProxy->notifyChanged();
```

- [ ] **Step 4: Run test**

```bash
cmake --build build-dev --target markoff_core -j 8 && \
ctest --test-dir build-dev -R tst_foundation -j 8 --output-on-failure
```
Expected: all foundation tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-core/src/MarkoffDocument.cpp
git commit -m "fix(foundation): d2InsertBlock/d2RemoveBlock fire idListProxy->notifyChanged()

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Phase 2 — Live-render model infrastructure

### Task 5: Add `ListItem`, `Blockquote`, `Math` to `BlockKind`

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/BlockKind.h`
- Modify: `libs/markoff-live-render/src/BlockKind.cpp`

- [ ] **Step 1: Write failing test**

In `tst_live_render_registry.cpp`, add:
```cpp
void registry_has_list_item_kind() {
    BlockKindRegistry reg;
    QVERIFY(reg.find(BlockKind::ListItem) != nullptr);
}
void registry_has_blockquote_kind() {
    BlockKindRegistry reg;
    QVERIFY(reg.find(BlockKind::Blockquote) != nullptr);
}
void registry_has_math_kind() {
    BlockKindRegistry reg;
    QVERIFY(reg.find(BlockKind::Math) != nullptr);
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build-dev --target tst_live_render_registry -j 8 2>&1 | grep "error:"
```
Expected: compile errors — `BlockKind::ListItem` etc. do not exist yet.

- [ ] **Step 3: Add constants to header**

```cpp
// In BlockKind.h, add to the BlockKind namespace:
MARKOFF_LIVE_RENDER_EXPORT extern const QString ListItem;    // "list-item"
MARKOFF_LIVE_RENDER_EXPORT extern const QString Blockquote;  // "blockquote"
MARKOFF_LIVE_RENDER_EXPORT extern const QString Math;        // "math"
```

- [ ] **Step 4: Add definitions to BlockKind.cpp**

```cpp
const QString BlockKind::ListItem   = QStringLiteral("list-item");
const QString BlockKind::Blockquote = QStringLiteral("blockquote");
const QString BlockKind::Math       = QStringLiteral("math");
```

- [ ] **Step 5: Update `blockKindToString` in `LiveListModelBinding.cpp`**

In the anonymous-namespace helper `blockKindToString`:
```cpp
case BK::ListItem:   return BlockKind::ListItem;
case BK::Blockquote: return BlockKind::Blockquote;
case BK::Math:       return BlockKind::Math;
```

- [ ] **Step 6: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: clean build. (Registry tests still fail — fixed in Task 8 when registration happens.)

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/BlockKind.h \
        libs/markoff-live-render/src/BlockKind.cpp \
        libs/markoff-live-render/src/LiveListModelBinding.cpp
git commit -m "feat(live-render): add ListItem/Blockquote/Math BlockKind string constants

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 6: Add `attrs` field to `BlockRecord`; add `BlockAttrsRole` to `LiveBlockModel`

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/BlockRecord.h`
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveBlockModel.h`
- Modify: `libs/markoff-live-render/src/LiveBlockModel.cpp`

- [ ] **Step 1: Write failing test**

In `tst_live_render_block_model.cpp`, add:
```cpp
void blockAttrsRole_returns_variant_map() {
    LiveBlockModel m;
    BlockRecord r;
    r.kind = BlockKind::ListItem;
    r.text = "- hello";
    r.blockAnchor = Markoff::BlockId::fromRaw(42);
    r.attrs.insert("markerStyle", QString("-"));
    r.attrs.insert("indentLevel", 0);
    QList<AstBlockDiff::Op> ops = AstBlockDiff::diff({}, { BlockKey{r.kind, r.blockAnchor} });
    m.applyOps(ops, {r});
    QVariant v = m.data(m.index(0), LiveBlockModel::BlockAttrsRole);
    QVERIFY(v.isValid());
}
```

- [ ] **Step 2: Run to verify fail**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8 2>&1 | grep "error:"
```
Expected: compile errors.

- [ ] **Step 3: Add `attrs` to BlockRecord**

```cpp
// In BlockRecord.h, add include:
#include <markoff-foundation/BlockAttrsMap.h>
// In struct BlockRecord, add field:
QHash<Markoff::AttrName, Markoff::AttrValue> attrs;
// attrs is excluded from operator== (same as inlineSpans):
// operator== already returns false on kind/text/headingLevel/codeLanguage/blockAnchor
```

- [ ] **Step 4: Add `BlockAttrsRole` to LiveBlockModel**

In `LiveBlockModel.h`:
```cpp
enum Role {
    KindRole         = Qt::UserRole + 1,
    TextRole,
    HeadingLevelRole,
    CodeLanguageRole,
    BlockAnchorRole,
    BlockAttrsRole,   // QVariantMap of block attributes
};
```

- [ ] **Step 5: Implement `BlockAttrsRole` in LiveBlockModel.cpp**

In `data()`, add a case:
```cpp
case BlockAttrsRole: {
    const auto &a = m_rows.at(row).attrs;
    QVariantMap map;
    for (auto it = a.cbegin(); it != a.cend(); ++it) {
        const QString key = QString::fromLatin1(it.key());
        map.insert(key, std::visit([](auto &&v) -> QVariant {
            return QVariant::fromValue(v);
        }, it.value()));
    }
    return map;
}
```

In `roleNames()`, add:
```cpp
roles[BlockAttrsRole] = "blockAttrs";
```

- [ ] **Step 6: Run test**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8 && \
ctest --test-dir build-dev -R tst_live_render_block_model --output-on-failure
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/BlockRecord.h \
        libs/markoff-live-render/include/markoff/live-render/LiveBlockModel.h \
        libs/markoff-live-render/src/LiveBlockModel.cpp
git commit -m "feat(live-render): BlockRecord.attrs field + LiveBlockModel BlockAttrsRole

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 7: `BlockKindDescriptor` serializer field + `BlockKindRegistry` inherits abstract interface + registers new kinds

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/BlockKindDescriptor.h`
- Modify: `libs/markoff-live-render/include/markoff/live-render/BlockKindRegistry.h`
- Modify: `libs/markoff-live-render/src/BlockKindRegistry.cpp`

- [ ] **Step 1: Write failing tests**

In `tst_live_render_registry.cpp`, add the tests from Task 5 step 1 plus:
```cpp
void blockKindRegistry_implements_serialize_interface() {
    BlockKindRegistry reg;
    // Cast check: BlockKindRegistry IS-A Markoff::BlockSerializerRegistry
    const Markoff::BlockSerializerRegistry *iface = &reg;
    QVERIFY(iface != nullptr);
}
void serialize_paragraph_returns_text() {
    BlockKindRegistry reg;
    QByteArray result = reg.serialize(
        Markoff::BlockKind::Paragraph, "hello world", {});
    QCOMPARE(result, QByteArray("hello world"));
}
```

- [ ] **Step 2: Add `serializer` field to BlockKindDescriptor**

```cpp
// In BlockKindDescriptor.h add include:
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <functional>
// In struct BlockKindDescriptor, add field:
/// Serializer callback: (text, attrs) → markdown bytes for this block.
/// Null means use the built-in fallback (passthrough text).
std::function<QByteArray(const QByteArray &text,
                          const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs)>
    serializer;
```

- [ ] **Step 3: Update BlockKindRegistry header**

```cpp
// In BlockKindRegistry.h add include:
#include <markoff-foundation/BlockSerializerRegistry.h>
// Change class declaration:
class MARKOFF_LIVE_RENDER_EXPORT BlockKindRegistry
    : public Markoff::BlockSerializerRegistry {
public:
    BlockKindRegistry();
    void register_(BlockKindDescriptor descriptor);
    const BlockKindDescriptor *find(const QString &id) const;
    QStringList kinds() const;

    // Markoff::BlockSerializerRegistry implementation
    QByteArray serialize(Markoff::BlockKind kind,
                         const QByteArray &text,
                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs) const override;
private:
    void registerBuiltins();
    QHash<QString, BlockKindDescriptor> m_descriptors;
};
```

- [ ] **Step 4: Implement `serialize()` in BlockKindRegistry.cpp**

```cpp
QByteArray BlockKindRegistry::serialize(Markoff::BlockKind kind,
                                         const QByteArray &text,
                                         const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs) const
{
    using BK = Markoff::BlockKind;
    // Map foundation BlockKind to LiveRender string kind
    QString kindStr;
    switch (kind) {
    case BK::Heading:        kindStr = BlockKind::Heading;       break;
    case BK::CodeBlock:      kindStr = BlockKind::CodeBlock;     break;
    case BK::HorizontalRule: kindStr = BlockKind::HorizontalRule; break;
    case BK::Image:          kindStr = BlockKind::Image;         break;
    case BK::ListItem:       kindStr = BlockKind::ListItem;      break;
    case BK::Blockquote:     kindStr = BlockKind::Blockquote;    break;
    case BK::Math:           kindStr = BlockKind::Math;          break;
    default:                 kindStr = BlockKind::Paragraph;     break;
    }
    const auto *desc = find(kindStr);
    if (desc && desc->serializer)
        return desc->serializer(text, attrs);
    return text;  // passthrough fallback
}
```

- [ ] **Step 5: Register ListItem, Blockquote, Math in `registerBuiltins()`**

```cpp
// ListItem
{
    BlockKindDescriptor d;
    d.id = BlockKind::ListItem;
    d.acceptsTextRoleUpdates = true;
    d.supportedCursorVariants = { QStringLiteral("TextCaret") };
    d.consumedStructuralKeys = {
        Qt::Key_Return, Qt::Key_Enter,
        Qt::Key_Backspace, Qt::Key_Delete,
        Qt::Key_Tab,
    };
    d.delegateUrl = QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/render/delegates/ListItemDelegate.qml");
    d.serializer = [](const QByteArray &text, const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
        return text;  // text is source-faithful (includes marker prefix)
    };
    m_descriptors.insert(d.id, d);
}
// Blockquote
{
    BlockKindDescriptor d;
    d.id = BlockKind::Blockquote;
    d.acceptsTextRoleUpdates = true;
    d.supportedCursorVariants = { QStringLiteral("TextCaret") };
    d.consumedStructuralKeys = {
        Qt::Key_Return, Qt::Key_Enter,
        Qt::Key_Backspace, Qt::Key_Delete,
    };
    d.delegateUrl = QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/render/delegates/BlockquoteDelegate.qml");
    d.serializer = [](const QByteArray &text, const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
        return text;
    };
    m_descriptors.insert(d.id, d);
}
// Math
{
    BlockKindDescriptor d;
    d.id = BlockKind::Math;
    d.acceptsTextRoleUpdates = true;
    d.supportedCursorVariants = {
        QStringLiteral("BlockSelected"),
        QStringLiteral("BlockInternalEdit"),
    };
    d.internalEditModes = { QStringLiteral("editing-latex") };
    d.consumedStructuralKeys = {
        Qt::Key_Return, Qt::Key_Enter,
        Qt::Key_Backspace, Qt::Key_Delete,
        Qt::Key_F2,
    };
    d.delegateUrl = QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/render/delegates/MathDelegate.qml");
    d.serializer = [](const QByteArray &text, const QHash<Markoff::AttrName, Markoff::AttrValue> &) {
        return text;
    };
    m_descriptors.insert(d.id, d);
}
```

Also update existing Image descriptor to add `BlockInternalEdit` variant:
```cpp
d.supportedCursorVariants = {
    QStringLiteral("BlockSelected"),
    QStringLiteral("BlockInternalEdit"),
};
d.internalEditModes = { QStringLiteral("alt-edit") };
```

And add consumed structural keys to HorizontalRule:
```cpp
d.consumedStructuralKeys = {
    Qt::Key_Delete, Qt::Key_Backspace,
    Qt::Key_Up, Qt::Key_Down,
};
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build-dev --target tst_live_render_registry -j 8 && \
ctest --test-dir build-dev -R tst_live_render_registry --output-on-failure
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/BlockKindDescriptor.h \
        libs/markoff-live-render/include/markoff/live-render/BlockKindRegistry.h \
        libs/markoff-live-render/src/BlockKindRegistry.cpp
git commit -m "feat(live-render): BlockKindRegistry inherits BlockSerializerRegistry; registers ListItem/Blockquote/Math

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 8: Delete `BlockWalker`; add `KindTransition.h/cpp`

**Files:**
- Delete: `libs/markoff-live-render/src/BlockWalker.h`
- Delete: `libs/markoff-live-render/src/BlockWalker.cpp`
- Create: `libs/markoff-live-render/src/KindTransition.h`
- Create: `libs/markoff-live-render/src/KindTransition.cpp`
- Modify: `libs/markoff-live-render/CMakeLists.txt`

- [ ] **Step 1: Write failing test for `inferBlockKind`**

Create `libs/markoff-live-render/tests/tst_live_render_kind_transition.cpp`:
```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
// Include via relative path since KindTransition is in src/ (internal)
// Tests are compiled against the library so use the internal header directly.
#include "../src/KindTransition.h"
#include <markoff/live-render/BlockKind.h>

using namespace Markoff::LiveRender;

class TstKindTransition : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void heading_detected()    { QCOMPARE(inferBlockKind("# Hello"), BlockKind::Heading); }
    void heading_h6()          { QCOMPARE(inferBlockKind("###### x"), BlockKind::Heading); }
    void code_block_backtick() { QCOMPARE(inferBlockKind("```cpp"), BlockKind::CodeBlock); }
    void code_block_tilde()    { QCOMPARE(inferBlockKind("~~~"), BlockKind::CodeBlock); }
    void hr_dashes()           { QCOMPARE(inferBlockKind("---"), BlockKind::HorizontalRule); }
    void hr_stars()            { QCOMPARE(inferBlockKind("***"), BlockKind::HorizontalRule); }
    void hr_underscores()      { QCOMPARE(inferBlockKind("___"), BlockKind::HorizontalRule); }
    void image_detected()      { QCOMPARE(inferBlockKind("![alt](url)"), BlockKind::Image); }
    void math_display()        { QCOMPARE(inferBlockKind("$$x^2$$"), BlockKind::Math); }
    void math_inline()         { QCOMPARE(inferBlockKind("$x$"), BlockKind::Math); }
    void list_dash()           { QCOMPARE(inferBlockKind("- item"), BlockKind::ListItem); }
    void list_star()           { QCOMPARE(inferBlockKind("* item"), BlockKind::ListItem); }
    void list_plus()           { QCOMPARE(inferBlockKind("+ item"), BlockKind::ListItem); }
    void list_ordered()        { QCOMPARE(inferBlockKind("1. item"), BlockKind::ListItem); }
    void list_ordered_paren()  { QCOMPARE(inferBlockKind("2) item"), BlockKind::ListItem); }
    void blockquote()          { QCOMPARE(inferBlockKind("> quote"), BlockKind::Blockquote); }
    void paragraph_plain()     { QCOMPARE(inferBlockKind("just text"), BlockKind::Paragraph); }
    void dollar_before_hash()  {
        // $ is matched before #; a line starting with "$#" is Math not Heading
        QCOMPARE(inferBlockKind("$# not a heading"), BlockKind::Math);
    }
    void double_dollar_before_single() {
        // $$ must match before $
        QCOMPARE(inferBlockKind("$$display$$"), BlockKind::Math);
        QCOMPARE(inferBlockKind("$$display$$"),
                 inferBlockKind("$$display$$"));  // idempotent
    }
    void display_mode_attr() {
        bool isDisplay = false;
        inferBlockKind("$$x$$", &isDisplay);
        QVERIFY(isDisplay);
        inferBlockKind("$x$", &isDisplay);
        QVERIFY(!isDisplay);
    }
};
QTEST_MAIN(TstKindTransition)
#include "tst_live_render_kind_transition.moc"
```

- [ ] **Step 2: Create `KindTransition.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace Markoff::LiveRender {

/// Infer the BlockKind string for a block's source text.
/// Rules are applied in order; first match wins.
/// If displayMode is non-null and the inferred kind is Math,
/// *displayMode is set to true for $$ prefix, false for $.
QString inferBlockKind(const QString &text, bool *displayMode = nullptr);

/// Count leading '#' characters before a space/EOL. Returns 0 if not a heading.
int countLeadingHashes(const QString &text);

}  // namespace Markoff::LiveRender
```

- [ ] **Step 3: Create `KindTransition.cpp`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindTransition.h"
#include <markoff/live-render/BlockKind.h>
#include <QRegularExpression>

namespace Markoff::LiveRender {

int countLeadingHashes(const QString &text)
{
    int n = 0;
    while (n < text.size() && n < 6 && text[n] == u'#') ++n;
    if (n == 0) return 0;
    // Must be followed by space, EOL, or end of string
    if (n < text.size() && text[n] != u' ' && text[n] != u'\n') return 0;
    return n;
}

QString inferBlockKind(const QString &text, bool *displayMode)
{
    if (text.isEmpty())
        return BlockKind::Paragraph;

    // Heading: 1–6 '#' followed by space or EOL
    if (countLeadingHashes(text) > 0)
        return BlockKind::Heading;

    // CodeBlock: starts with ``` or ~~~ (3+ chars)
    if ((text.startsWith(QStringLiteral("```")) && text.size() >= 3) ||
        (text.startsWith(QStringLiteral("~~~")) && text.size() >= 3))
        return BlockKind::CodeBlock;

    // HorizontalRule: trimmed is ---, ***, or ___
    {
        const QString trimmed = text.trimmed();
        if (trimmed == QStringLiteral("---") ||
            trimmed == QStringLiteral("***") ||
            trimmed == QStringLiteral("___"))
            return BlockKind::HorizontalRule;
    }

    // Image: starts with ![
    if (text.startsWith(QStringLiteral("![")))
        return BlockKind::Image;

    // Math: check $$ before $ (longest-match first)
    if (text.startsWith(QStringLiteral("$$"))) {
        if (displayMode) *displayMode = true;
        return BlockKind::Math;
    }
    if (text.startsWith(u'$')) {
        if (displayMode) *displayMode = false;
        return BlockKind::Math;
    }

    // ListItem: [-*+] followed by space, or \d+[.)]\s
    {
        static const QRegularExpression listRe(
            QStringLiteral("^[ \\t]{0,3}([-*+]|\\d+[.)])\\s"));
        if (listRe.match(text).hasMatch())
            return BlockKind::ListItem;
    }

    // Blockquote: starts with "> "
    if (text.startsWith(QStringLiteral("> ")) || text == QStringLiteral(">"))
        return BlockKind::Blockquote;

    return BlockKind::Paragraph;
}

}  // namespace Markoff::LiveRender
```

- [ ] **Step 4: Remove BlockWalker from CMakeLists; add KindTransition**

In `libs/markoff-live-render/CMakeLists.txt`:
- Remove `src/BlockWalker.h` and `src/BlockWalker.cpp` from SOURCES.
- Add `src/KindTransition.h` and `src/KindTransition.cpp` to SOURCES.
- Add test target at bottom:
```cmake
qt_add_executable(tst_live_render_kind_transition
    tests/tst_live_render_kind_transition.cpp
)
target_include_directories(tst_live_render_kind_transition PRIVATE src/)
target_link_libraries(tst_live_render_kind_transition PRIVATE
    Qt6::Core Qt6::Test markoff_live_render)
add_test(NAME tst_live_render_kind_transition
         COMMAND tst_live_render_kind_transition)
```

Add the test target to `libs/markoff-live-render/tests/CMakeLists.txt` as well (or whichever file manages tests — check).

- [ ] **Step 5: Delete BlockWalker files**

```bash
rm libs/markoff-live-render/src/BlockWalker.h \
   libs/markoff-live-render/src/BlockWalker.cpp
```

- [ ] **Step 6: Build and run kind-transition tests**

```bash
cmake --build build-dev --target tst_live_render_kind_transition -j 8 && \
ctest --test-dir build-dev -R tst_live_render_kind_transition --output-on-failure
```
Expected: all 18 tests pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live-render/src/KindTransition.h \
        libs/markoff-live-render/src/KindTransition.cpp \
        libs/markoff-live-render/tests/tst_live_render_kind_transition.cpp \
        libs/markoff-live-render/CMakeLists.txt \
        libs/markoff-live-render/tests/CMakeLists.txt
git rm libs/markoff-live-render/src/BlockWalker.h \
       libs/markoff-live-render/src/BlockWalker.cpp
git commit -m "feat(live-render): add KindTransition (inferBlockKind); delete dead BlockWalker

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 9: Update `onD2Changed` — inline spans, attrs, kind-transition, registry injection

This is the core of Phase 2. `onD2Changed` gains: inline span population, headingLevel/codeLanguage population, blockAttrs population, kind-transition detection call, and updated registry wiring from `doc->serializerRegistry()`.

**Files:**
- Modify: `libs/markoff-live-render/src/LiveListModelBinding.cpp`
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h` (Private registry injection)

- [ ] **Step 1: Write failing test**

In `tst_live_render_block_model.cpp`, add:
```cpp
void onD2Changed_populates_headingLevel() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("## Section");
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(binding.model()->data(binding.model()->index(0), LiveBlockModel::HeadingLevelRole).toInt(), 2);
}
void onD2Changed_populates_codeLanguage() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("```rust\nlet x = 1;\n```");
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(binding.model()->data(binding.model()->index(0), LiveBlockModel::CodeLanguageRole).toString(),
             QStringLiteral("rust"));
}
```

- [ ] **Step 2: Run to verify fails**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8 && \
ctest --test-dir build-dev -R tst_live_render_block_model --output-on-failure
```
Expected: new tests fail (headingLevel and codeLanguage are 0/empty).

- [ ] **Step 3: Update `LiveListModelBinding.cpp`**

Add includes:
```cpp
#include "KindTransition.h"
#include <markoff-foundation/AttrNames.h>
#include <markoff-foundation/BlockSerializerRegistry.h>
```

Update `Private` struct to add owned registry and pointer:
```cpp
struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document        = nullptr;
    LiveBlockModel            *model          = nullptr;
    BlockKindRegistry          ownedRegistry;   // fallback when doc has no registry
    const BlockKindRegistry   *registry       = nullptr;  // points to owned or cast from doc
    LiveCursorState           *cursorState    = nullptr;
    BlockHitTester            *hitTester      = nullptr;
    LiveSelectionView         *selectionView  = nullptr;
    LiveStructuralKeyHandler  *structuralKeys = nullptr;
    QList<BlockKey>            lastKeys;
    bool                       applyingModelUpdate = false;
};
```

In constructor, set `d->registry = &d->ownedRegistry;`.

In `setDocument`, after `d->document = doc; if (d->document) {`, add:
```cpp
if (auto *extReg = qobject_cast<const BlockKindRegistry *>(
        reinterpret_cast<const QObject *>(doc->serializerRegistry()))) {
    d->registry = extReg;
} else {
    d->registry = &d->ownedRegistry;
}
```
(Note: the cast via QObject is awkward — since `BlockKindRegistry` doesn't inherit QObject, use a `dynamic_cast` instead. `BlockKindRegistry` inherits `Markoff::BlockSerializerRegistry`. Cast the abstract pointer to `BlockKindRegistry *` with `dynamic_cast<const BlockKindRegistry *>(doc->serializerRegistry())`.)

Update `onD2Changed` body to populate all fields:
```cpp
void LiveListModelBinding::onD2Changed()
{
    auto *doc = d->document;
    if (!doc) return;

    const auto blockIds = doc->iterateBlocks();
    QList<BlockRecord> records;
    records.reserve(static_cast<int>(blockIds.size()));

    for (const auto &id : blockIds) {
        BlockRecord r;
        r.blockAnchor = id;
        r.kind = blockKindToString(doc->blockKind(id));

        QByteArray raw = doc->blockText(id);
        if (raw.endsWith('\n')) raw.chop(1);
        r.text = QString::fromUtf8(raw);

        // Populate kind-specific extras
        r.inlineSpans  = doc->inlineSpansFor(id);
        const auto attrs = doc->blockAttrs(id);
        r.attrs = attrs;

        if (r.kind == BlockKind::Heading) {
            auto it = attrs.find(Markoff::AttrNames::Level);
            if (it != attrs.end())
                r.headingLevel = std::get_if<int>(&it.value()) ? std::get<int>(it.value()) : 0;
        } else if (r.kind == BlockKind::CodeBlock) {
            auto it = attrs.find(Markoff::AttrNames::InfoString);
            if (it != attrs.end())
                r.codeLanguage = std::get_if<QString>(&it.value())
                                 ? std::get<QString>(it.value()) : QString{};
        }

        records.append(r);
    }

    QList<BlockKey> nextKeys;
    nextKeys.reserve(records.size());
    for (const auto &r : records)
        nextKeys.append(BlockKey{ r.kind, r.blockAnchor });

    const QList<AstBlockDiff::Op> ops = AstBlockDiff::diff(d->lastKeys, nextKeys);

    // Kind-transition detection: check Equal-op blocks for kind mismatch.
    // Do this BEFORE applyOps so model update and any re-triggered onD2Changed
    // see the corrected kind on the next spin.
    for (int i = 0; i < ops.size(); ++i) {
        if (!std::holds_alternative<AstBlockDiff::Equal>(ops[i])) continue;
        const int row = std::get<AstBlockDiff::Equal>(ops[i]).row;
        if (row >= records.size()) continue;
        const auto &rec = records[row];
        const BlockId id = BlockId(rec.blockAnchor);

        bool displayMode = false;
        const QString inferred = inferBlockKind(rec.text, &displayMode);
        const QString stored   = rec.kind;  // already converted from foundation BlockKind

        if (inferred != stored) {
            // Map inferred string back to foundation BlockKind
            Markoff::BlockKind fk = Markoff::BlockKind::Paragraph;
            if      (inferred == BlockKind::Heading)        fk = Markoff::BlockKind::Heading;
            else if (inferred == BlockKind::CodeBlock)      fk = Markoff::BlockKind::CodeBlock;
            else if (inferred == BlockKind::HorizontalRule) fk = Markoff::BlockKind::HorizontalRule;
            else if (inferred == BlockKind::Image)          fk = Markoff::BlockKind::Image;
            else if (inferred == BlockKind::Math)           fk = Markoff::BlockKind::Math;
            else if (inferred == BlockKind::ListItem)       fk = Markoff::BlockKind::ListItem;
            else if (inferred == BlockKind::Blockquote)     fk = Markoff::BlockKind::Blockquote;

            QList<QByteArray> attrNames;
            QList<Markoff::AttrValue> attrVals;
            if (fk == Markoff::BlockKind::Heading) {
                attrNames << Markoff::AttrNames::Level;
                attrVals  << int(countLeadingHashes(rec.text));
            } else if (fk == Markoff::BlockKind::Math) {
                attrNames << Markoff::AttrNames::DisplayMode;
                attrVals  << displayMode;
            }
            Cmd::changeKind(*doc, id, fk, attrNames, attrVals);
            // changeKind schedules another d2DocumentChanged; return here to
            // let that spin re-run onD2Changed with the corrected kind.
            return;
        }

        // Heading level update (kind still Heading but level changed)
        if (stored == BlockKind::Heading) {
            const int newLevel = countLeadingHashes(rec.text);
            const int oldLevel = rec.headingLevel;
            if (newLevel > 0 && newLevel != oldLevel) {
                Cmd::changeKind(*doc, id, Markoff::BlockKind::Heading,
                                {Markoff::AttrNames::Level}, {newLevel});
                return;
            }
        }
    }

    d->applyingModelUpdate = true;
    auto _ = qScopeGuard([this]{ d->applyingModelUpdate = false; });
    d->model->applyOps(ops, records);
    d->lastKeys = std::move(nextKeys);

    // Structural signals emitted after applyOps (Task 10 adds these)
}
```

Add missing include for `Cmd`:
```cpp
#include <markoff-foundation/Cmd/D2.h>
```

- [ ] **Step 4: Run tests**

```bash
cmake --build build-dev --target tst_live_render_block_model -j 8 && \
ctest --test-dir build-dev -R tst_live_render_block_model --output-on-failure
```
Expected: all pass including new heading-level and code-language tests.

- [ ] **Step 5: Run full live-render test suite**

```bash
cmake --build build-dev --target markoff_live_render -j 8 && \
ctest --test-dir build-dev -R "^tst_live_render" --output-on-failure
```
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add libs/markoff-live-render/src/LiveListModelBinding.cpp \
        libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h
git commit -m "feat(live-render): onD2Changed populates inline spans, attrs, headingLevel, codeLanguage; kind-transition detection

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Phase 3 — Cursor delivery redesign

### Task 10: Add structural signals to `LiveListModelBinding`; emit from `onD2Changed`

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h`
- Modify: `libs/markoff-live-render/src/LiveListModelBinding.cpp`

- [ ] **Step 1: Write failing test**

In `tst_live_render_cursor.cpp`, add:
```cpp
void structural_rows_inserted_emitted_on_new_block() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("hello");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    QSignalSpy spy(&binding, &LiveListModelBinding::structuralRowsInserted);

    auto ids = doc.iterateBlocks();
    auto t = doc.d2UndoLog().beginTransaction();
    doc.d2InsertBlock(ids[0], Markoff::BlockKind::Paragraph, t);
    t.commit();

    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toInt(), 1);  // inserted at row 1
    QCOMPARE(spy[0][1].toInt(), 1);
}
void structural_row_removed_emitted_on_block_removal() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("a\n\nb");
    QTRY_COMPARE(binding.model()->rowCount(), 2);

    QSignalSpy spy(&binding, &LiveListModelBinding::structuralRowRemoved);

    auto ids = doc.iterateBlocks();
    auto t = doc.d2UndoLog().beginTransaction();
    doc.d2RemoveBlock(ids[1], t);
    t.commit();

    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy[0][0].toInt(), 1);
}
```

- [ ] **Step 2: Add signals to header**

```cpp
// In LiveListModelBinding.h Q_SIGNALS section, add:
Q_SIGNAL void structuralRowsInserted(int first, int last);
Q_SIGNAL void structuralRowRemoved(int row);
```

- [ ] **Step 3: Emit signals in `onD2Changed`**

After `d->model->applyOps(ops, records);` and `d->lastKeys = std::move(nextKeys);`, add:

```cpp
// Emit structural signals derived from the diff ops.
// These replace the noteParseArrived cursor-resolution path.
for (const auto &op : ops) {
    if (auto *ins = std::get_if<AstBlockDiff::Insert>(&op)) {
        Q_EMIT structuralRowsInserted(ins->row, ins->row);
    } else if (auto *del = std::get_if<AstBlockDiff::Delete>(&op)) {
        Q_EMIT structuralRowRemoved(del->row);
    }
}
// Remove noteParseArrived call (Task 11 deletes the method entirely).
```

Also remove the existing `d->cursorState->noteParseArrived(...)` call.

- [ ] **Step 4: Run tests**

```bash
cmake --build build-dev --target tst_live_render_cursor -j 8 && \
ctest --test-dir build-dev -R tst_live_render_cursor --output-on-failure
```
Expected: all pass including new structural signal tests.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/LiveListModelBinding.h \
        libs/markoff-live-render/src/LiveListModelBinding.cpp
git commit -m "feat(live-render): LiveListModelBinding emits structuralRowsInserted/Removed from onD2Changed

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 11: Rewire `LiveCursorState` — drop parse-cycle path; subscribe to structural signals

This is the largest cursor change. Removes `setSignalModel`, `noteParseArrived`, `requestTextCaretAtByte`, `parseCyclesSeen`, and the `onRowsInserted` handler. Adds `onStructuralRowsInserted` and `onStructuralRowRemoved` handlers.

**Files:**
- Modify: `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h`
- Modify: `libs/markoff-live-render/src/LiveCursorState.cpp`
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp` (remove `requestTextCaretAtByte` call sites)

- [ ] **Step 1: Check all callers of removed methods**

```bash
grep -rn "noteParseArrived\|requestTextCaretAtByte\|setSignalModel\|parseCyclesSeen" \
  libs/markoff-live-render/
```
Note every file and line number.

- [ ] **Step 2: Update `LiveCursorState.h`**

Remove:
- `void setSignalModel(QAbstractItemModel *signalModel);`
- `void noteParseArrived(quint64 parseSeq);`
- `void requestTextCaretAtByte(Markoff::MarkoffDocument *, quint32, int);`
- `m_signalModel` member
- `PendingRow::parseCyclesSeen` field
- `PendingRow::byteDocument` and `PendingRow::targetByte` fields
- `onRowsInserted` private slot
- `resolvePendingForByte` private slot

Add:
```cpp
// Constructor gains binding pointer for structural signal subscription.
explicit LiveCursorState(const BlockKindRegistry *registry,
                         const LiveBlockModel    *model,
                         LiveListModelBinding    *binding,  // NEW
                         QObject                 *parent = nullptr);
```

Add private slots:
```cpp
void onStructuralRowsInserted(int first, int last);
void onStructuralRowRemoved(int row);
```

Forward-declare `LiveListModelBinding` at top of header.

- [ ] **Step 3: Update `LiveCursorState.cpp`**

Update constructor to accept and connect `binding`:
```cpp
LiveCursorState::LiveCursorState(const BlockKindRegistry *registry,
                                  const LiveBlockModel    *model,
                                  LiveListModelBinding    *binding,
                                  QObject                 *parent)
    : QObject(parent), m_registry(registry), m_model(model)
{
    if (binding) {
        connect(binding, &LiveListModelBinding::structuralRowsInserted,
                this, &LiveCursorState::onStructuralRowsInserted);
        connect(binding, &LiveListModelBinding::structuralRowRemoved,
                this, &LiveCursorState::onStructuralRowRemoved);
    }
}
```

Add `onStructuralRowsInserted`:
```cpp
void LiveCursorState::onStructuralRowsInserted(int first, int last)
{
    if (!m_pendingRow) return;
    const int row = m_pendingRow->row;
    if (row >= first && row <= last + 1) {
        // Row-keyed resolution
        if (row < m_model->rowCount())
            resolvePendingForRow(row);
        return;
    }
    // Anchor-keyed resolution: search all new rows for the wanted anchor
    if (m_pendingRow->anchor) {
        resolvePendingForAnchor();
    }
}
```

Add `onStructuralRowRemoved`:
```cpp
void LiveCursorState::onStructuralRowRemoved(int /*row*/)
{
    // A merge removed a row. The cursor should be in the surviving block.
    // If we have an anchor-keyed pending, try to resolve it now.
    if (m_pendingRow && m_pendingRow->anchor)
        resolvePendingForAnchor();
}
```

Remove: `setSignalModel`, `noteParseArrived`, `requestTextCaretAtByte`, `resolvePendingForByte`, `onRowsInserted` implementations.

Update `PendingRow` struct to remove `parseCyclesSeen`, `byteDocument`, `targetByte`.

- [ ] **Step 4: Update `LiveListModelBinding.cpp` constructor**

Pass `this` as binding to `LiveCursorState`:
```cpp
d->cursorState = new LiveCursorState(&d->ownedRegistry, d->model, this, this);
```

- [ ] **Step 5: Update `LiveStructuralKeyHandler.cpp`**

Find all `requestTextCaretAtByte` calls and replace with `requestTextCaretAtAnchor` using the known BlockAnchor (the anchor of the surviving/new block, which is already available at the call site).

```bash
grep -n "requestTextCaretAtByte" libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp
```
For each, replace with:
```cpp
d->cursorState->requestTextCaretAtAnchor(survivingBlockAnchor, qtPos);
```

- [ ] **Step 6: Run full test suite**

```bash
cmake --build build-dev --target markoff_live_render -j 8 && \
ctest --test-dir build-dev -R "^tst_live_render" --output-on-failure
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h \
        libs/markoff-live-render/src/LiveCursorState.cpp \
        libs/markoff-live-render/src/LiveListModelBinding.cpp \
        libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp
git commit -m "refactor(live-render): LiveCursorState subscribes to structural signals; retire parse-cycle cursor path

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 12: QML cursor-reset fix in all text-bearing delegates

When `text` changes on a QML `TextEdit`, Qt resets `cursorPosition` to 0. Add `onTextChanged` to re-assert the cursor position.

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml`
- Modify: `libs/markoff-live-render/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml`

- [ ] **Step 1: Update ParagraphDelegate.qml**

Inside the `TextEdit { }` block, after the existing `Connections { target: root.selectionView ... }`, add:

```qml
// Re-assert cursor position after text updates (Qt resets to 0 on text change).
onTextChanged: {
    const cs = root.liveBinding ? root.liveBinding.cursorState : null
    if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
        cursorPosition = cs.focusedQtPos
}

Connections {
    target: root.liveBinding ? root.liveBinding.cursorState : null
    function onCursorChanged() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
            edit.cursorPosition = cs.focusedQtPos
    }
}
```

- [ ] **Step 2: Apply same fix to HeadingDelegate.qml and CodeBlockDelegate.qml**

Identical addition in the TextEdit block of each delegate.

- [ ] **Step 3: Test manually**

Start the test app:
```bash
./build-dev/bin/markoff-live-render-app
```
Type to create a multi-block document. Use Backspace-merge to merge two blocks. Verify cursor lands at the correct position after merge, not at position 0.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/ParagraphDelegate.qml \
        libs/markoff-live-render/qml/delegates/HeadingDelegate.qml \
        libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml
git commit -m "fix(live-render): re-assert cursor position after text change in text-bearing delegates

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Phase 4 — L6 structural key extensions

### Task 13: Heading — level-change gesture (Cmd+Shift+1–6, Cmd+Shift+0)

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/HeadingDelegate.qml`
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp` (or handle in QML directly)

The level-change gesture is view-side only (calls `Cmd::changeKind`). It can live in the QML delegate's `Keys.onPressed` handler since it doesn't need the structural key handler's routing logic.

- [ ] **Step 1: Add consumed key registration for Heading in BlockKindRegistry**

In `BlockKindRegistry.cpp`, update the Heading descriptor:
```cpp
d.consumedStructuralKeys = {
    Qt::Key_Return, Qt::Key_Enter,
    Qt::Key_Backspace, Qt::Key_Delete,
    // Level-change: handled in QML via Ctrl+Shift+0-6
};
```
(The level-change keys don't go through `LiveStructuralKeyHandler::tryHandle` — they're intercepted by the QML delegate directly.)

- [ ] **Step 2: Update HeadingDelegate.qml**

In `Keys.onPressed`, extend the key-forward condition to handle level-change keys:

```qml
Keys.priority: Keys.BeforeItem
Keys.onPressed: (event) => {
    const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
    const k = event.key
    const mods = event.modifiers

    // Level-change: Ctrl+Shift+1–6 or Ctrl+Shift+0
    if ((mods & Qt.ControlModifier) && (mods & Qt.ShiftModifier)) {
        const doc = root.liveBinding ? root.liveBinding.document : null
        if (doc && k >= Qt.Key_0 && k <= Qt.Key_6) {
            const level = k - Qt.Key_0
            if (level === 0) {
                // Demote to paragraph
                doc.applyStructural({
                    kind: "changeKind",
                    blockAnchor: model.blockAnchor,
                    newKind: "paragraph",
                    attrs: {}
                })
            } else {
                doc.applyStructural({
                    kind: "changeKind",
                    blockAnchor: model.blockAnchor,
                    newKind: "heading",
                    attrs: { level: level }
                })
            }
            event.accepted = true
            return
        }
    }

    if (!handler) { event.accepted = false; return }
    if (k !== Qt.Key_Return && k !== Qt.Key_Enter
            && k !== Qt.Key_Backspace && k !== Qt.Key_Delete)
        return
    const handled = handler.tryHandle(k, mods, root.modelIndex,
        edit.cursorPosition, edit.selectionStart === edit.selectionEnd, model.text)
    event.accepted = handled
}
```

Note: `doc.applyStructural(...)` is not the right call here — `applyStructural` takes a `Markoff::StructuralOp` C++ type, not a JS object. Instead, expose a Q_INVOKABLE or use `Cmd::changeKind` via the structural key handler.

**Revised approach**: Add `Q_INVOKABLE void changeBlockKind(Markoff::BlockAnchor anchor, const QString &newKind, int level)` to `LiveStructuralKeyHandler` (or add a dedicated `setHeadingLevel` Q_INVOKABLE). Expose via the binding.

Simpler: in `LiveStructuralKeyHandler`, intercept `Ctrl+Shift+1-6` inside `tryHandle` by adding them to the heading's consumed keys and checking modifiers:

```cpp
// In tryHandle, when kind == "heading", add:
if ((modifiers & Qt::ControlModifier) && (modifiers & Qt::ShiftModifier)) {
    if (key >= Qt::Key_0 && key <= Qt::Key_6) {
        int level = key - Qt::Key_0;
        if (level == 0) {
            Cmd::changeKind(*d->document, id, Markoff::BlockKind::Paragraph, {}, {});
        } else {
            Cmd::changeKind(*d->document, id, Markoff::BlockKind::Heading,
                           {Markoff::AttrNames::Level}, {level});
        }
        return true;
    }
}
```

Update HeadingDelegate QML to forward Ctrl+Shift+0-6:
```qml
if (k !== Qt.Key_Return && k !== Qt.Key_Enter
        && k !== Qt.Key_Backspace && k !== Qt.Key_Delete
        && !((mods & Qt.ControlModifier) && (mods & Qt.ShiftModifier)
             && k >= Qt.Key_0 && k <= Qt.Key_6))
    return
```

Add `Qt::Key_0` through `Qt::Key_6` with modifier conditions to consumed keys in the descriptor (not `consumedStructuralKeys` since those are just Qt::Key values; the modifiers are checked in `tryHandle`).

- [ ] **Step 3: Write test**

In `tst_live_render_structural.cpp`:
```cpp
void heading_level_change_via_ctrl_shift_1() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("## Hello");
    QTRY_COMPARE(binding.model()->rowCount(), 1);
    QCOMPARE(binding.model()->data(binding.model()->index(0),
             LiveBlockModel::HeadingLevelRole).toInt(), 2);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_1, Qt::ControlModifier | Qt::ShiftModifier,
        0, 0, true, "## Hello");

    QTRY_COMPARE(binding.model()->data(binding.model()->index(0),
                 LiveBlockModel::HeadingLevelRole).toInt(), 1);
}
```

- [ ] **Step 4: Run test**

```bash
cmake --build build-dev --target tst_live_render_structural -j 8 && \
ctest --test-dir build-dev -R tst_live_render_structural --output-on-failure
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/HeadingDelegate.qml \
        libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp
git commit -m "feat(live-render): heading level-change via Ctrl+Shift+1-6/0 in structural key handler

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 14: CodeBlock — Tab inserts 4 spaces; language-tag editing

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml`
- Modify: `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`

- [ ] **Step 1: Add Tab to CodeBlock consumed keys**

In `BlockKindRegistry.cpp`, update CodeBlock descriptor:
```cpp
d.consumedStructuralKeys = {
    Qt::Key_Backspace, Qt::Key_Delete,
    Qt::Key_Tab,
};
```

- [ ] **Step 2: Handle Tab in `tryHandle`**

In `LiveStructuralKeyHandler.cpp`, find the CodeBlock handlers section and add:
```cpp
if (key == Qt::Key_Tab && kind == BlockKind::CodeBlock) {
    // Insert 4 spaces at current cursor position
    const QByteArray spaces("    ");
    auto t = d->document->d2UndoLog().beginTransaction();
    const auto ids = d->document->iterateBlocks();
    if (blockIndex < static_cast<int>(ids.size())) {
        d->document->d2ApplyBufferEdit(
            ids[blockIndex],
            static_cast<uint32_t>(qtPos),
            0,
            spaces,
            t);
        t.commit();
        d->cursorState->requestTextCaretAtRow(blockIndex, qtPos + 4);
    }
    return true;
}
```

- [ ] **Step 3: Language-tag editing in QML**

In `CodeBlockDelegate.qml`, the current delegate has the monospace TextEdit and KF6 highlighter. Add a language tag label that becomes editable on click:

```qml
// Add near the top of the Item body:
Row {
    id: langTagRow
    anchors { top: parent.top; right: parent.right }
    padding: 4

    property bool editing: false

    Text {
        visible: !langTagRow.editing && model.codeLanguage !== ""
        text: model.codeLanguage
        font.pixelSize: 11
        color: palette.mid
        MouseArea {
            anchors.fill: parent
            onClicked: langTagRow.editing = true
        }
    }
    TextInput {
        id: langInput
        visible: langTagRow.editing
        text: model.codeLanguage
        font.pixelSize: 11
        color: palette.text
        onActiveFocusChanged: if (!activeFocus) langTagRow.editing = false
        Keys.onReturnPressed: {
            const doc = root.liveBinding ? root.liveBinding.document : null
            if (doc) {
                // Cmd::changeKind to update infoString attr
                // Exposed via structuralKeyHandler.changeCodeLanguage(blockAnchor, lang)
                const handler = root.liveBinding.structuralKeyHandler
                if (handler) handler.changeCodeLanguage(model.blockAnchor, text)
            }
            langTagRow.editing = false
        }
        Keys.onEscapePressed: langTagRow.editing = false
    }
}
```

Add `Q_INVOKABLE void changeCodeLanguage(Markoff::BlockAnchor anchor, const QString &lang)` to `LiveStructuralKeyHandler`:
```cpp
void LiveStructuralKeyHandler::changeCodeLanguage(BlockAnchor anchor, const QString &lang)
{
    BlockId id(anchor);
    Cmd::changeKind(*d->document, id, Markoff::BlockKind::CodeBlock,
                    {Markoff::AttrNames::InfoString}, {lang});
}
```

- [ ] **Step 4: Test Tab key**

```cpp
void codeblock_tab_inserts_4_spaces() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("```\nhello\n```");
    QTRY_COMPARE(binding.model()->rowCount(), 1);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Tab, Qt::NoModifier, 0, 3, true, "```\nhello\n```");

    QTRY_VERIFY(QString::fromUtf8(doc.blockText(doc.iterateBlocks()[0]))
                .contains(QStringLiteral("    ")));
}
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/CodeBlockDelegate.qml \
        libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/include/markoff/live-render/LiveStructuralKeyHandler.h
git commit -m "feat(live-render): CodeBlock Tab inserts 4 spaces; language-tag inline editing

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 15: `HorizontalRuleDelegate.qml` — BlockSelected focus + structural key routing

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/HorizontalRuleDelegate.qml`

- [ ] **Step 1: Read current HrDelegate**

```bash
cat libs/markoff-live-render/qml/delegates/HorizontalRuleDelegate.qml
```

- [ ] **Step 2: Rewrite HrDelegate**

Replace with:
```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: 20

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var cursorState: liveBinding ? liveBinding.cursorState : null

    // The rule line
    Rectangle {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 2
        color: root.isSelected ? palette.highlight : palette.mid
        radius: 1
    }

    readonly property bool isSelected:
        cursorState !== null
        && cursorState.cursorKind === "BlockSelected"
        && cursorState.focusedAnchorRow === root.modelIndex

    // Focus ring
    Rectangle {
        visible: root.isSelected
        anchors.fill: parent
        anchors.margins: -2
        border.color: palette.highlight
        border.width: 2
        color: "transparent"
        radius: 3
    }

    function positionAt(x, y) { return -1 }

    function focusEditAt(qtPos) {
        // HR has no TextEdit; set BlockSelected cursor via cursorState
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (!cs) return
        root.forceActiveFocus()
        cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (!root.isSelected) { event.accepted = false; return }
        const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
        if (!handler) { event.accepted = false; return }
        const k = event.key
        if (k !== Qt.Key_Delete && k !== Qt.Key_Backspace
                && k !== Qt.Key_Up && k !== Qt.Key_Down) {
            event.accepted = false; return
        }
        const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
            -1, true, model.text)
        event.accepted = handled
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(-1) })
    }
}
```

- [ ] **Step 3: Add Up/Down navigation to `LiveStructuralKeyHandler` for HR**

In `tryHandle`, add:
```cpp
if (kind == BlockKind::HorizontalRule) {
    if (key == Qt::Key_Up && blockIndex > 0) {
        d->cursorState->requestTextCaretAtRow(blockIndex - 1,
            d->model->recordAt(blockIndex - 1).text.length());
        return true;
    }
    if (key == Qt::Key_Down && blockIndex < d->model->rowCount() - 1) {
        d->cursorState->requestTextCaretAtRow(blockIndex + 1, 0);
        return true;
    }
    if (key == Qt::Key_Backspace || key == Qt::Key_Delete) {
        // Remove the HR block
        auto t = d->document->d2UndoLog().beginTransaction();
        d->document->d2RemoveBlock(ids[blockIndex], t);
        t.commit();
        // Cursor to previous block (or next if first)
        const int targetRow = std::max(0, blockIndex - 1);
        d->cursorState->requestTextCaretAtRow(targetRow,
            d->model->rowCount() > 0 ?
                d->model->recordAt(std::min(targetRow, d->model->rowCount()-1)).text.length() : 0);
        return true;
    }
}
```

- [ ] **Step 4: Test (manual + structural test)**

```cpp
void hr_delete_removes_block() {
    Markoff::MarkoffDocument doc(1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.loadFromMarkdown("hello\n\n---\n\nworld");
    QTRY_COMPARE(binding.model()->rowCount(), 3);

    binding.structuralKeyHandler()->tryHandle(
        Qt::Key_Delete, Qt::NoModifier, 1, -1, true, "---");

    QTRY_COMPARE(binding.model()->rowCount(), 2);
}
```

- [ ] **Step 5: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/HorizontalRuleDelegate.qml \
        libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp
git commit -m "feat(live-render): HorizontalRuleDelegate BlockSelected focus + Up/Down/Delete navigation

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

### Task 16: `ImageDelegate.qml` — BlockSelected + BlockInternalEdit alt-edit rebuild

**Files:**
- Modify: `libs/markoff-live-render/qml/delegates/ImageDelegate.qml`

- [ ] **Step 1: Rewrite ImageDelegate**

```qml
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: imageArea.implicitHeight + 8

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var cursorState: liveBinding ? liveBinding.cursorState : null

    readonly property bool isSelected:
        cursorState !== null
        && cursorState.cursorKind === "BlockSelected"
        && cursorState.focusedAnchorRow === root.modelIndex

    readonly property bool isAltEditing:
        cursorState !== null
        && cursorState.cursorKind === "BlockInternalEdit"
        && cursorState.focusedAnchorRow === root.modelIndex

    readonly property string imgSrc: {
        const a = model.blockAttrs
        return a ? (a["src"] || "") : ""
    }
    readonly property string imgAlt: {
        const a = model.blockAttrs
        return a ? (a["alt"] || "") : ""
    }

    Item {
        id: imageArea
        width: parent.width
        implicitHeight: imgDisplay.implicitHeight + altRow.implicitHeight + 8

        Image {
            id: imgDisplay
            source: root.imgSrc
            width: parent.width - 16
            anchors.horizontalCenter: parent.horizontalCenter
            fillMode: Image.PreserveAspectFit
            visible: !root.isAltEditing
        }

        // Placeholder when src is empty or image fails to load
        Rectangle {
            visible: imgDisplay.status !== Image.Ready && !root.isAltEditing
            width: parent.width - 16
            height: 80
            anchors.horizontalCenter: parent.horizontalCenter
            color: palette.alternateBase
            border.color: palette.mid
            Text {
                anchors.centerIn: parent
                text: root.imgSrc === "" ? "[image: no src]" : "[image: " + root.imgSrc + "]"
                color: palette.mid
            }
        }

        // Alt text (read mode)
        Text {
            id: altRow
            anchors { top: imgDisplay.bottom; left: parent.left; right: parent.right }
            anchors.margins: 8
            text: root.imgAlt
            color: palette.mid
            font.italic: true
            font.pixelSize: 12
            visible: !root.isAltEditing && root.imgAlt !== ""
        }

        // Alt text editor (edit mode)
        TextInput {
            id: altInput
            visible: root.isAltEditing
            anchors { top: imgDisplay.bottom; left: parent.left; right: parent.right }
            anchors.margins: 8
            text: root.imgAlt
            font.pixelSize: 12
            placeholderText: "Alt text…"

            Keys.onReturnPressed: {
                const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
                if (handler) handler.changeImageAlt(model.blockAnchor, text)
                root.exitAltEdit()
            }
            Keys.onEscapePressed: root.exitAltEdit()
        }
    }

    // Focus ring
    Rectangle {
        visible: root.isSelected || root.isAltEditing
        anchors.fill: parent
        anchors.margins: -2
        border.color: palette.highlight
        border.width: 2
        color: "transparent"
        radius: 3
    }

    function positionAt(x, y) { return -1 }

    function focusEditAt(qtPos) {
        root.forceActiveFocus()
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    function enterAltEdit() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockInternalEdit",
                              block: model.blockAnchor, mode: "alt-edit" })
        altInput.forceActiveFocus()
    }

    function exitAltEdit() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (root.isSelected && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            root.enterAltEdit()
            event.accepted = true
            return
        }
        if (root.isSelected && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (handler) {
                event.accepted = handler.tryHandle(event.key, event.modifiers,
                    root.modelIndex, -1, true, model.text)
            }
            return
        }
        event.accepted = false
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: if (root.isSelected) root.enterAltEdit()
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(-1) })
    }
}
```

Add `Q_INVOKABLE void changeImageAlt(Markoff::BlockAnchor anchor, const QString &alt)` to `LiveStructuralKeyHandler`:
```cpp
void LiveStructuralKeyHandler::changeImageAlt(BlockAnchor anchor, const QString &alt)
{
    BlockId id(anchor);
    Cmd::changeKind(*d->document, id, Markoff::BlockKind::Image,
                    {Markoff::AttrNames::Alt}, {alt});
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build-dev --target markoff_live_render -j 8
```
Expected: clean build.

- [ ] **Step 3: Test manually**

```bash
./build-dev/bin/markoff-live-render-app
```
Load a file with `![alt text](url)`. Verify image renders (or placeholder). Click image → BlockSelected ring. Press Enter → alt edit mode. Type new alt → Enter → confirm kind stored.

- [ ] **Step 4: Commit**

```bash
git add libs/markoff-live-render/qml/delegates/ImageDelegate.qml \
        libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp \
        libs/markoff-live-render/include/markoff/live-render/LiveStructuralKeyHandler.h
git commit -m "feat(live-render): ImageDelegate full rebuild — BlockSelected + BlockInternalEdit alt-edit

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

*Part 1 ends here. Continue with Part 2 (`docs/plans/2026-05-05-d3-view-layer-adaptation-part2.md`) for:*
- *Phase 5 — L7: ListItemDelegate, BlockquoteDelegate, LiveView DelegateChoices*
- *Phase 6 — L8: MathRenderer, MathDelegate, BlockInternalEdit dispatch*
- *Phase 7 — Per-block undo UI (LiveContextMenu)*
- *Phase 8 — Tests (tst_live_render_kind_transition, tst_live_render_math, tst_live_render_context_menu, extend structural)*
- *Phase 9 — Integration build + dogfood*
