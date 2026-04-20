# SPDX-License-Identifier: GPL-3.0-or-later
#
# Phase 1 tests for qutepart-corbomite. Only the smoke test ships right now;
# more tests are added in later phases as the library is shaped.

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Test)

qt_add_executable(tst_qutepart_smoke tst_qutepart_smoke.cpp)

target_link_libraries(tst_qutepart_smoke
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        qutepart-corbomite
)

add_test(NAME tst_qutepart_smoke COMMAND tst_qutepart_smoke)

# Headless run via offscreen QPA, so ctest works in environments without a
# display server (CI, container, orchestrator shell).
set_tests_properties(tst_qutepart_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
