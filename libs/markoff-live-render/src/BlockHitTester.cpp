// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockHitTester.h>

#include <QMetaObject>
#include <QVariant>

namespace Markoff::LiveRender {

BlockHitTester::BlockHitTester(QObject *parent) : QObject(parent) {}

void BlockHitTester::setListView(QObject *lv)
{
    if (m_listView == lv) return;
    m_listView = lv;
    Q_EMIT listViewChanged();
}

QVariantMap BlockHitTester::miss()
{
    return {{ QStringLiteral("blockIndex"), -1 }, { QStringLiteral("qtPos"), -1 }};
}

QVariantMap BlockHitTester::makeResult(int blockIndex, int qtPos)
{
    return {{ QStringLiteral("blockIndex"), blockIndex },
            { QStringLiteral("qtPos"), qtPos }};
}

QVariantMap BlockHitTester::hit(double mouseX, double mouseY,
                                 double viewportWidth) const
{
    if (!m_listView) return miss();

    const int    count         = m_listView->property("count").toInt();
    const double contentX      = qProp(m_listView, "contentX");
    const double contentY      = qProp(m_listView, "contentY");
    const double contentHeight = qProp(m_listView, "contentHeight");
    const double lv_width      = qProp(m_listView, "width");
    const double lv_height     = qProp(m_listView, "height");

    if (count == 0) return miss();

    // Clamp to visible area.
    const double clampedX = qMax(0.0, qMin(mouseX, viewportWidth - 1));
    const double clampedY = qMax(0.0, qMin(mouseY, lv_height > 0 ? lv_height - 1 : 9999.0));

    const double cx = clampedX + contentX;
    const double cy = clampedY + contentY;

    // Probe x guaranteed inside items' horizontal band.
    const double probeX = lv_width / 2.0;

    auto clampedLocalX = [](QObject *item, double contentCx) -> double {
        const double w = qProp(item, "width");
        return qMax(0.0, qMin(contentCx - qProp(item, "x"), w - 1));
    };

    // Below all content.
    if (cy >= contentHeight) {
        QObject *probe = itemAt(probeX, contentHeight - 1);
        if (probe) {
            const double localY = qMax(0.0, qProp(probe, "height") - 1);
            return makeResult(probe->property("modelIndex").toInt(),
                              positionAt(probe, clampedLocalX(probe, cx), localY));
        }
        return makeResult(count - 1, -1);
    }

    // Above all content.
    if (cy < 0) return makeResult(0, 0);

    // Direct hit.
    QObject *item = itemAt(probeX, cy);
    if (item) {
        const double localY = cy - qProp(item, "y");
        return makeResult(item->property("modelIndex").toInt(),
                          positionAt(item, clampedLocalX(item, cx), localY));
    }

    // In gap between delegates: walk up and down.
    QObject *aboveItem = nullptr, *belowItem = nullptr;
    double aboveDy = 0, belowDy = 0;
    for (double dy = 4; dy < 64; dy += 4) {
        if (!aboveItem) {
            auto *a = itemAt(probeX, qMax(0.0, cy - dy));
            if (a) { aboveItem = a; aboveDy = dy; }
        }
        if (!belowItem) {
            auto *b = itemAt(probeX, qMin(contentHeight - 1, cy + dy));
            if (b) { belowItem = b; belowDy = dy; }
        }
        if (aboveItem && belowItem) break;
    }
    if (aboveItem && (!belowItem || aboveDy <= belowDy)) {
        const double localY = qMax(0.0, qProp(aboveItem, "height") - 1);
        return makeResult(aboveItem->property("modelIndex").toInt(),
                          positionAt(aboveItem, clampedLocalX(aboveItem, cx), localY));
    }
    if (belowItem) {
        return makeResult(belowItem->property("modelIndex").toInt(),
                          positionAt(belowItem, clampedLocalX(belowItem, cx), 0));
    }
    return miss();
}

double BlockHitTester::qProp(QObject *obj, const char *name)
{
    if (!obj) return 0.0;
    return obj->property(name).toDouble();
}

QObject *BlockHitTester::itemAt(double cx, double cy) const
{
    // qReturnArg (Qt 6.5+) handles QObject* return types correctly regardless
    // of whether the callee declares QObject* or a concrete subtype.
    QObject *result = nullptr;
    QMetaObject::invokeMethod(m_listView, "itemAt",
        Qt::DirectConnection,
        qReturnArg(result), cx, cy);
    return result;
}

int BlockHitTester::positionAt(QObject *item, double localX, double localY)
{
    if (!item) return 0;
    int result = 0;
    QMetaObject::invokeMethod(item, "positionAt",
        Qt::DirectConnection,
        qReturnArg(result), localX, localY);
    return result;
}

}  // namespace Markoff::LiveRender
