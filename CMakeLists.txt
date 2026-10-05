cmake_minimum_required(VERSION 3.24)

project(IronPhoenix VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(IRONPHOENIX_BUILD_TESTS "Build IronPhoenix correctness tests" ON)
option(IRONPHOENIX_BUILD_BENCHMARK "Build IronPhoenix microbenchmark" ON)

add_library(ironphoenix_core STATIC
    src/geometry.cpp
    src/zobrist.cpp
    src/position.cpp
    src/see.cpp
    src/tt.cpp
    src/search.cpp
    src/fen.cpp
    src/movegen.cpp
    src/preft.cpp
    src/uci.cpp
)

add_library(IronPhoenix::Core ALIAS ironphoenix_core)

target_include_directories(ironphoenix_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ironphoenix_core PUBLIC cxx_std_20)
target_link_libraries(ironphoenix_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(ironphoenix_core PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Oi>
        $<$<CONFIG:RelWithDebInfo>:/Ot>
    )
else()
    target_compile_options(ironphoenix_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
    )
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT IRONPHOENIX_IPO_SUPPORTED OUTPUT IRONPHOENIX_IPO_ERROR)
if(IRONPHOENIX_IPO_SUPPORTED)
    set_property(TARGET ironphoenix_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET ironphoenix_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

add_executable(ironphoenix
    src/main.cpp
)

target_link_libraries(ironphoenix PRIVATE IronPhoenix::Core)
target_compile_features(ironphoenix PRIVATE cxx_std_20)
set_target_properties(ironphoenix PROPERTIES
    OUTPUT_NAME "ironphoenix"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ironphoenix>"
)

if(MSVC)
    target_compile_options(ironphoenix PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/Oi>
        $<$<CONFIG:RelWithDebInfo>:/Ot>
    )
else()
    target_compile_options(ironphoenix PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
    )
endif()

if(IRONPHOENIX_IPO_SUPPORTED)
    set_property(TARGET ironphoenix PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET ironphoenix PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

if(IRONPHOENIX_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(ironphoenix_smoke tests/smoke.cpp)
    target_link_libraries(ironphoenix_smoke PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_smoke PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_smoke COMMAND ironphoenix_smoke)

    add_executable(ironphoenix_property tests/property.cpp)
    target_link_libraries(ironphoenix_property PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_property PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_property COMMAND ironphoenix_property)

    set_target_properties(ironphoenix_smoke ironphoenix_property PROPERTIES
        FOLDER "Tests"
    )
endif()

if(IRONPHOENIX_BUILD_BENCHMARK)
    add_executable(ironphoenix_bench tests/bench.cpp)
    target_link_libraries(ironphoenix_bench PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_bench PRIVATE cxx_std_20)
    set_target_properties(ironphoenix_bench PROPERTIES FOLDER "Benchmarks")
endif()
