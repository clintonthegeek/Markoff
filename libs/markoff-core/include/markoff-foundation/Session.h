// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

#include <crdt/Anchor.h>

#include <markoff-foundation/FoldRef.h>
#include <markoff-foundation/MarkoffFoundationExport.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/SessionParams.h>

namespace Markoff {

class MarkoffDocument;

/// A view's per-document state. Owned by the parent MarkoffDocument
/// (which calls createSession / destroySession). Tracks primary +
/// secondary selections, scroll, folded regions. Hot-swap via
/// copyStateFrom. Persistence via toJson / fromJson. See spec §7.2.
class MARKOFF_FOUNDATION_EXPORT Session : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Session)
public:
    explicit Session(MarkoffDocument *doc, const SessionParams &params);
    ~Session() override;

    QString id() const;
    QString participantId() const;
    QString participantLabel() const;
    QColor  presenceColor() const;

    // Filled in Tasks 19-22.
    Selection primarySelection() const;
    void      setPrimarySelection(const Selection &);

    const QList<Selection> &secondarySelections() const;
    void setSecondarySelections(QList<Selection>);
    void addSecondarySelection(Selection);
    void clearSecondarySelectionsOfKind(Selection::Kind);

    CollabText::Crdt::Anchor topVisibleAnchor() const;
    qreal                    topVisibleFraction() const;
    void setTopVisible(CollabText::Crdt::Anchor, qreal fraction);

    const QList<FoldRef> &foldedRegions() const;
    void                  setFoldedRegions(QList<FoldRef>);
    void                  toggleFold(const FoldRef &);

    void copyStateFrom(const Session &other);

    QJsonObject toJson() const;
    void        fromJson(const QJsonObject &);

Q_SIGNALS:
    void primarySelectionChanged(const Markoff::Selection &);
    void secondarySelectionsChanged();
    void scrollChanged(CollabText::Crdt::Anchor, qreal fraction);
    void foldedRegionsChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

}  // namespace Markoff
