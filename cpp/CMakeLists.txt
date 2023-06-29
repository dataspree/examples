cmake_policy(SET CMP0048 NEW)
cmake_minimum_required(VERSION 3.15...3.23)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE ON)

# Used to avoid conflicts f.i., between -Wpedantic being enabled and -std=c++20 and -std=gnu++20 when compiling with
# precompiled header files enabled
set(CMAKE_CXX_EXTENSIONS OFF)

# If true, all analysis (sanitizers, static analysis) is enabled
set(ENABLE_DEVELOPER_MODE OFF CACHE BOOL "Enable 'developer mode'")

#
# If true, warnings are treated as errors in DEVELOPER_MODE
set(OPT_WARNINGS_AS_ERRORS_DEVELOPER_DEFAULT TRUE)

# Add project_options v0.20.0
# https://github.com/cpp-best-practices/project_options
include(FetchContent)
FetchContent_Declare(_project_options
        URL https://github.com/aminya/project_options/archive/refs/tags/v0.20.0.zip DOWNLOAD_EXTRACT_TIMESTAMP true)
FetchContent_MakeAvailable(_project_options)
include(${_project_options_SOURCE_DIR}/Index.cmake)

set(ENABLE_VCPKG FALSE CACHE BOOL "Enable packaging via VCPKG")
if (ENABLE_VCPKG)
    run_vcpkg()
endif ()

# Set the project name and language
project(Dataspree_Cpp VERSION 0.0.1 DESCRIPTION "" HOMEPAGE_URL "https://www.data-spree.com/" LANGUAGES CXX C)
include(GNUInstallDirs)

# This variable is set by project() in CMake 3.21+
string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" ".." PROJECT_IS_TOP_LEVEL)
if (PROJECT_IS_TOP_LEVEL)
    # Consider the CTest module, which creates targets and options!
    # Only needed if you want to enable submissions to a CDash server.
    include(CTest)
endif ()

set(GIT_SHA "Unknown" CACHE STRING "SHA this build was generated from")
string(SUBSTRING "${GIT_SHA}" 0 8 GIT_SHORT_SHA)

get_property(BUILDING_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (BUILDING_MULTI_CONFIG)
    if (NOT CMAKE_BUILD_TYPE)
        # Make sure that all supported configuration types have their
        # associated conan packages available. You can reduce this
        # list to only the configuration types you use, but only if one
        # is not forced-set on the command line for VS
        message(TRACE "Setting up multi-config build types")
        set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel CACHE STRING "Enabled build types" FORCE)
    else ()
        message(TRACE "User chose a specific build type, so we are using that")
        set(CMAKE_CONFIGURATION_TYPES
                ${CMAKE_BUILD_TYPE}
                CACHE STRING "Enabled build types" FORCE)
    endif ()
endif ()

include(${_project_options_SOURCE_DIR}/src/DynamicProjectOptions.cmake)

# Initialize project_options variable related to this project
# This overwrites `project_options` and sets `project_warnings`
# uncomment the options to enable them:
dynamic_project_options(
        # Note: PCH is disabled by default in developer mode because these headers become
        # globally included and they can mask other errors
        PCH_HEADERS
        <vector>
        <string> # This is a list of headers to pre-compile, here are some common ones
        ENABLE_CONAN
        CONAN_OPTIONS    # Extra options to pass to conan
        # MSVC_WARNINGS    # Override the defaults for the MSVC warnings
        # CLANG_WARNINGS   # Override the defaults for the CLANG warnings
        # GCC_WARNINGS     # Override the defaults for the GCC warnings
        CPPCHECK_OPTIONS
        --enable=style,performance,warning,portability
        --inline-suppr
        # We cannot act on a bug/missing feature of cppcheck
        --suppress=cppcheckError
        --suppress=internalAstError
        # if a file does not have an internalAstError, we get an unmatchedSuppression error
        --suppress=unmatchedSuppression
        --suppress=passedByValue
        --suppress=syntaxError
        --inconclusive
)
message("${CLANG_WARNINGS} currently enabled")
target_compile_features(project_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
add_library(DataspreeCpp::project_options INTERFACE IMPORTED inference/src/dataspree/inference/core/Exception.cpp examples/tcp_tutorial/main.cpp)
add_library(DataspreeCpp::project_warnings INTERFACE IMPORTED)

add_subdirectory(inference)

add_subdirectory(examples)

# Adding the tests:
option(ENABLE_TESTING "Enable the tests" ${PROJECT_IS_TOP_LEVEL})
if (ENABLE_TESTING)
    enable_testing()
    message(AUTHOR_WARNING "Building Tests")
    add_subdirectory(test)
endif ()

option(ENABLE_EXAMPLES "Build examples " ${PROJECT_IS_TOP_LEVEL})
if (ENABLE_TESTING)
    message(AUTHOR_WARNING "Building examples")
    add_subdirectory(examples)
endif ()

option(ENABLE_FUZZING "Enable the fuzz tests" OFF)
if (ENABLE_FUZZING)
    message(ERROR "Building Fuzz Tests, using fuzzing sanitizer https://www.llvm.org/docs/LibFuzzer.html - Currently not implemented")
    add_subdirectory(fuzz_test)
endif ()

# If MSVC is being used, and ASAN is enabled, we need to set the debugger environment
# so that it behaves well with MSVC's debugger, and we can run the target from visual studio
if (MSVC)
    get_all_installable_targets(all_targets)
    message("all_targets=${all_targets}")
    set_target_properties(${all_targets} PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$(VC_ExecutablePath_x64);%PATH%")
endif ()

# set the startup project for the "play" button in MSVC
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT intro)

if (CMAKE_SKIP_INSTALL_RULES)
    return()
elseif (NOT PROJECT_IS_TOP_LEVEL)
    return()
endif ()

# Installable targets:
package_project(TARGETS intro project_options project_warnings)

# Experience shows that explicit package naming can help make it easier to sort out potential ABI related issues
# before they start, while helping you track a build to a specific GIT SHA
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${GIT_SHORT_SHA}-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

include(CPack)
