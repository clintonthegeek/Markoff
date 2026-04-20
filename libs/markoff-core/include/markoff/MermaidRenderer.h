// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include <QByteArray>
#include <QString>

namespace Markoff {

/// Abstract Mermaid renderer. The host supplies a concrete implementation
/// (Corbomite wires the mmdr Rust FFI); standalone builds fall back to
/// Markoff::DefaultMermaidRenderer which returns empty bytes.
class MermaidRenderer
{
public:
    virtual ~MermaidRenderer() = default;

    /// Render a Mermaid source string to SVG bytes. Returns empty on failure
    /// or when no renderer is available.
    virtual QByteArray renderSvg(const QString &source) const = 0;
};

} // namespace Markoff
