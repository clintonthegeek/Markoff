// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transplanted from Penelope's stylemanager at:
//   ~/dev/Penelope/src/style/stylemanager.{h,cpp}
// Penelope HEAD at transplant time: 6b9c32344032c9eb54c041970a5a3e2feff7caff
// Penelope is GPL-3.0 (see ~/dev/Penelope/COPYING).
// Adapted for Corbomite's libs/readingview/ — TableStyle and FootnoteStyle
// coupling dropped (out of scope for Phase 3a); ThemeManager coupling severed
// (never existed in Penelope's StyleManager, but the class was constructed
// alongside one — we construct stand-alone). Preset factory
// `makeObsidianDefault(Theme)` added so Phase 3a has a batteries-included
// style table. Namespace rebadged.
// (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#ifndef CORBOMITE_READINGVIEW_STYLEMANAGER_H
#define CORBOMITE_READINGVIEW_STYLEMANAGER_H

#include "corbomite/readingview/CodeBlockHighlighter.h"
#include "corbomite/readingview/styling/CharacterStyle.h"
#include "corbomite/readingview/styling/ParagraphStyle.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace Corbomite::ReadingView {

class StyleManager : public QObject
{
    Q_OBJECT

public:
    explicit StyleManager(QObject *parent = nullptr);

    void addParagraphStyle(const ParagraphStyle &style);
    void addCharacterStyle(const CharacterStyle &style);

    ParagraphStyle *paragraphStyle(const QString &name);
    CharacterStyle *characterStyle(const QString &name);

    const QHash<QString, ParagraphStyle> &paragraphStyles() const { return m_paraStyles; }
    const QHash<QString, CharacterStyle> &characterStyles() const { return m_charStyles; }

    QStringList paragraphStyleNames() const;
    QStringList characterStyleNames() const;

    // Resolve a style by walking its parent chain.
    // Returns a fully-resolved copy with all inherited properties filled in.
    ParagraphStyle resolvedParagraphStyle(const QString &name);
    CharacterStyle resolvedCharacterStyle(const QString &name);

    // Deep-copy this style manager
    StyleManager *clone(QObject *parent = nullptr) const;

    /// Populate this manager with Corbomite's default Obsidian-like style
    /// table: Body, Heading1..Heading6, CodeBlock, Blockquote, ListItem.
    /// Idempotent — re-calling overwrites the defaults but leaves any
    /// user-added styles intact.
    void populateObsidianDefaults(Theme theme);

    /// Convenience — new manager with defaults already populated.
    static StyleManager *makeObsidianDefault(Theme theme,
                                             QObject *parent = nullptr);

private:
    QHash<QString, ParagraphStyle> m_paraStyles;
    QHash<QString, CharacterStyle> m_charStyles;
};

} // namespace Corbomite::ReadingView

#endif // CORBOMITE_READINGVIEW_STYLEMANAGER_H
