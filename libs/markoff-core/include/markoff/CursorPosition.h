// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>
#include <markoff/MarkoffCoreExport.h>

namespace Markoff {

class MarkoffDocument;

class MARKOFF_CORE_EXPORT CursorPosition {
public:
    CursorPosition();
    CursorPosition(const CursorPosition &) = delete;
    CursorPosition &operator=(const CursorPosition &) = delete;
    CursorPosition(CursorPosition &&other) noexcept;
    CursorPosition &operator=(CursorPosition &&other) noexcept;
    ~CursorPosition();

    bool isValid() const;

private:
    friend class MarkoffDocument;
    CursorPosition(MarkoffDocument *doc, quint64 handle);
    void release();

    MarkoffDocument *m_doc = nullptr;
    quint64          m_handle = 0;
};

} // namespace Markoff
