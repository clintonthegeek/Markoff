cmake_minimum_required(VERSION 3.19)

if(NOT TARGET CollabText::Core)
    find_package(collabtext CONFIG REQUIRED COMPONENTS core)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
if(NOT TARGET Markoff::TextUnits)
    add_subdirectory(../markoff-text-units
                     ${CMAKE_CURRENT_BINARY_DIR}/markoff-text-units)
endif()

add_library(markoff_collabtext_vnext STATIC
    include/markoff/vnext/AcceptedPrefixProjection.h
    include/markoff/vnext/ActionPublicationCoordinator.h
    include/markoff/vnext/ActionStager.h
    include/markoff/vnext/ActionTransactionBuilder.h
    include/markoff/vnext/ActionBundleCodec.h
    include/markoff/vnext/BlockIdentity.h
    include/markoff/vnext/CounterGapCodec.h
    include/markoff/vnext/CrossStreamHistoryCodec.h
    include/markoff/vnext/DocumentStorageProvider.h
    include/markoff/vnext/HistoryRetirement.h
    include/markoff/vnext/LocalHistoryCodec.h
    include/markoff/vnext/OwnerHistoryCodec.h
    include/markoff/vnext/ProviderBackedDocumentState.h
    include/markoff/vnext/QuarantineRecordCodec.h
    include/markoff/vnext/MarkoffMapCodec.h
    include/markoff/vnext/MarkoffMapValueCodec.h
    include/markoff/vnext/ReplicatedMapStream.h
    include/markoff/vnext/RouterBudget.h
    include/markoff/vnext/ScopedIdListStream.h
    include/markoff/vnext/ScopedListRouter.h
    include/markoff/vnext/ScopedMapRouter.h
    include/markoff/vnext/ScopedOperationRouter.h
    include/markoff/vnext/ScopedTextStream.h
    include/markoff/vnext/SourceSnapshotCodec.h
    include/markoff/vnext/StreamNames.h
    include/markoff/vnext/UndoDepth.h
    src/AcceptedPrefixProjection.cpp
    src/ActionBundleCodec.cpp
    src/ActionPublicationCoordinator.cpp
    src/ActionStager.cpp
    src/ActionTransactionBuilder.cpp
    src/CounterGapCodec.cpp
    src/CrossStreamHistoryCodec.cpp
    src/DocumentStorageProvider.cpp
    src/HistoryRetirement.cpp
    src/LocalHistoryCodec.cpp
    src/OwnerHistoryCodec.cpp
    src/ProviderBackedDocumentState.cpp
    src/QuarantineRecordCodec.cpp
    src/MarkoffMapCodec.cpp
    src/MarkoffMapValueCodec.cpp
    src/ReplicatedMapStream.cpp
    src/ScopedIdListStream.cpp
    src/ScopedListRouter.cpp
    src/ScopedMapRouter.cpp
    src/ScopedOperationRouter.cpp
    src/ScopedTextStream.cpp
    src/SourceSnapshotCodec.cpp
    src/StreamNames.cpp
)
add_library(Markoff::CollabTextVNext ALIAS markoff_collabtext_vnext)
set_target_properties(markoff_collabtext_vnext PROPERTIES
    EXPORT_NAME CollabTextVNext
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(markoff_collabtext_vnext PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../markoff-core/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(markoff_collabtext_vnext PUBLIC
    CollabText::Core
    Markoff::TextUnits
    Qt6::Core
)

if(DEFINED KDE_INSTALL_LIBDIR)
    install(TARGETS markoff_collabtext_vnext
        EXPORT CorbomiteTargets
        ARCHIVE DESTINATION ${KDE_INSTALL_LIBDIR}
    )
    install(DIRECTORY include/markoff/vnext
        DESTINATION ${KDE_INSTALL_INCLUDEDIR}/markoff
    )
endif()
