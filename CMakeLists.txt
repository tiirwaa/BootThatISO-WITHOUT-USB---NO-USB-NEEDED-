cmake_minimum_required(VERSION 3.10)
project(EasyISOBoot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find Qt5
find_package(Qt5 COMPONENTS Widgets REQUIRED)

# Enable Qt5 automoc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Include directories
include_directories(include)

# Source files
file(GLOB SOURCES "src/*.cpp" "src/*.h")

# Executable
add_executable(EasyISOBoot WIN32 ${SOURCES})

# Link Qt5 libraries
target_link_libraries(EasyISOBoot Qt5::Widgets)