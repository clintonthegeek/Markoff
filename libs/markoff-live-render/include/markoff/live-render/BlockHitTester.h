// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>

#include <QObject>
#include <QVariantMap>
#include <qqmlintegration.h>

namespace Markoff::LiveRender {

/// Translates viewport mouse coordinates to a hit result by calling itemAt()
/// and positionAt() on the QML ListView via QMetaObject::invokeMethod.
///
/// Returns a QVariantMap with keys:
///   "blockIndex" : int  — -1 on miss; otherwise the delegate's modelIndex
///   "qtPos"      : int  — within-block UTF-16 offset; -1 for non-text blocks
///
/// QVariantMap is used (not a custom struct) so QML can access members
/// directly without registering a meta-type.
///
/// For testing: set `listView` to a mock QObject implementing:
///   Q_PROPERTY(int count)
///   Q_PROPERTY(double contentX/Y/Height/width/height)
///   Q_INVOKABLE QObject* itemAt(double cx, double cy)
/// Each item-QObject must implement:
///   Q_PROPERTY(double x/y/width/height)
///   Q_PROPERTY(int modelIndex)     — NOT "index" (context var inaccessible from C++)
///   Q_INVOKABLE int positionAt(double localX, double localY)
///
/// Note: delegates expose `modelIndex` as a declared QML property bound to
/// the ListView's injected `index` context variable. This makes it
/// accessible from C++ via QObject::property("modelIndex").
class MARKOFF_LIVE_RENDER_EXPORT BlockHitTester : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("BlockHitTester is provided by LiveListModelBinding")

    Q_PROPERTY(QObject *listView READ listView WRITE setListView NOTIFY listViewChanged)

public:
    explicit BlockHitTester(QObject *parent = nullptr);

    QObject *listView() const { return m_listView; }
    void setListView(QObject *lv);

    /// Hit-test (mouseX, mouseY) in viewport coordinates against the ListView.
    /// Returns QVariantMap{"blockIndex": int, "qtPos": int}.
    /// blockIndex is -1 on miss.
    Q_INVOKABLE QVariantMap hit(double mouseX, double mouseY,
                                double viewportWidth) const;

Q_SIGNALS:
    void listViewChanged();

private:
    static double qProp(QObject *obj, const char *name);
    QObject *itemAt(double cx, double cy) const;
    static int positionAt(QObject *item, double localX, double localY);
    static QVariantMap makeResult(int blockIndex, int qtPos);
    static QVariantMap miss();

    QObject *m_listView = nullptr;
};

}  // namespace Markoff::LiveRender
