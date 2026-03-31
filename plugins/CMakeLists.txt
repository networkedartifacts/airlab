cmake_minimum_required(VERSION 3.20)
project(plugins C)

set(CMAKE_C_STANDARD 11)

# gather all example sources
file(GLOB SOURCES examples/*/*.c screens/*/*.c)

# dummy target for IDE indexing (not meant to be built)
add_library(plugins STATIC ${SOURCES} al.h screens/common.h)
