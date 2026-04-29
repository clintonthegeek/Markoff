// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQuickTextDocument>
#include <QtQmlIntegration>

#include <markoff-foundation/SourceTextDocumentBinding.h>

namespace Markoff::View::Qml {

/// Extension class that adds QML-callable convenience methods to
/// `Markoff::SourceTextDocumentBinding`. Lives in the QML library so the
/// foundation header stays QML-free (no `QQuickTextDocument` dependency).
class SourceTextDocumentBindingExtension : public QObject {
    Q_OBJECT
public:
    explicit SourceTextDocumentBindingExtension(QObject *parent = nullptr)
        : QObject(parent)
    {}

    /// QML-friendly setter that accepts a `QQuickTextDocument *` (the type of
    /// `TextArea.textDocument`) and forwards the underlying `QTextDocument *`
    /// to the foundation binding.
    Q_INVOKABLE void setQtQuickDocument(QQuickTextDocument *qqtd) {
        auto *binding = qobject_cast<Markoff::SourceTextDocumentBinding *>(parent());
        if (!binding) return;
        binding->setTextDocument(qqtd ? qqtd->textDocument() : nullptr);
    }
};

/// QML foreign-type registration for the foundation-side
/// `Markoff::SourceTextDocumentBinding`. Keeps the foundation header
/// QML-free while making the type available as
/// `SourceTextDocumentBinding` in QML code, with a QML-friendly
/// `setQtQuickDocument(QQuickTextDocument*)` invokable.
struct SourceTextDocumentBindingForeign {
    Q_GADGET
    QML_FOREIGN(Markoff::SourceTextDocumentBinding)
    QML_NAMED_ELEMENT(SourceTextDocumentBinding)
    QML_EXTENDED(SourceTextDocumentBindingExtension)
};

}  // namespace Markoff::View::Qml
