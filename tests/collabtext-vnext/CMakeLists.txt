cmake_minimum_required(VERSION 3.19)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(markoff_collabtext_vnext_probe LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This is an installed-package consumer. Configure with CMAKE_PREFIX_PATH set
# to a relocated CollabText installation; never add the CollabText source tree.
find_package(collabtext CONFIG REQUIRED COMPONENTS core)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test)

if(NOT TARGET Markoff::CollabTextVNext)
    add_subdirectory(../../libs/markoff-collabtext-vnext
                     ${CMAKE_CURRENT_BINARY_DIR}/markoff-collabtext-vnext)
endif()

enable_testing()
add_executable(markoff_collabtext_vnext_probe main.cpp)
target_link_libraries(markoff_collabtext_vnext_probe PRIVATE Markoff::CollabTextVNext)
add_test(NAME markoff_collabtext_vnext_probe COMMAND markoff_collabtext_vnext_probe)

add_executable(markoff_document_storage_provider_test tst_document_storage_provider.cpp)
target_link_libraries(markoff_document_storage_provider_test PRIVATE Markoff::CollabTextVNext)
add_test(NAME markoff_document_storage_provider_test
         COMMAND markoff_document_storage_provider_test)

add_executable(markoff_provider_scope_test tst_provider_scope.cpp)
target_link_libraries(markoff_provider_scope_test PRIVATE Markoff::CollabTextVNext)
add_test(NAME markoff_provider_scope_test COMMAND markoff_provider_scope_test)

if(TARGET Markoff::Core)
    add_executable(markoff_provider_backed_document_test
        tst_provider_backed_document.cpp
    )
    set_target_properties(markoff_provider_backed_document_test PROPERTIES AUTOMOC ON)
    target_include_directories(markoff_provider_backed_document_test PRIVATE
        ../../libs/markoff-core/include
        ../../libs/markoff-parser/include
        ../../libs/markoff-text-units/include
    )
    target_link_libraries(markoff_provider_backed_document_test
        PRIVATE Markoff::Core)
    add_test(NAME markoff_provider_backed_document_test
        COMMAND markoff_provider_backed_document_test)

    # Exercise the action-bundle transport consumer in the candidate suite
    # even when the legacy live/QML demo target is omitted. The demo app's
    # test directory registers the same test when that app is enabled.
    if(NOT TARGET tst_vnext_action_consumer)
        add_executable(tst_vnext_action_consumer
            ../../apps/markoff-collab-testapp/tests/tst_vnext_action_consumer.cpp
            ../../apps/markoff-collab-testapp/CollabConsumer.cpp
            ../../apps/markoff-collab-testapp/EphemeralPresence.cpp
            ../../apps/markoff-collab-testapp/InMemoryTransport.cpp
        )
        set_target_properties(tst_vnext_action_consumer PROPERTIES AUTOMOC ON)
        target_include_directories(tst_vnext_action_consumer PRIVATE
            ../../apps/markoff-collab-testapp
        )
        target_link_libraries(tst_vnext_action_consumer PRIVATE
            Qt6::Core Qt6::Test Markoff::Core
        )
        add_test(NAME tst_vnext_action_consumer COMMAND tst_vnext_action_consumer)
    endif()
endif()
