// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/EditorBackend.h>

namespace Markoff::View::Qml {

EditorBackend::EditorBackend(QObject *parent) : QObject(parent) {}
EditorBackend::~EditorBackend() = default;

Markoff::MarkoffDocument *EditorBackend::document() const { return m_document; }

void EditorBackend::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_document == doc) return;
    m_document = doc;
    Q_EMIT documentChanged();
}

}  // namespace Markoff::View::Qml
