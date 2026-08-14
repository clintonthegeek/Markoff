// SPDX-License-Identifier: GPL-3.0-or-later
#include "CalloutBlocks.h"

namespace Markoff::Canvas::Detail {

namespace {

struct CalloutType {
    const char *key;
    Theme::Slot slot;
    const char *label;
    const char *icon;
};

// The five types with a dedicated Theme::Slot (Theme.h). Obsidian itself
// recognizes more synonyms (danger, bug, success, ...) — out of scope; the
// plan names "note/warning/tip" as the minimum, and important/caution ride
// along for free since their slots already existed unused (Theme.h).
constexpr CalloutType kTypes[] = {
    {"note",      Theme::Slot::CalloutNote,      "Note",      "ℹ"},        // ℹ
    {"tip",       Theme::Slot::CalloutTip,       "Tip",       "\U0001F4A1"},    // 💡
    {"warning",   Theme::Slot::CalloutWarning,   "Warning",   "⚠"},        // ⚠
    {"important", Theme::Slot::CalloutImportant, "Important", "❗"},        // ❗
    {"caution",   Theme::Slot::CalloutCaution,   "Caution",   "⚠"},        // ⚠
};

}  // namespace

CalloutInfo parseCallout(const QString &blockText)
{
    CalloutInfo info;

    const QString text = blockText.trimmed();
    if (!text.startsWith(QStringLiteral("[!")))
        return info;

    const int close = text.indexOf(QLatin1Char(']'));
    if (close < 2)
        return info;

    const QString rawType = text.mid(2, close - 2);
    if (rawType.isEmpty())
        return info;
    const QString typeKey = rawType.toLower();

    info.isCallout = true;
    info.typeKey   = typeKey;

    for (const CalloutType &t : kTypes) {
        if (typeKey == QLatin1String(t.key)) {
            info.slot  = t.slot;
            info.label = QString::fromUtf8(t.label);
            info.icon  = QString::fromUtf8(t.icon);
            return info;
        }
    }

    // Unrecognized bracket type: still a callout (isCallout stays true),
    // default CalloutNote styling, label falls back to the raw type text
    // capitalized.
    info.label = rawType.isEmpty() ? QStringLiteral("Note")
                                    : (rawType.left(1).toUpper() + rawType.mid(1).toLower());
    info.icon  = QStringLiteral("ℹ");
    return info;
}

}  // namespace Markoff::Canvas::Detail
