qt_add_executable(tst_d5_op_types tst_d5_op_types.cpp)
target_link_libraries(tst_d5_op_types PRIVATE Qt6::Test markoff_core)
add_test(NAME tst_d5_op_types COMMAND tst_d5_op_types)
