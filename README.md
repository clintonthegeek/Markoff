# Markoff

QGraphicsView-based Markdown editor widget + tree-sitter Markdown AST parser.
Extracted from the Corbomite project on 2026-04-16.

## Contents

- `libs/markoff/` — `Markoff::Markoff`, the Qt widget.
- `libs/markoff-parser/` — `MarkoffParser::MarkoffParser`, the AST + YAML parser.
- `libs/rapidyaml/` — `ryml::ryml`, vendored YAML parser (biojppm/rapidyaml).
- `tests/markoff/` — integration tests.

## Use

Standalone:

```bash
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

As a submodule (e.g., in Corbomite):

```cmake
add_subdirectory(libs/markoff-family)   # submodule path
# targets Markoff::Markoff, MarkoffParser::MarkoffParser, ryml::ryml now available
```

## Dependencies

- Qt6 ≥ 6.8 (Core, Gui, Widgets, Svg)
- Optionally `jkqtmathtext` and `mmdr` (Mermaid renderer) for math/mermaid
  rendering inside the widget — parent projects link these in.

## Provenance

History before 2026-04-16 lives in the Corbomite git log
(`https://…/Corbomite`). Use `git log --follow -- libs/markoff/<path>` in that
repo to see pre-extraction commits.

## License

GPL-3.0-or-later.
