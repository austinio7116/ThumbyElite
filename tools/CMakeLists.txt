# ThumbyElite — native host tools (built via ExternalProject from the
# host/device builds so the cross toolchain never sees them).
cmake_minimum_required(VERSION 3.16)
project(thumbyelite_tools C)
set(CMAKE_C_STANDARD 11)

add_executable(obj2mesh obj2mesh.c)
target_link_libraries(obj2mesh m)
