cmake_minimum_required(VERSION 3.22)

# VFXAamps — NAM amp head with a 10-slot morphing gain knob and a dual IR loader.
# Self-contained app: cmake -S . -B build.
project(VFXAampsHead VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE) # universal binary
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

option(VFXA_BUILD_PLUGIN "Build the JUCE plugin + standalone" ON)
option(VFXA_BUILD_TESTS  "Build the DSP and plugin tests"     ON)

include(FetchContent)

# --- NeuralAmpModelerCore (git submodule) ----------------------------------------------
set(VFXA_NAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/NeuralAmpModelerCore" CACHE PATH "NeuralAmpModelerCore checkout")
if(NOT EXISTS "${VFXA_NAM_DIR}/NAM/dsp.h")
    message(FATAL_ERROR "NeuralAmpModelerCore not found. Run: git submodule update --init")
endif()

# --- Eigen 3.4.0 (header only) ----------------------------------------------------------
if(DEFINED EIGEN_DIR AND EXISTS "${EIGEN_DIR}/Eigen/Dense")
    set(VFXA_EIGEN_DIR "${EIGEN_DIR}")
elseif(EXISTS "${VFXA_NAM_DIR}/Dependencies/eigen/Eigen/Dense")
    set(VFXA_EIGEN_DIR "${VFXA_NAM_DIR}/Dependencies/eigen")
else()
    FetchContent_Declare(eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG        3.4.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  vfxa-populate-only)
    FetchContent_MakeAvailable(eigen)
    set(VFXA_EIGEN_DIR "${eigen_SOURCE_DIR}")
endif()

# --- DSP core: JUCE-free, includes the NAM engine ---------------------------------------
file(GLOB VFXA_NAM_SOURCES CONFIGURE_DEPENDS
    "${VFXA_NAM_DIR}/NAM/*.cpp"
    "${VFXA_NAM_DIR}/NAM/wavenet/*.cpp")

# OBJECT library so NAM's self-registering architectures are never dropped by the linker.
add_library(vfxa_dsp OBJECT dsp/src/MorphEngine.cpp ${VFXA_NAM_SOURCES})
target_include_directories(vfxa_dsp
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/dsp/include
    PRIVATE ${VFXA_NAM_DIR} ${VFXA_NAM_DIR}/NAM ${VFXA_NAM_DIR}/Dependencies/nlohmann)
target_include_directories(vfxa_dsp SYSTEM PRIVATE ${VFXA_EIGEN_DIR})
target_compile_definitions(vfxa_dsp PRIVATE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST EIGEN_MPL2_ONLY)
# Keep NAM / Eigen / nlohmann private to our binary. Exported, their inline functions and statics (e.g. NAM's
# architecture registry) get shared on macOS with any other loaded plug-in that also exports them (the official NAM
# plug-in, other NAM-based plug-ins): we would end up running their, differently built, NAM code -> crash in the DAW.
set_target_properties(vfxa_dsp PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
    target_compile_options(vfxa_dsp PRIVATE /bigobj /W3 /fp:fast)
    target_compile_definitions(vfxa_dsp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
else()
    target_compile_options(vfxa_dsp PRIVATE -O3)
    set_source_files_properties(${VFXA_NAM_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    set_source_files_properties(dsp/src/MorphEngine.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()

if(VFXA_BUILD_TESTS)
    enable_testing()
    add_executable(vfxa_dsp_tests tests/dsp_tests.cpp)
    target_link_libraries(vfxa_dsp_tests PRIVATE vfxa_dsp)
    target_compile_definitions(vfxa_dsp_tests PRIVATE VFXA_EXAMPLE_MODELS="${VFXA_NAM_DIR}/example_models")
    add_test(NAME dsp_tests COMMAND vfxa_dsp_tests)
endif()

# --- JUCE plugin ------------------------------------------------------------------------
if(VFXA_BUILD_PLUGIN)
    if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
        add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.15
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(juce)
    endif()
    add_subdirectory(source)
endif()
