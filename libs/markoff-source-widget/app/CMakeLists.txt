add_executable(markoff-source-widget-app main.cpp)
target_link_libraries(markoff-source-widget-app
    PRIVATE Qt6::Widgets markoff_source_widget markoff_foundation)
