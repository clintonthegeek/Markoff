// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/CursorPosition.h>
#include <markoff/MarkoffDocument.h>

namespace Markoff {

CursorPosition::CursorPosition() = default;

CursorPosition::CursorPosition(MarkoffDocument *doc, quint64 handle)
    : m_doc(doc), m_handle(handle) {}

CursorPosition::CursorPosition(CursorPosition &&other) noexcept
    : m_doc(other.m_doc), m_handle(other.m_handle) {
    other.m_doc = nullptr;
    other.m_handle = 0;
}

CursorPosition &CursorPosition::operator=(CursorPosition &&other) noexcept {
    if (this != &other) {
        release();
        m_doc = other.m_doc;
        m_handle = other.m_handle;
        other.m_doc = nullptr;
        other.m_handle = 0;
    }
    return *this;
}

CursorPosition::~CursorPosition() {
    release();
}

bool CursorPosition::isValid() const {
    return m_doc != nullptr && m_handle != 0;
}

void CursorPosition::release() {
    // Phase C3 Task 6 fills in the release path once
    // MarkoffDocument::releaseAnchorHandle exists:
    //     if (isValid()) m_doc->releaseAnchorHandle(m_handle);
    // Until then, the m_doc == nullptr guard prevents a null-deref
    // and the handle leak is bounded by the life of the MarkoffDocument
    // (which owns the CanonicalBuffer that owns the anchor).
    m_doc = nullptr;
    m_handle = 0;
}

} // namespace Markoff
