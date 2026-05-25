// SPDX-License-Identifier: GPL-3.0-or-later
//
// PerfProbe — debug-only per-stage timing accumulator.
//
// Header-only singleton with a scoped RAII helper. Used by the table
// typing-perf benchmark to attribute per-keystroke cost across the
// parser → core → live → highlighter cascade. Probe sites are unconditional
// (cost: one QElapsedTimer + one hash insert) and benign to leave in
// non-benchmark builds.
//
// Usage:
//   { MARKOFF_PERF_SCOPE("TreeSitterParser::parse"); ... }
//   Markoff::Perf::Probe::instance().dump("after 20 keystrokes");
//
// JS sites call through TableEditBinding's Q_INVOKABLE perfTime/perfNote.
#pragma once

#include <QHash>
#include <QString>
#include <QElapsedTimer>
#include <QDebug>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

namespace Markoff::Perf {

struct Stat {
    qint64 totalNs = 0;
    int    calls   = 0;
};

class Probe {
public:
    static Probe &instance() { static Probe p; return p; }

    void record(const char *name, qint64 ns)
    {
        QMutexLocker lk(&m_mu);
        Stat &s = m_stats[QString::fromLatin1(name)];
        s.totalNs += ns;
        s.calls   += 1;
    }

    void recordMs(const QString &name, double ms)
    {
        QMutexLocker lk(&m_mu);
        Stat &s = m_stats[name];
        s.totalNs += static_cast<qint64>(ms * 1.0e6);
        s.calls   += 1;
    }

    void note(const QString &name)
    {
        QMutexLocker lk(&m_mu);
        Stat &s = m_stats[name];
        s.calls += 1;
    }

    void reset()
    {
        QMutexLocker lk(&m_mu);
        m_stats.clear();
    }

    void dump(const char *label)
    {
        QMutexLocker lk(&m_mu);
        qDebug().noquote() << "=== PerfProbe" << label << "===";
        QList<QPair<QString, Stat>> items;
        items.reserve(m_stats.size());
        for (auto it = m_stats.cbegin(); it != m_stats.cend(); ++it)
            items.append({it.key(), it.value()});
        std::sort(items.begin(), items.end(),
                  [](const auto &a, const auto &b) {
                      return a.second.totalNs > b.second.totalNs;
                  });
        for (const auto &p : items) {
            const Stat &s = p.second;
            const double totMs = double(s.totalNs) / 1.0e6;
            const double avgUs = s.calls
                ? (double(s.totalNs) / s.calls) / 1000.0
                : 0.0;
            qDebug().noquote()
                << QString("  %1  calls=%2  total=%3 ms  avg=%4 us")
                       .arg(p.first, -52)
                       .arg(s.calls, 5)
                       .arg(totMs, 9, 'f', 3)
                       .arg(avgUs, 9, 'f', 2);
        }
        qDebug().noquote() << "=== end PerfProbe ===";
    }

private:
    QMutex m_mu;
    QHash<QString, Stat> m_stats;
};

class Scope {
public:
    explicit Scope(const char *name) : m_name(name) { m_timer.start(); }
    ~Scope() { Probe::instance().record(m_name, m_timer.nsecsElapsed()); }
private:
    const char *m_name;
    QElapsedTimer m_timer;
};

} // namespace Markoff::Perf

#define MARKOFF_PERF_CONCAT_(a, b) a##b
#define MARKOFF_PERF_CONCAT(a, b)  MARKOFF_PERF_CONCAT_(a, b)
#define MARKOFF_PERF_SCOPE(name)   \
    ::Markoff::Perf::Scope MARKOFF_PERF_CONCAT(_markoff_perf_scope_, __LINE__)(name)
