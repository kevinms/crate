cmake_minimum_required(VERSION 2.8.12)
project(crate)

add_library(crate crate.c list.c)
target_include_directories(crate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crate pthread)

add_subdirectory(tests)
