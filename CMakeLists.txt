cmake_minimum_required(VERSION 3.16)
project(BootThatISO LANGUAGES C CXX RC)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE _7ZIP_ST)

# 7z SDK (minimal subset for ISO reading)
set(7Z_SDK_C_FILES
    third-party/C/7zCrc.c
    third-party/C/Alloc.c
    third-party/C/7zCrcOpt.c
)

set(7Z_SDK_CPP_FILES
    # Common utilities
    third-party/CPP/Common/MyWindows.cpp
    third-party/CPP/Common/MyString.cpp
    third-party/CPP/Common/IntToString.cpp
    third-party/CPP/Common/StringConvert.cpp
    third-party/CPP/Common/UTFConvert.cpp
    third-party/CPP/Common/CRC.cpp
    third-party/CPP/Common/MyMap.cpp

    # Windows wrappers
    third-party/CPP/Windows/FileIO.cpp
    third-party/CPP/Windows/FileName.cpp
    third-party/CPP/Windows/FileDir.cpp
    third-party/CPP/Windows/FileFind.cpp
    third-party/CPP/Windows/PropVariant.cpp
    third-party/CPP/Windows/System.cpp
    third-party/CPP/Windows/TimeUtils.cpp
    third-party/CPP/Windows/PropVariantUtils.cpp

    # 7zip common stream helpers
    third-party/CPP/7zip/Common/FileStreams.cpp
    third-party/CPP/7zip/Common/InBuffer.cpp
    third-party/CPP/7zip/Common/LimitedStreams.cpp
    third-party/CPP/7zip/Common/OutBuffer.cpp
    third-party/CPP/7zip/Common/ProgressUtils.cpp
    third-party/CPP/7zip/Common/StreamUtils.cpp
    third-party/CPP/7zip/Common/PropId.cpp
    third-party/CPP/7zip/Common/StreamObjects.cpp

    # Archive exports and ISO handler
    third-party/CPP/7zip/Archive/Common/ItemNameUtils.cpp
    third-party/CPP/7zip/Archive/ExtHandler.cpp
    third-party/CPP/7zip/Archive/HandlerCont.cpp
    third-party/CPP/7zip/Archive/MbrHandler.cpp
    third-party/CPP/7zip/Archive/ArchiveExports.cpp
    third-party/CPP/7zip/Archive/Iso/IsoHandler.cpp
    third-party/CPP/7zip/Archive/Iso/IsoHeader.cpp
    third-party/CPP/7zip/Archive/Iso/IsoIn.cpp
    third-party/CPP/7zip/Archive/Iso/IsoRegister.cpp
    # UDF handler (Windows ISOs are UDF)
    third-party/CPP/7zip/Archive/Udf/UdfIn.cpp
    third-party/CPP/7zip/Archive/Udf/UdfHandler.cpp
    
    # Basic copy coder used by some helpers
    third-party/CPP/7zip/Compress/CopyCoder.cpp
    third-party/CPP/7zip/Compress/LzOutWindow.cpp
    
    # Ext handler dependencies
    third-party/CPP/7zip/Archive/LzhHandler.cpp
    third-party/CPP/7zip/Compress/LzhDecoder.cpp
    
    # Our GUID definitions for 7-Zip interfaces
    src/SevenZipGuids.cpp
)

add_library(sevenzip STATIC ${7Z_SDK_C_FILES} ${7Z_SDK_CPP_FILES})

target_include_directories(sevenzip
    PUBLIC
        ${CMAKE_SOURCE_DIR}/third-party/CPP
        ${CMAKE_SOURCE_DIR}/third-party/CPP/7zip
        ${CMAKE_SOURCE_DIR}/third-party/CPP/Windows
        ${CMAKE_SOURCE_DIR}/third-party/CPP/Common
        ${CMAKE_SOURCE_DIR}/third-party/C
)

# Ensure sevenzip uses the same runtime as the rest (/MT for Release)
if(MSVC)
    target_compile_options(sevenzip PRIVATE /MP $<$<CONFIG:Release>:/MT /O2 /arch:AVX2 /fp:fast /GL> $<$<CONFIG:Debug>:/MTd>)
    set_target_properties(sevenzip PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(SOURCE_FILES
    src/main.cpp
    src/controllers/ProcessController.cpp
    src/controllers/ProcessService.cpp
    src/models/efimanager.cpp
    src/models/filecopymanager.cpp
    src/models/isomounter.cpp
    src/models/ISOReader.cpp
    src/models/IniConfigurator.cpp
    src/models/ContentExtractor.cpp
    src/models/HashVerifier.cpp
    src/models/DiskIntegrityChecker.cpp
    src/models/VolumeDetector.cpp
    src/models/VolumeDetectionStrategy.cpp
    src/models/SpaceManager.cpp
    src/models/RecoverySteps.cpp
    src/models/DiskpartExecutor.cpp
    src/models/PartitionReformatter.cpp
    src/models/PartitionCreator.cpp
    src/services/bcdmanager.cpp
    src/services/isocopymanager.cpp
    src/services/isotypedetector.cpp
    src/services/partitionmanager.cpp
    src/services/DiskLogger.cpp
    src/utils/Logger.cpp
    src/utils/LocalizationManager.cpp
    src/utils/Utils.cpp
    src/views/mainwindow.cpp
    src/views/EditionSelectorDialog.cpp
    # Refactored boot processing modules
    src/boot/BootWimProcessor.cpp
    src/wim/WimMounter.cpp
    src/wim/WindowsEditionSelector.cpp
    src/drivers/DriverIntegrator.cpp
    src/config/PecmdConfigurator.cpp
    src/config/StartnetConfigurator.cpp
    src/config/IniFileProcessor.cpp
    src/filesystem/ProgramsIntegrator.cpp
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

add_executable(BootThatISO ${SOURCE_FILES} ${RESOURCE_FILES})

target_include_directories(BootThatISO
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/third-party/CPP
    ${CMAKE_SOURCE_DIR}/third-party/CPP/7zip
    ${CMAKE_SOURCE_DIR}/third-party/CPP/Windows
    ${CMAKE_SOURCE_DIR}/third-party/CPP/Common
)

if(MSVC)
    target_compile_options(BootThatISO PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus /MP $<$<CONFIG:Release>:/MT /O2 /arch:AVX2 /fp:fast /GL> $<$<CONFIG:Debug>:/MTd>)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_target_properties(BootThatISO PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(BootThatISO PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# Link necessary libraries (MFC is included via CMAKE_MFC_FLAG)
target_link_libraries(BootThatISO comctl32 gdiplus sevenzip virtdisk.lib)

set_target_properties(BootThatISO PROPERTIES OUTPUT_NAME "BootThatISO!")

if(MSVC)
    target_link_options(BootThatISO PRIVATE "/MANIFEST:NO" "/WHOLEARCHIVE:sevenzip" $<$<CONFIG:Release>:/LTCG>)
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
    set_target_properties(UtilsTests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
    src/services/bcdmanager.cpp
    src/models/DiskIntegrityChecker.cpp
    src/models/VolumeDetector.cpp
    src/models/VolumeDetectionStrategy.cpp
    src/models/SpaceManager.cpp
    src/models/RecoverySteps.cpp
    src/models/DiskpartExecutor.cpp
    src/models/PartitionReformatter.cpp
    src/models/PartitionCreator.cpp
    src/services/DiskLogger.cpp
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
    set_target_properties(TestRecoverSpace PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(TestRecoverSpace PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(WIN32)
    target_link_libraries(TestRecoverSpace PRIVATE advapi32 comctl32 gdiplus virtdisk.lib)
endif()

add_executable(TestArgs
    src/test_args.cpp
)

target_include_directories(TestArgs
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(TestArgs PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
    set_target_properties(TestArgs PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(TestPartitionStatus
    tests/test_partition_status.cpp
    src/services/partitionmanager.cpp
    src/services/bcdmanager.cpp
    src/models/DiskIntegrityChecker.cpp
    src/models/VolumeDetector.cpp
    src/models/VolumeDetectionStrategy.cpp
    src/models/SpaceManager.cpp
    src/models/RecoverySteps.cpp
    src/models/DiskpartExecutor.cpp
    src/models/PartitionReformatter.cpp
    src/models/PartitionCreator.cpp
    src/services/DiskLogger.cpp
    src/utils/Utils.cpp
    src/utils/LocalizationManager.cpp
    src/utils/Logger.cpp
)

target_include_directories(TestPartitionStatus
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)

if(MSVC)
    target_compile_options(TestPartitionStatus PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
    set_target_properties(TestPartitionStatus PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(TestPartitionStatus PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(WIN32)
    target_link_libraries(TestPartitionStatus PRIVATE advapi32 comctl32 gdiplus virtdisk.lib)
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
        src/models/IniConfigurator.h
        src/boot/BootWimProcessor.h
        src/models/ContentExtractor.h
        src/models/HashVerifier.h
        src/views/mainwindow.h
        src/utils/Logger.h
        src/utils/Utils.h
        src/utils/LocalizationManager.h
        include/models/HashInfo.h
        tests/utils_tests.cpp
    )
    add_custom_target(check-format
        COMMAND cd ${CMAKE_SOURCE_DIR} && ${CLANG_FORMAT_EXE} --dry-run --Werror ${FORMAT_FILES}
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

add_executable(TestISOReader
    test_iso_reader.cpp
    src/models/ISOReader.cpp
    src/utils/Utils.cpp
)

target_include_directories(TestISOReader
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/third-party/CPP
    ${CMAKE_SOURCE_DIR}/third-party/CPP/7zip
    ${CMAKE_SOURCE_DIR}/third-party/CPP/Windows
    ${CMAKE_SOURCE_DIR}/third-party/CPP/Common
)

if(MSVC)
    target_compile_options(TestISOReader PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
    set_target_properties(TestISOReader PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(TestISOReader PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

target_link_libraries(TestISOReader PRIVATE sevenzip)
if(MSVC)
    target_link_options(TestISOReader PRIVATE "/WHOLEARCHIVE:sevenzip")
endif()

add_executable(TestISODetection
    test_iso_detection.cpp
    src/models/ISOReader.cpp
    src/utils/Utils.cpp
)

target_include_directories(TestISODetection
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/third-party/CPP
    ${CMAKE_SOURCE_DIR}/third-party/CPP/7zip
    ${CMAKE_SOURCE_DIR}/third-party/CPP/Windows
    ${CMAKE_SOURCE_DIR}/third-party/CPP/Common
)

if(MSVC)
    target_compile_options(TestISODetection PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
    set_target_properties(TestISODetection PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(TestISODetection PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

target_link_libraries(TestISODetection PRIVATE sevenzip)
if(MSVC)
    target_link_options(TestISODetection PRIVATE "/WHOLEARCHIVE:sevenzip")
endif()


add_executable(ListFormats
    test_list_formats.cpp
)

target_include_directories(ListFormats
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third-party/CPP
        ${CMAKE_SOURCE_DIR}/third-party/CPP/7zip
        ${CMAKE_SOURCE_DIR}/third-party/CPP/Windows
        ${CMAKE_SOURCE_DIR}/third-party/CPP/Common
)

target_link_libraries(ListFormats PRIVATE sevenzip)
if(MSVC)
    target_link_options(ListFormats PRIVATE "/WHOLEARCHIVE:sevenzip")
    set_target_properties(ListFormats PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Translation validation tool
add_executable(ValidateTranslations
    tools/validate_translations.cpp
)

target_include_directories(ValidateTranslations
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(ValidateTranslations PRIVATE /utf-8 /W4 /permissive- /EHsc /Zc:__cplusplus)
    set_target_properties(ValidateTranslations PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(ValidateTranslations PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()


