cmake_minimum_required(VERSION 3.10)
project(BootThatISO)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable Unicode support
add_definitions(-DUNICODE -D_UNICODE)

# Set UTF-8 source encoding for MSVC
if(MSVC)
    add_compile_options(/utf-8)
    # Use static runtime library
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Get version from git tag
execute_process(
    COMMAND git describe --tags --abbrev=0
    OUTPUT_VARIABLE GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_TAG)
    set(GIT_TAG "v1.0.0")
endif()

# Remove 'v' prefix for display
string(SUBSTRING ${GIT_TAG} 1 -1 APP_VERSION)

# Configure version header
configure_file(
    ${CMAKE_SOURCE_DIR}/include/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
)

# Include directories
include_directories(include ${CMAKE_BINARY_DIR})

# Source files
file(GLOB_RECURSE SOURCES "src/*.cpp")
set(SOURCES ${SOURCES} src/BootThatISO.rc)

# Executable
add_executable(BootThatISO WIN32 ${SOURCES} src/BootThatISO.rc)

# Link necessary libraries (MFC is included via CMAKE_MFC_FLAG)
target_link_libraries(BootThatISO comctl32)
set(CMAKE_MFC_FLAG 1)

set_target_properties(BootThatISO PROPERTIES OUTPUT_NAME "BootThatISO!")
