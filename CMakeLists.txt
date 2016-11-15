cmake_minimum_required(VERSION 3)
project(CuckooMap)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 11)

add_library (tdigest 
    3rdParty/t-digest/src/tdigest/avltree.cpp
    3rdParty/t-digest/src/tdigest/tdigest.cpp
)
target_include_directories(tdigest PUBLIC 3rdParty/t-digest/src/tdigest/)


#list(APPEND CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -DNDEBUG ${CMAKE_CXX_FLAGS}")
#list(APPEND CMAKE_EXE_LINKER_FLAGS "-O0 -g -fprofile-arcs -ftest-coverage -DNDEBUG ${CMAKE_EXE_LINKER_FLAGS}")


add_library(cuckoo INTERFACE)
target_include_directories(cuckoo INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(sometest
    path_to_test.cpp
)
target_link_libraries(sometest PRIVATE cuckoo tdigest)
