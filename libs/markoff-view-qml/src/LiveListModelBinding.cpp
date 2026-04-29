// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveListModelBinding.h>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveSelectionModel.h>
#include "BlockWalker.h"
#include "AstBlockDiff.h"

#include <markoff-parser/Document.h>

#include <QSet>

namespace Markoff::View::Qml {

struct LiveListModelBinding::Private {
    EditorBackend       *editorBackend = nullptr;
    LiveBlockModel      *model         = nullptr;
    LiveSelectionModel  *selection     = nullptr;
    QList<BlockKey>      lastKeys;
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model = new LiveBlockModel(this);
    d->selection = new LiveSelectionModel(this);
}

LiveListModelBinding::~LiveListModelBinding() = default;

EditorBackend *LiveListModelBinding::editorBackend() const { return d->editorBackend; }
LiveBlockModel *LiveListModelBinding::model() const { return d->model; }
LiveSelectionModel *LiveListModelBinding::selectionModel() const { return d->selection; }

void LiveListModelBinding::setEditorBackend(EditorBackend *eb)
{
    if (d->editorBackend == eb) return;
    if (d->editorBackend) {
        QObject::disconnect(d->editorBackend, nullptr, this, nullptr);
    }
    d->editorBackend = eb;
    if (d->editorBackend) {
        QObject::connect(d->editorBackend, &EditorBackend::parseUpdatedAt,
                         this, &LiveListModelBinding::onParseUpdatedAt);
    }
    Q_EMIT editorBackendChanged();
}

void LiveListModelBinding::onParseUpdatedAt(const Markoff::Document *parsed,
                                            CollabText::Crdt::Global /*atVersion*/)
{
    if (!parsed) return;
    const QString source = parsed->sourceText();
    const QList<BlockRecord> nextRecords = BlockWalker::walk(source);

    QList<BlockKey> nextKeys;
    nextKeys.reserve(nextRecords.size());
    for (const auto &r : nextRecords) {
        nextKeys.append(BlockKey { r.kind, r.source });
    }

    const QList<AstBlockDiff::Op> ops = AstBlockDiff::diff(d->lastKeys, nextKeys);

    if (d->selection->hasSelection()) {
        QSet<int> deletedPrevIndices;
        for (const auto &op : ops) {
            if (op.kind == AstBlockDiff::OpKind::Delete) {
                deletedPrevIndices.insert(op.prevIndex);
            }
        }
        const int aB = d->selection->anchorBlock();
        const int actB = d->selection->activeBlock();
        if (deletedPrevIndices.contains(aB) || deletedPrevIndices.contains(actB)) {
            d->selection->clear();
        }
    }

    d->model->applyOps(ops, nextRecords);
    d->lastKeys = nextKeys;
}

}  // namespace Markoff::View::Qml
