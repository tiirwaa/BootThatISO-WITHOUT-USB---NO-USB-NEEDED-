cmake_minimum_required(VERSION 3.16)
project(BootThatISO LANGUAGES CXX RC)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE)

set(SOURCE_FILES
    src/main.cpp
    src/controllers/ProcessController.cpp
    src/controllers/ProcessService.cpp
    src/models/efimanager.cpp
    src/models/filecopymanager.cpp
    src/models/isomounter.cpp
    src/services/bcdmanager.cpp
    src/services/isocopymanager.cpp
    src/services/isotypedetector.cpp    src/services/partitionmanager.cpp
        src/utils/Logger.cpp
        src/utils/LocalizationManager.cpp
        src/utils/Utils.cpp
        src/views/mainwindow.cpp
)

set(RESOURCE_FILES
    ${CMAKE_BINARY_DIR}/BootThatISO.rc
)

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

# Parse version into components
string(REPLACE "." ";" VERSION_LIST ${APP_VERSION})
list(GET VERSION_LIST 0 VERSION_MAJOR)
list(GET VERSION_LIST 1 VERSION_MINOR)
list(GET VERSION_LIST 2 VERSION_PATCH)

configure_file(
    ${CMAKE_SOURCE_DIR}/include/version.h.in
    ${CMAKE_BINARY_DIR}/version.h
)

configure_file(
    ${CMAKE_SOURCE_DIR}/src/BootThatISO.rc.in
    ${CMAKE_BINARY_DIR}/BootThatISO.rc
)

set(CMAKE_MFC_FLAG 2)

add_executable(BootThatISO WIN32 ${SOURCE_FILES} ${RESOURCE_FILES})

target_include_directories(BootThatISO
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
)

if(MSVC)
    target_compile_options(BootThatISO PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus $<$<CONFIG:Release>:/MT> $<$<CONFIG:Debug>:/MTd>)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(BootThatISO PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# Link necessary libraries (MFC is included via CMAKE_MFC_FLAG)
target_link_libraries(BootThatISO comctl32 gdiplus)

set_target_properties(BootThatISO PROPERTIES OUTPUT_NAME "BootThatISO!")

if(MSVC)
    target_link_options(BootThatISO PRIVATE "/MANIFEST:NO")
endif()

add_executable(UtilsTests
    tests/utils_tests.cpp
    src/utils/Utils.cpp
)

target_include_directories(UtilsTests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(UtilsTests PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
else()
    target_compile_options(UtilsTests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(WIN32)
    target_link_libraries(UtilsTests PRIVATE advapi32)
endif()

add_test(NAME UtilsTests COMMAND $<TARGET_FILE:UtilsTests>)

add_executable(TestRecoverSpace
    tests/test_recover_space.cpp
    src/services/partitionmanager.cpp
    src/utils/Utils.cpp
    src/utils/LocalizationManager.cpp
    src/utils/Logger.cpp
)

target_include_directories(TestRecoverSpace
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)

if(MSVC)
    target_compile_options(TestRecoverSpace PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
else()
    target_compile_options(TestRecoverSpace PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(WIN32)
    target_link_libraries(TestRecoverSpace PRIVATE advapi32 comctl32 gdiplus)
endif()

find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    set_target_properties(BootThatISO PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-warnings-as-errors=*")
endif()

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    set(FORMAT_FILES
        ${SOURCE_FILES}
        src/controllers/ProcessController.h
        src/models/EventManager.h
        src/models/EventObserver.h
        src/views/mainwindow.h
        src/utils/Logger.h
        src/utils/Utils.h
        src/utils/LocalizationManager.h\n        include/models/HashInfo.h
        tests/utils_tests.cpp
    )
    add_custom_target(check-format
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${FORMAT_FILES}
        COMMENT "Checking source formatting with clang-format"
        VERBATIM
    )
else()
    add_custom_target(check-format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found; skipping format check"
        VERBATIM
    )
endif()

add_executable(test_ini_replacer test_ini_replacer.cpp)




