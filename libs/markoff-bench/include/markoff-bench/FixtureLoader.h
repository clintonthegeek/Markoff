// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QStringList>

namespace Markoff::Bench {

/// Load a real-doc fixture from libs/markoff-bench/fixtures/<name>.md.
/// Returns the file's bytes, or an empty QByteArray if the file does
/// not exist or cannot be read. Logs a warning to qWarning() on failure.
QByteArray loadFixture(const QString &name);

/// Names of all available fixtures (without the .md suffix).
QStringList availableFixtures();

}  // namespace Markoff::Bench
