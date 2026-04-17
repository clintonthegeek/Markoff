// SPDX-License-Identifier: GPL-3.0-or-later
#include "FoldingModel.h"
#include <QJsonArray>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcFolding, "markoff.folding")

namespace Markoff {

FoldingModel::FoldingModel(QObject *parent) : QObject(parent) {}

bool FoldingModel::isFolded(const FoldRegionKey &path) const {
    return m_folded.contains(path);
}

QList<FoldRegionKey> FoldingModel::foldedPaths() const {
    return QList<FoldRegionKey>(m_folded.begin(), m_folded.end());
}

QList<FoldRegionKey> FoldingModel::allPaths() const {
    QList<FoldRegionKey> r;
    r.reserve(m_headings.size());
    for (const auto &h : m_headings) r << h.path;
    return r;
}

bool FoldingModel::isHiddenByFold(const FoldRegionKey &path) const {
    // Any ancestor prefix (not the path itself) being folded hides it.
    // A heading hides its CHILDREN; the heading line itself stays visible.
    for (int i = 1; i < path.size(); ++i) {
        FoldRegionKey prefix = path.mid(0, i);
        if (m_folded.contains(prefix)) return true;
    }
    return false;
}

void FoldingModel::fold(const FoldRegionKey &path) {
    const bool inserted = !m_folded.contains(path);
    if (inserted) { m_folded.insert(path); emit foldStateChanged(); }
}

void FoldingModel::unfold(const FoldRegionKey &path) {
    const bool removed = m_folded.remove(path);
    if (removed) emit foldStateChanged();
}

void FoldingModel::toggle(const FoldRegionKey &path) {
    if (m_folded.contains(path)) unfold(path);
    else fold(path);
}

void FoldingModel::foldAll() {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (!m_folded.contains(h.path)) { m_folded.insert(h.path); changed = true; }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::unfoldAll() {
    if (m_folded.isEmpty()) return;
    m_folded.clear();
    emit foldStateChanged();
}

void FoldingModel::foldAllAtLevel(int level) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level == level && !m_folded.contains(h.path)) {
            m_folded.insert(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::unfoldAllAtLevel(int level) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level == level && m_folded.contains(h.path)) {
            m_folded.remove(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::foldLevel(int n) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level >= n && !m_folded.contains(h.path)) {
            m_folded.insert(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}

void FoldingModel::unfoldLevel(int n) {
    bool changed = false;
    for (const auto &h : m_headings) {
        if (h.info.level >= n && m_folded.contains(h.path)) {
            m_folded.remove(h.path);
            changed = true;
        }
    }
    if (changed) emit foldStateChanged();
}
QJsonObject FoldingModel::serialize() const {
    QJsonArray folds;
    for (const auto &p : m_folded) {
        QJsonArray arr;
        for (const auto &seg : p) arr.append(seg);
        folds.append(arr);
    }
    QJsonObject root;
    root["version"] = 1;
    root["folds"] = folds;
    return root;
}

void FoldingModel::restore(const QJsonObject &obj) {
    const auto prev = m_folded;
    m_folded.clear();

    const QJsonValue foldsVal = obj.value("folds");
    if (foldsVal.isArray()) {
        for (const auto &entry : foldsVal.toArray()) {
            if (!entry.isArray()) continue;
            QStringList path;
            for (const auto &seg : entry.toArray()) {
                if (seg.isString()) path << seg.toString();
            }
            if (!path.isEmpty()) m_folded.insert(path);
        }
    } else if (!foldsVal.isUndefined() && !foldsVal.isNull()) {
        qCWarning(lcFolding) << "restore: 'folds' must be an array, got" << foldsVal.type();
    }

    if (prev != m_folded) emit foldStateChanged();
}
void FoldingModel::reconcile(const QList<HeadingInfo> &newHeadings) {
    // 1. Rebuild heading cache with paths.
    const auto paths = computeHeadingPaths(newHeadings);
    m_headings.clear();
    m_headings.reserve(newHeadings.size());
    QSet<FoldRegionKey> newPathSet;
    for (int i = 0; i < newHeadings.size(); ++i) {
        m_headings.append({ paths[i], newHeadings[i] });
        newPathSet.insert(paths[i]);
    }

    // 2. Intersect folded set with newPathSet.
    const auto prev = m_folded;
    auto it = m_folded.begin();
    while (it != m_folded.end()) {
        if (!newPathSet.contains(*it)) it = m_folded.erase(it);
        else ++it;
    }
    if (prev != m_folded) emit foldStateChanged();
}

QList<FoldRegionKey> FoldingModel::unfoldAncestors(const FoldRegionKey &path) {
    QList<FoldRegionKey> unfolded;
    for (int i = 1; i <= path.size(); ++i) {
        FoldRegionKey prefix = path.mid(0, i);
        if (m_folded.contains(prefix)) {
            m_folded.remove(prefix);
            unfolded.append(prefix);
        }
    }
    if (!unfolded.isEmpty()) emit foldStateChanged();
    return unfolded;
}

} // namespace Markoff
