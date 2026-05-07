add_executable(markoff-source-app main.cpp)
target_link_libraries(markoff-source-app
    PRIVATE Qt6::Widgets markoff_source markoff_core)
