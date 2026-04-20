// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#pragma once

#include "markoff/MermaidRenderer.h"

namespace Markoff {

/// No-op Mermaid renderer. Returns empty SVG bytes. Used by ReadingView as
/// a lazy default when the host has not injected a real renderer.
class DefaultMermaidRenderer : public MermaidRenderer
{
public:
    QByteArray renderSvg(const QString & /*source*/) const override
    {
        return {};
    }
};

} // namespace Markoff
