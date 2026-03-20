# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# Exclude standalone test entrypoint from normal firmware build.
list(REMOVE_ITEM app_sources "${CMAKE_SOURCE_DIR}/src/test_all_on.cpp")

idf_component_register(SRCS ${app_sources})
