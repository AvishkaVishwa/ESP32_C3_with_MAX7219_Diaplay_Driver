# Copyright 2024 Zorxx Software. All rights reserved.

# esp-idf component
if(IDF_TARGET)
   idf_component_register(SRCS "lib/rtci2c.c" "lib/ds1307.c" "lib/ds3231.c" "lib/esp-idf.c"
                          INCLUDE_DIRS "include"
                          PRIV_INCLUDE_DIRS "lib" "include/rtci2c"
                          PRIV_REQUIRES "driver" "esp_timer")
   return()
endif()

cmake_minimum_required(VERSION 3.5)
set(project rtci2c)
project(${project} LANGUAGES C VERSION 1.3.0)

add_library(rtci2c STATIC lib/rtci2c.c lib/ds1307.c lib/ds3231.c lib/linux.c)
target_include_directories(rtci2c PUBLIC include)
target_include_directories(rtci2c PRIVATE lib include/rtci2c)
target_compile_definitions(rtci2c PRIVATE SYS_DEBUG_ENABLE)
install(TARGETS rtci2c LIBRARY DESTINATION lib)
install(DIRECTORY include/rtci2c DESTINATION include)

add_subdirectory(example/linux)
