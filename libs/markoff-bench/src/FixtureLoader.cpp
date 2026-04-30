// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-bench/FixtureLoader.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

namespace Markoff::Bench {

#ifndef MARKOFF_BENCH_FIXTURE_DIR
#  error "MARKOFF_BENCH_FIXTURE_DIR not defined; set via CMake target_compile_definitions"
#endif

namespace {
Q_LOGGING_CATEGORY(lcFixture, "markoff.bench.fixture")
}

QByteArray loadFixture(const QString &name) {
    const QString path = QStringLiteral("%1/%2.md").arg(QStringLiteral(MARKOFF_BENCH_FIXTURE_DIR), name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcFixture).noquote() << "fixture not found:" << path;
        return {};
    }
    return f.readAll();
}

QStringList availableFixtures() {
    QDir d(QStringLiteral(MARKOFF_BENCH_FIXTURE_DIR));
    QStringList out;
    const auto entries = d.entryInfoList({QStringLiteral("*.md")}, QDir::Files);
    for (const auto &e : entries) out.append(e.completeBaseName());
    return out;
}

}  // namespace Markoff::Bench
