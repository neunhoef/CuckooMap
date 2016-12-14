cmake_minimum_required(VERSION 3.0)
project(CuckooMap)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

find_package(jemalloc REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

#list(APPEND CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -DNDEBUG ${CMAKE_CXX_FLAGS}")
#list(APPEND CMAKE_EXE_LINKER_FLAGS "-O0 -g -fprofile-arcs -ftest-coverage -DNDEBUG ${CMAKE_EXE_LINKER_FLAGS}")

add_library(qdigest INTERFACE)
target_include_directories(qdigest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/qdigest)

add_library(rocksdb INTERFACE)
target_include_directories(rocksdb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/rocksdb/include)

add_library(cppbtree
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/cpp-btree/btree.h
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/cpp-btree/btree_map.h
  ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/cpp-btree/btree_set.h
)
set_target_properties(cppbtree PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(cppbtree INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/cpp-btree)

add_library(cuckoo INTERFACE)
target_include_directories(cuckoo INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(CuckooFilterTest
    tests/CuckooFilterTest.cpp
)
target_link_libraries(CuckooFilterTest PRIVATE cuckoo)

add_executable(CuckooMapTest
    tests/CuckooMapTest.cpp
)
target_link_libraries(CuckooMapTest PRIVATE cuckoo)

add_executable(CuckooMultiMapTest
    tests/CuckooMultiMapTest.cpp
)
target_link_libraries(CuckooMultiMapTest PRIVATE cuckoo)

add_executable(InternalCuckooMapTest
    tests/InternalCuckooMapTest.cpp
)
target_link_libraries(InternalCuckooMapTest PRIVATE cuckoo)

add_executable(ShardedCuckooMapTest
    tests/ShardedCuckooMapTest.cpp
)
target_link_libraries(ShardedCuckooMapTest PRIVATE cuckoo)

add_executable(ShardedCuckooMultiMapTest
    tests/ShardedCuckooMultiMapTest.cpp
)
target_link_libraries(ShardedCuckooMultiMapTest PRIVATE cuckoo)

add_executable(PerformanceTest
    tests/PerformanceTest.cpp
)
target_link_libraries(PerformanceTest PRIVATE
    ${CMAKE_THREAD_LIBS_INIT}
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/rocksdb/librocksdb.a
    ${ZLIB_LIBRARIES}
    ${BZIP2_LIBRARIES}
    snappy
    jemalloc
    -ldl
    rocksdb
    cppbtree
    qdigest
    cuckoo
)

add_custom_command(TARGET PerformanceTest PRE_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                       ${CMAKE_SOURCE_DIR}/performance $<TARGET_FILE_DIR:PerformanceTest>/performance)
