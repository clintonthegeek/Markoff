# Candidate Markoff installed-consumer acceptance

This parent-shaped fixture stages the candidate Markoff Core export and its
public dependencies and the Source, Styled and Canvas view libraries, then
the adjacent `markoff-core-installed-consumer` builds against that installed
export. Its Core consumer verifies durable document edits, while its view
consumer constructs all three editors against an imported vNext document. It
expects the selected installed
CollabText Core package and Qt/KF dependencies to be available.

```sh
cmake -S tests/collabtext-vnext/markoff-core-install-harness \
  -B /tmp/markoff-core-install-harness/build \
  -DMARKOFF_SOURCE_DIR="$PWD" \
  -DCMAKE_PREFIX_PATH=/path/to/collabtext-prefix \
  -DCMAKE_INSTALL_PREFIX=/tmp/markoff-core-install-harness/prefix \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/markoff-core-install-harness/build \
  --target markoff_core markoff_source markoff_styled markoff_canvas --parallel 2
cmake --install /tmp/markoff-core-install-harness/build
cmake -S tests/collabtext-vnext/markoff-core-installed-consumer \
  -B /tmp/markoff-core-install-harness/consumer-build \
  -DMARKOFF_TARGETS_FILE=/tmp/markoff-core-install-harness/prefix/lib/cmake/MarkoffCoreHarness/CorbomiteTargets.cmake \
  -DMARKOFF_INSTALL_PREFIX=/tmp/markoff-core-install-harness/prefix \
  -DCMAKE_PREFIX_PATH="/tmp/markoff-core-install-harness/prefix;/path/to/collabtext-prefix" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/markoff-core-install-harness/consumer-build --parallel 2
ctest --test-dir /tmp/markoff-core-install-harness/consumer-build --output-on-failure
```

The harness verifies the Core and view CMake target exports and installed
public-header consumers. It does not stand in for Corbomite's generated
package config, full application graph, production storage provider, or final
application acceptance.
