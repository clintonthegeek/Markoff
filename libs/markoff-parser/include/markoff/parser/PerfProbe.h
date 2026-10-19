// SPDX-License-Identifier: GPL-3.0-or-later
//
// PerfProbe — opt-in per-stage timing accumulator.
//
// Header-only singleton with a scoped RAII helper, used to attribute
// per-keystroke cost across the parser → core → highlighter cascade.
//
// Compiled out unless MARKOFF_PERF is defined: with the probe on, every site
// costs a clock read plus a mutex round-trip and a map insert, which is enough
// to perturb the numbers the probe exists to measure (a 500-heading document
// used to pay 500 of them per parse). Build with -DMARKOFF_PERF to enable.
//
// Usage:
//   { MARKOFF_PERF_SCOPE("TreeSitterParser::parse"); ... }
//   Markoff::Perf::Probe::instance().dump("after 20 keystrokes");
#pragma once

#if !defined(MARKOFF_PERF)

#define MARKOFF_PERF_SCOPE(name) do { } while (false)

#else

#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QLatin1String>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <algorithm>

namespace Markoff::Perf {

struct Stat {
    qint64 totalNs = 0;
    int    calls   = 0;
};

class Probe {
public:
    static Probe &instance() { static Probe p; return p; }

    /// Hot path: `name` must be a string literal, so the key is its stable
    /// address and no QString is built per call.
    void record(const char *name, qint64 ns)
    {
        QMutexLocker lk(&m_mu);
        Stat &s = m_stats[name];
        s.totalNs += ns;
        s.calls   += 1;
    }

    void note(const char *name)
    {
        QMutexLocker lk(&m_mu);
        m_stats[name].calls += 1;
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
            items.append({QString::fromLatin1(it.key()), it.value()});
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
    QHash<const char *, Stat> m_stats;
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

#endif // MARKOFF_PERF
