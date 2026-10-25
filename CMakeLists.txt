cmake_minimum_required(VERSION 3.24)
# The app version (About box, CFBundleShortVersionString). Bump it here when tagging a release.
project(GraphingCalc VERSION 1.3.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# M3 MacBook Air baseline. Never -ffast-math: NaN/inf drive gap & asymptote detection.
set(OPT_FLAGS -O3 -mcpu=apple-m3 -fno-fast-math -ffp-contract=on)
set(WARN_FLAGS -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(OPT_FLAGS -O0 -g)
else()
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)  # -flto
endif()

set(METAL_CPP ${CMAKE_SOURCE_DIR}/third_party/metal-cpp)

# ---- core library: everything that doesn't need a window ---------------------------------------
file(GLOB MATH_SRC CONFIGURE_DEPENDS  ${CMAKE_SOURCE_DIR}/src/math/*.cpp)
file(GLOB GRAPH_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/graph/*.cpp)
file(GLOB RENDER_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/render/*.cpp)
file(GLOB UI_SRC CONFIGURE_DEPENDS    ${CMAKE_SOURCE_DIR}/src/ui/*.cpp)
file(GLOB GPU_SRC CONFIGURE_DEPENDS   ${CMAKE_SOURCE_DIR}/src/gpu/*.cpp ${CMAKE_SOURCE_DIR}/src/gpu/*.mm)
file(GLOB APP_CORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/app/*.cpp)

add_library(calc_core STATIC ${MATH_SRC} ${GRAPH_SRC} ${RENDER_SRC} ${UI_SRC} ${GPU_SRC} ${APP_CORE_SRC})
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/gpu/MpsBridge.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
target_include_directories(calc_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${METAL_CPP})
target_compile_options(calc_core PUBLIC ${OPT_FLAGS} ${WARN_FLAGS})
target_link_libraries(calc_core PUBLIC
    "-framework Foundation" "-framework Metal" "-framework MetalPerformanceShaders"
    "-framework QuartzCore" "-framework CoreText" "-framework CoreGraphics"
    "-framework ImageIO" "-framework CoreFoundation")

# ---- shaders -> default.metallib (offline, so launch never waits on the shader compiler) --------
file(GLOB SHADER_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.metal)
set(AIR_FILES)
foreach(src ${SHADER_SRC})
    get_filename_component(name ${src} NAME_WE)
    set(air ${CMAKE_BINARY_DIR}/shaders/${name}.air)
    add_custom_command(OUTPUT ${air}
        COMMAND xcrun -sdk macosx metal -std=metal3.1 -O3 -fno-fast-math
                -I ${CMAKE_SOURCE_DIR}/shaders -I ${CMAKE_SOURCE_DIR}/src -c ${src} -o ${air}
        DEPENDS ${src} ${CMAKE_SOURCE_DIR}/src/math/Bytecode.hpp ${CMAKE_SOURCE_DIR}/src/render/ShaderTypes.hpp
        COMMENT "metal ${name}.metal" VERBATIM)
    list(APPEND AIR_FILES ${air})
endforeach()
set(METALLIB ${CMAKE_BINARY_DIR}/default.metallib)
add_custom_command(OUTPUT ${METALLIB}
    COMMAND xcrun -sdk macosx metallib ${AIR_FILES} -o ${METALLIB}
    DEPENDS ${AIR_FILES} COMMENT "metallib" VERBATIM)
add_custom_target(shaders DEPENDS ${METALLIB})

# ---- app bundle ---------------------------------------------------------------------------------
add_executable(GraphingCalc MACOSX_BUNDLE ${CMAKE_SOURCE_DIR}/src/app/Platform.mm ${METALLIB})
set_source_files_properties(${METALLIB} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_target_properties(GraphingCalc PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME GraphingCalc
    MACOSX_BUNDLE_GUI_IDENTIFIER au.com.amproductions.graphingcalc
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
target_compile_options(GraphingCalc PRIVATE -fobjc-arc)
target_link_libraries(GraphingCalc PRIVATE calc_core "-framework AppKit" "-framework UniformTypeIdentifiers")
add_dependencies(GraphingCalc shaders)

# ---- tests & bench (headless) -------------------------------------------------------------------
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)
add_executable(calc_tests ${CMAKE_SOURCE_DIR}/tests/main.cpp ${TEST_SRC})
target_link_libraries(calc_tests PRIVATE calc_core)
target_compile_definitions(calc_tests PRIVATE METALLIB_PATH="${METALLIB}" FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_dependencies(calc_tests shaders)

add_executable(calc_bench ${CMAKE_SOURCE_DIR}/tests/bench.cpp)
target_link_libraries(calc_bench PRIVATE calc_core)
target_compile_definitions(calc_bench PRIVATE METALLIB_PATH="${METALLIB}" FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_dependencies(calc_bench shaders)
