add_executable(markoff-canvas-app main.cpp)
target_link_libraries(markoff-canvas-app
    PRIVATE Qt6::Widgets markoff_canvas markoff_core)
