// libs/markoff/include/markoff/EditorSettings.h
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_EDITORSETTINGS_H
#define MARKOFF_EDITORSETTINGS_H

namespace Markoff {

struct EditorSettings {
    int tabSize = 4;
    bool lineNumbers = false;
    bool lineWrap = true;
    bool highlightCurrentLine = true;
    bool highlightingEnabled = true;
    bool tripleClickSelectsLine = true; // false = Qt default (paragraph)
};

} // namespace Markoff

#endif // MARKOFF_EDITORSETTINGS_H
