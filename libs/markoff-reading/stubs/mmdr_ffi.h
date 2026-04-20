// SPDX-License-Identifier: GPL-3.0-or-later
// Phase-A stub shim for the `mmdr` Rust FFI. Real rendering is Phase B.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Phase-A stub: always fails; MermaidRenderer::renderSvg returns an empty
// QByteArray and SectionLayout renders nothing for mermaid fences.
inline int mmdr_render_svg(const char * /*input*/, char **output)
{
    if (output) *output = nullptr;
    return -1;
}

inline void mmdr_free(char * /*ptr*/) {}

#ifdef __cplusplus
}
#endif
