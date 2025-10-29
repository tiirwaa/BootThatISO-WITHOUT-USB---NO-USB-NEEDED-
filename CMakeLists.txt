cmake_minimum_required(VERSION 3.10)
project(EasyISOBoot)

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

# Include directories
include_directories(include)

# Source files
file(GLOB SOURCES "src/*.cpp" "src/*.h")

# Executable
add_executable(EasyISOBoot WIN32 ${SOURCES})

# Link necessary libraries (MFC is included via CMAKE_MFC_FLAG)
set(CMAKE_MFC_FLAG 1)