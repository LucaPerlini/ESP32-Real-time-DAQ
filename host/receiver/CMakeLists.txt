cmake_minimum_required(VERSION 3.16)
project(rt_receiver C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-O2 -Wall -Wextra)

add_executable(rt_receiver
    rt_receiver.c
    serial.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common/crc16.c
)
target_include_directories(rt_receiver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
# clock_gettime / pthread scheduling: on glibc >= 2.17 these are in libc,
# but link rt/pthread explicitly for portability.
find_library(RT_LIB rt)
if(RT_LIB)
    target_link_libraries(rt_receiver PRIVATE ${RT_LIB})
endif()
