// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim. See core/VaultResourceProvider.h for context.
#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <optional>

namespace Corbomite {

struct SourcePosition
{
    int line = 0;
    int col = 0;
    int offset = 0;
};

struct SourceRange
{
    SourcePosition start;
    SourcePosition end;
};

struct HeadingCache
{
    QString heading;
    int level = 0;
    SourceRange position;
};

struct BlockCache
{
    QString id;
    SourceRange position;
};

struct SectionCache
{
    QString type;
    SourceRange position;
};

struct CachedMetadata
{
    std::optional<QVector<HeadingCache>> headings;
    std::optional<QHash<QString, BlockCache>> blocks;
    std::optional<QVector<SectionCache>> sections;
};

} // namespace Corbomite
