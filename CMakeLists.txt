cmake_minimum_required(VERSION 3.20)
project(iron_phoenix LANGUAGES CXX)

add_library(iron_phoenix STATIC
    src/geometry.cpp
    src/zobrist.cpp
    src/position.cpp
    src/see.cpp
)

target_include_directories(iron_phoenix PUBLIC include)
target_compile_features(iron_phoenix PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(iron_phoenix PRIVATE
        /permissive-
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:RelWithDebInfo>:/Oi>
        $<$<CONFIG:RelWithDebInfo>:/Ot>
    )
else()
    target_compile_options(iron_phoenix PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-march=native>
        $<$<CONFIG:RelWithDebInfo>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-march=native>
    )
endif()

add_executable(iron_phoenix_smoke tests/smoke.cpp)
target_link_libraries(iron_phoenix_smoke PRIVATE iron_phoenix)

add_executable(iron_phoenix_bench tests/bench.cpp)
target_link_libraries(iron_phoenix_bench PRIVATE iron_phoenix)

add_executable(iron_phoenix_property tests/property.cpp)
target_link_libraries(iron_phoenix_property PRIVATE iron_phoenix)

include(CheckIPOSupported)
check_ipo_supported(RESULT IRONPHOENIX_IPO_SUPPORTED OUTPUT IRONPHOENIX_IPO_ERROR)
if (IRONPHOENIX_IPO_SUPPORTED)
    foreach(tgt
        iron_phoenix
        iron_phoenix_smoke
        iron_phoenix_bench
        iron_phoenix_property)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    endforeach()
endif()
