# Spike directory

Throwaway prototypes that prove (or disprove) architectural assumptions
*before* writing the production code.

Each subdirectory is a self-contained spike with its own `CMakeLists.txt`.
Build from inside the spike directory:

```
cd <spike-name>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 8
./build/<spike-target>
```

Spikes ARE committed (so future contributors can run them) but the build
directories are gitignored. Spikes are not part of any production target.

## Index

- `cross-block-selection/` — proves the `LiveSelectionModel` + per-delegate
  `Connections` + top-level `MouseArea` pattern delivers native-feeling
  cross-block text selection in a QML `ListView`. Findings: `docs/specs/2026-04-29-cross-block-selection-spike-findings.md`.
