cmake_minimum_required(VERSION 3.19)
project(Markoff_Tests LANGUAGES CXX)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
find_package(KF6SyntaxHighlighting REQUIRED)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Cluster J phase 3 — honesty regression gate. No "bases" hardcode may
# appear in libs/markoff/src/ hover-link emissions.
add_executable(tst_markoff_link_source_honesty tst_markoff_link_source_honesty.cpp)
add_test(NAME tst_markoff_link_source_honesty COMMAND tst_markoff_link_source_honesty)
target_link_libraries(tst_markoff_link_source_honesty PRIVATE Qt6::Test)

# Cluster J phase 3 — positive integration test: wikilinks are clickable
# via the Editor -> LinkRenderer bridge and fire the previously-dormant
# linkClicked signal with the correct target + honest source string.
add_executable(tst_markoff_wikilink_clickable tst_markoff_wikilink_clickable.cpp)
add_test(NAME tst_markoff_wikilink_clickable COMMAND tst_markoff_wikilink_clickable)
target_link_libraries(tst_markoff_wikilink_clickable PRIVATE
    Qt6::Test Qt6::Widgets markoff_live KF6::SyntaxHighlighting)
# Internal headers (MarkdownTextItem, SelectableItem) are in src/ and
# needed for the end-to-end anchorHref check; mirrors the existing
# libs/markoff-live/tests/ test-target convention.
target_include_directories(tst_markoff_wikilink_clickable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/markoff-live/src)
set_tests_properties(tst_markoff_wikilink_clickable PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Task 15 — Cross-leaf smoke test: polymorphic MarkdownView dispatch across
# live/source/reading leaves through the MarkdownView base interface.
add_executable(tst_tri_view_smoke tst_tri_view_smoke.cpp)
add_test(NAME tst_tri_view_smoke COMMAND tst_tri_view_smoke)
target_link_libraries(tst_tri_view_smoke PRIVATE
    Qt6::Test Qt6::Widgets
    markoff_core
    markoff_live
    markoff_source
    markoff_reading
)
set_tests_properties(tst_tri_view_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Task 17 — Top-level tri-view canonical interop test. All three leaves bound
# to one MarkoffDocument: cross-leaf edit propagation + parseUpdated burst
# coalescing.
add_executable(tst_canonical_interop tst_canonical_interop.cpp)
add_test(NAME tst_canonical_interop COMMAND tst_canonical_interop)
target_link_libraries(tst_canonical_interop PRIVATE
    Qt6::Test Qt6::Widgets
    markoff_core
    markoff_live
    markoff_source
    markoff_reading
)
# SceneCoordinator.h is an internal markoff-live header; expose it for the
# coordinatorForTesting() call the same way tst_live_canonical_attach does.
target_include_directories(tst_canonical_interop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/markoff-live/src)
set_tests_properties(tst_canonical_interop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
