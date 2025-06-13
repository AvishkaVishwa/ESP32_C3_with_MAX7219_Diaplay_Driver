set(APP rtci2c_test)
add_executable(${APP} main.c)
target_link_libraries(${APP} rtci2c)