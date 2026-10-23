cmake_minimum_required(VERSION 3.24)

project(IronPhoenix VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(IRONPHOENIX_BUILD_TESTS "Build IronPhoenix correctness tests" ON)
option(IRONPHOENIX_BUILD_BENCHMARK "Build IronPhoenix microbenchmark" ON)
option(IRONPHOENIX_BUILD_DATASET_GENERATOR "Build PhoenixNet dataset generator" ON)
option(IRONPHOENIX_NATIVE_CPU "Optimize Release/RelWithDebInfo for the build host CPU; nonportable binaries" OFF)

add_library(ironphoenix_core STATIC
    src/geometry.cpp
    src/zobrist.cpp
    src/position.cpp
    src/see.cpp
    src/history.cpp
    src/lmr.cpp
    src/tt.cpp
    src/eval.cpp
    src/bishop_eval.cpp
    src/king_eval.cpp
    src/nnue.cpp
    src/tuning.cpp
    src/search.cpp
    src/multipv.cpp
    src/fen.cpp
    src/movegen.cpp
    src/preft.cpp
    src/uci.cpp
    include/ironphoenix/eval.hpp
    include/ironphoenix/bishop_eval.hpp
    include/ironphoenix/king_eval.hpp
    include/ironphoenix/search_stack.hpp
    include/ironphoenix/lmr.hpp
    include/ironphoenix/nnue.hpp
    include/ironphoenix/tuning.hpp
)

add_library(IronPhoenix::Core ALIAS ironphoenix_core)

target_include_directories(ironphoenix_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(ironphoenix_core PUBLIC cxx_std_20)
target_link_libraries(ironphoenix_core PUBLIC Threads::Threads)

# Only enable for a binary that will run on the same CPU generation used to
# build it. Keep CI/distribution generic so older systems are not exposed to
# unsupported instructions. PUBLIC propagates ISA flags to tests/apps because
# Bitboard intrinsics are also header-inlined.
if(IRONPHOENIX_NATIVE_CPU)
    if(MSVC)
        target_compile_options(ironphoenix_core PUBLIC
            $<$<CONFIG:Release>:/arch:AVX2>
            $<$<CONFIG:RelWithDebInfo>:/arch:AVX2>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(ironphoenix_core PUBLIC
            $<$<CONFIG:Release>:-march=native>
            $<$<CONFIG:RelWithDebInfo>:-march=native>
        )
    endif()
endif()

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
check_ipo_supported(
    RESULT IRONPHOENIX_IPO_SUPPORTED
    OUTPUT IRONPHOENIX_IPO_ERROR
)

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

if(IRONPHOENIX_BUILD_DATASET_GENERATOR)
    add_executable(ironphoenix_dataset
        tools/nnue_dataset_resumable.cpp
    )
    target_link_libraries(ironphoenix_dataset PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_dataset PRIVATE cxx_std_20)
    set_target_properties(ironphoenix_dataset PROPERTIES
        OUTPUT_NAME "ironphoenix_dataset"
        FOLDER "Tools"
    )

    if(MSVC)
        target_compile_options(ironphoenix_dataset PRIVATE
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
        target_compile_options(ironphoenix_dataset PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:RelWithDebInfo>:-O3>
        )
    endif()

    if(IRONPHOENIX_IPO_SUPPORTED)
        set_property(TARGET ironphoenix_dataset PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET ironphoenix_dataset PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    endif()
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

    add_executable(ironphoenix_search_stack tests/search_stack.cpp)
    target_link_libraries(ironphoenix_search_stack PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_search_stack PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_search_stack COMMAND ironphoenix_search_stack)

    add_executable(ironphoenix_lazy_smp tests/lazy_smp.cpp)
    target_link_libraries(ironphoenix_lazy_smp PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_lazy_smp PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_lazy_smp COMMAND ironphoenix_lazy_smp)
    set_target_properties(ironphoenix_lazy_smp PROPERTIES FOLDER "Tests")

    add_executable(ironphoenix_mate_stop tests/mate_stop.cpp)
    target_link_libraries(ironphoenix_mate_stop PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_mate_stop PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_mate_stop COMMAND ironphoenix_mate_stop)

    add_executable(ironphoenix_reverse_futility tests/reverse_futility.cpp)
    target_link_libraries(ironphoenix_reverse_futility PRIVATE IronPhoenix::Core)
    add_test(NAME ironphoenix_reverse_futility COMMAND ironphoenix_reverse_futility)

    add_executable(ironphoenix_attacking_pieces tests/attacking_pieces.cpp)
    target_link_libraries(ironphoenix_attacking_pieces PRIVATE IronPhoenix::Core)
    add_test(NAME ironphoenix_attacking_pieces COMMAND ironphoenix_attacking_pieces)

    add_executable(ironphoenix_knight_check_threat tests/knight_check_threat.cpp)
    target_link_libraries(ironphoenix_knight_check_threat PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_knight_check_threat PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_knight_check_threat COMMAND ironphoenix_knight_check_threat)

    add_executable(ironphoenix_knight_activity tests/knight_activity.cpp)
    target_link_libraries(ironphoenix_knight_activity PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_knight_activity PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_knight_activity COMMAND ironphoenix_knight_activity)

    add_executable(ironphoenix_knight_outpost tests/knight_outpost.cpp)
    target_link_libraries(ironphoenix_knight_outpost PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_knight_outpost PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_knight_outpost COMMAND ironphoenix_knight_outpost)

    add_executable(ironphoenix_check_order tests/check_order.cpp)
    target_link_libraries(ironphoenix_check_order PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_check_order PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_check_order COMMAND ironphoenix_check_order)
    set_target_properties(ironphoenix_check_order PROPERTIES FOLDER "Tests")

    add_executable(ironphoenix_eval_cache tests/eval_cache.cpp)
    target_link_libraries(ironphoenix_eval_cache PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_eval_cache PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_eval_cache COMMAND ironphoenix_eval_cache)
    set_target_properties(ironphoenix_eval_cache PROPERTIES FOLDER "Tests")

    add_executable(ironphoenix_king_features tests/king_features.cpp)
    target_link_libraries(ironphoenix_king_features PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_king_features PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_king_features COMMAND ironphoenix_king_features)
    set_target_properties(ironphoenix_king_features PROPERTIES FOLDER "Tests")

    add_executable(ironphoenix_bishop_features tests/bishop_features.cpp)
    target_link_libraries(ironphoenix_bishop_features PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_bishop_features PRIVATE cxx_std_20)
    add_test(NAME ironphoenix_bishop_features COMMAND ironphoenix_bishop_features)

    set_target_properties(ironphoenix_bishop_features PROPERTIES FOLDER "Tests")

    set_target_properties(ironphoenix_smoke ironphoenix_property ironphoenix_search_stack ironphoenix_mate_stop ironphoenix_reverse_futility ironphoenix_attacking_pieces ironphoenix_knight_check_threat ironphoenix_knight_activity ironphoenix_knight_outpost PROPERTIES
        FOLDER "Tests"
    )
endif()

if(IRONPHOENIX_BUILD_BENCHMARK)
    add_executable(ironphoenix_bench tests/bench.cpp)
    target_link_libraries(ironphoenix_bench PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_bench PRIVATE cxx_std_20)
    set_target_properties(ironphoenix_bench PROPERTIES FOLDER "Benchmarks")

    add_executable(ironphoenix_nps_bench tests/nps_bench.cpp)
    target_link_libraries(ironphoenix_nps_bench PRIVATE IronPhoenix::Core)
    target_compile_features(ironphoenix_nps_bench PRIVATE cxx_std_20)
    set_target_properties(ironphoenix_nps_bench PROPERTIES FOLDER "Benchmarks")
endif()
