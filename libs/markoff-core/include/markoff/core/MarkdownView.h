// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QWidget>
#include <markoff/core/CursorPos.h>
#include <markoff/core/EditorContext.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/core/Theme.h>

namespace Markoff {
class FindController;
class MarkoffDocument;

class MARKOFF_CORE_EXPORT MarkdownView : public QWidget {
    Q_OBJECT
public:
    explicit MarkdownView(QWidget *parent = nullptr);
    ~MarkdownView() override;

    virtual void setDocument(MarkoffDocument *doc);
    virtual MarkoffDocument *document() const;

    virtual CursorPos cursorPosition() const;
    virtual void setCursorPosition(CursorPos);

    virtual float scrollPositionVisualLine() const;
    virtual void  setScrollPositionVisualLine(float);

    virtual void setReadOnly(bool ro);
    virtual bool isReadOnly() const;

    virtual bool hasCursor()  const { return false; }
    virtual bool hasEditing() const { return false; }

    /// Caret rectangle in THIS widget's coordinate system, or an invalid
    /// QRect when no caret is established (no document, no focus, cursor
    /// not in a text-bearing state). Consumers anchor transient UI
    /// (completion popups) at bottomLeft(). Contract-v2 extension
    /// (2026-06-11 caret-rect; driven by Corbomite completion revival).
    virtual QRect caretRect() const { return {}; }

    // --- Ephemeral (non-content) view state, as JSON (contract-v2 P3.6).
    //     Corbomite's `EphemeralState` round-trip (spec §5) persists this
    //     across a session/workspace save; the document's own content is
    //     never part of it. Schema is per-leaf (coordinate spaces differ —
    //     canvas's is block-index + byte, others may differ), but every
    //     leaf's top-level shape is the same three keys: "scroll", the
    //     leaf's scroll-position state; "cursors", a JSON ARRAY (multi-
    //     cursor readiness, F1a — `cursorPosition()` above stays the
    //     single primary caret; the array's first element is the leaf's
    //     restore target until a leaf grows real multi-cursor support);
    //     "folds", always empty until P5.6, key present so the schema
    //     doesn't need a migration when folding lands. Default: no state
    //     (empty object) / no-op restore — a leaf that hasn't implemented
    //     this yet round-trips as "nothing to persist, nothing to
    //     restore", never a crash on a foreign or partial blob. ---
    virtual QJsonObject saveEphemeralState() const { return {}; }
    virtual void restoreEphemeralState(const QJsonObject &state) { Q_UNUSED(state); }

    // --- Find (spec §3). Default: loud no-op. ---
    virtual void attachFindController(FindController *fc);
    virtual void detachFindController();

    // --- Undo/redo: base-implemented over undoD2; no-op while read-only. ---
    virtual void undo();
    virtual void redo();

    // --- Theme / font scale: base stores + signals; leaves override to
    //     apply (call the base first to keep the store coherent). ---
    virtual Theme theme() const;
    virtual void setTheme(const Theme &t);
    virtual qreal fontScale() const;
    virtual void  setFontScale(qreal s);

    // --- Format verbs. Default no-op; hasEditing() advertises support. ---
    virtual void toggleBold() {}
    virtual void toggleItalic() {}
    virtual void toggleStrikethrough() {}
    virtual void toggleInlineCode() {}
    virtual void insertLink() {}
    virtual void setHeadingLevel(int level) { Q_UNUSED(level); }

signals:
    void documentChanged(Markoff::MarkoffDocument *doc);
    void cursorPositionChanged(int line, int column);
    void scrollPositionChanged(float pos);
    void themeChanged();
    void fontScaleChanged(qreal scale);
    void contextChanged(const Markoff::EditorContext &ctx);

private:
    MarkoffDocument *m_document = nullptr;
    bool m_readOnly = false;
    Theme m_theme;
    qreal m_fontScale = 1.0;
};
} // namespace Markoff
