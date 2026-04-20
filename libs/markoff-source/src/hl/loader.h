// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
//
// Originally from qutepart-cpp (https://github.com/diegoiast/qutepart-cpp)
// (c) 2024 Diego Iastrubni, MIT-licensed.
// Fork point: commit eec2e9ae5b50b591f017296ee743ee2860a280e4, 2026-04-12.
//
// Modifications (c) 2026 Corbomite contributors, GPL-3.0-or-later.

#pragma once

#include <QSharedPointer>
#include <QXmlStreamReader>

#include "language.h"

namespace Qutepart {

QSharedPointer<Language> loadLanguage(const QString &xmlFileName);

ContextPtr loadExternalContext(const QString &contextName);

} // namespace Qutepart
