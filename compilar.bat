@echo off
setlocal EnableExtensions

:: Build the project relative to this script and optionally sign the output.

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
pushd "%SCRIPT_DIR%" >nul

set "EXIT_CODE=0"

set "BUILD_DIR=%SCRIPT_DIR%\build"
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%" 2>nul
    if errorlevel 1 (
        echo [ERROR] Failed to create build directory "%BUILD_DIR%".
        set "EXIT_CODE=1"
        goto :cleanup
    )
)

set "GENERATOR=%CMAKE_GENERATOR%"
if not defined GENERATOR set "GENERATOR=Visual Studio 17 2022"

set "ARCH=%CMAKE_ARCH%"
if not defined ARCH set "ARCH=x64"

set "CONFIG=%CMAKE_CONFIG%"
if not defined CONFIG set "CONFIG=Release"

if not defined TARGET_NAME set "TARGET_NAME=BootThatISO!.exe"

call :resolve_cmake
if errorlevel 1 (
    set "EXIT_CODE=%ERRORLEVEL%"
    goto :cleanup
)

call :format_code
if errorlevel 1 (
    echo [WARNING] Code formatting failed or clang-format not found. Continuing build...
)

echo [INFO] Configuring with generator "%GENERATOR%" (%ARCH%)
"%CMAKE_CMD%" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" -A "%ARCH%"
if errorlevel 1 (
    set "EXIT_CODE=%ERRORLEVEL%"
    goto :cleanup
)

echo [INFO] Building configuration "%CONFIG%"
"%CMAKE_CMD%" --build "%BUILD_DIR%" --config "%CONFIG%"
if errorlevel 1 (
    set "EXIT_CODE=%ERRORLEVEL%"
    goto :cleanup
)

call :maybe_sign
set "EXIT_CODE=%ERRORLEVEL%"

:cleanup
popd >nul
exit /b %EXIT_CODE%

:resolve_cmake
if defined CMAKE_EXE (
    set "CMAKE_CMD=%CMAKE_EXE%"
    set "CMAKE_CMD=%CMAKE_CMD:"=%"
    if exist "%CMAKE_CMD%" goto :resolve_cmake_done
    for /f "delims=" %%I in ('where "%CMAKE_CMD%" 2^>nul') do (
        set "CMAKE_CMD=%%~fI"
        goto :resolve_cmake_done
    )
    echo [ERROR] CMAKE_EXE "%CMAKE_EXE%" not found.
    exit /b 1
)
for /f "delims=" %%I in ('where cmake 2^>nul') do (
    set "CMAKE_CMD=%%~fI"
    goto :resolve_cmake_done
)
if exist "%ProgramFiles%\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=%ProgramFiles%\CMake\bin\cmake.exe"
    goto :resolve_cmake_done
)
if exist "%ProgramFiles(x86)%\CMake\bin\cmake.exe" (
    set "CMAKE_CMD=%ProgramFiles(x86)%\CMake\bin\cmake.exe"
    goto :resolve_cmake_done
)
echo [ERROR] Could not find cmake. Install CMake or set CMAKE_EXE.
exit /b 1
:resolve_cmake_done
if not exist "%CMAKE_CMD%" (
    echo [ERROR] CMake executable "%CMAKE_CMD%" does not exist.
    exit /b 1
)
exit /b 0

:maybe_sign
if not defined SIGN_CERT_SHA1 set "SIGN_CERT_SHA1=91CD06AAB4C7685A51DD3F44343D08352161177F"

if /i "%SIGN_CERT_SHA1%"=="skip" (
    echo [INFO] Signing skipped via SIGN_CERT_SHA1=skip.
    exit /b 0
)

call :resolve_signtool
if errorlevel 1 exit /b %ERRORLEVEL%

set "SIGN_DESCRIPTION=%SIGN_DESCRIPTION%"
if not defined SIGN_DESCRIPTION set "SIGN_DESCRIPTION=BootThatISO - Bootear ISO sin USB"

set "SIGN_URL=%SIGN_URL%"
if not defined SIGN_URL set "SIGN_URL=https://github.com/tiirwaa/BootThatISO-WITHOUT-USB---NO-USB-NEEDED-"

set "SIGN_TIMESTAMP_URL=%SIGN_TIMESTAMP_URL%"
if not defined SIGN_TIMESTAMP_URL set "SIGN_TIMESTAMP_URL=http://timestamp.digicert.com"

set "SIGN_DIGEST=%SIGN_DIGEST%"
if not defined SIGN_DIGEST set "SIGN_DIGEST=SHA256"

set "TARGET_PATH=%BUILD_DIR%\%CONFIG%\%TARGET_NAME%"
if not exist "%TARGET_PATH%" (
    echo [ERROR] Built binary "%TARGET_PATH%" not found. Signing skipped.
    exit /b 1
)

echo [INFO] Signing "%TARGET_PATH%"...
"%SIGNTOOL_CMD%" sign /fd "%SIGN_DIGEST%" /sha1 "%SIGN_CERT_SHA1%" /d "%SIGN_DESCRIPTION%" /du "%SIGN_URL%" /tr "%SIGN_TIMESTAMP_URL%" /td "%SIGN_DIGEST%" "%TARGET_PATH%"
exit /b %ERRORLEVEL%

:resolve_signtool
if defined SIGNTOOL_EXE (
    set "SIGNTOOL_CMD=%SIGNTOOL_EXE%"
    set "SIGNTOOL_CMD=%SIGNTOOL_CMD:"=%"
    if exist "%SIGNTOOL_CMD%" goto :resolve_signtool_done
    echo [ERROR] SIGNTOOL_EXE "%SIGNTOOL_EXE%" not found.
    exit /b 1
)
for /f "delims=" %%I in ('where signtool 2^>nul') do (
    set "SIGNTOOL_CMD=%%~fI"
    goto :resolve_signtool_done
)
for /f "delims=" %%I in ('dir /b /s "%ProgramFiles(x86)%\Windows Kits\10\bin\signtool.exe" 2^>nul ^| sort /r') do (
    set "SIGNTOOL_CMD=%%~fI"
    goto :resolve_signtool_done
)
for /f "delims=" %%I in ('dir /b /s "%ProgramFiles%\Windows Kits\10\bin\signtool.exe" 2^>nul ^| sort /r') do (
    set "SIGNTOOL_CMD=%%~fI"
    goto :resolve_signtool_done
)
echo [ERROR] Could not locate signtool.exe. Install Windows SDK or set SIGNTOOL_EXE.
exit /b 1
:resolve_signtool_done
if not exist "%SIGNTOOL_CMD%" (
    echo [ERROR] signtool.exe "%SIGNTOOL_CMD%" does not exist.
    exit /b 1
)
exit /b 0

:format_code
echo [INFO] Formatting code with clang-format...
call :resolve_clang_format
if errorlevel 1 (
    echo [WARNING] clang-format not found, skipping formatting.
    exit /b 0
)

:: Format all .cpp and .h files in src, include, and tests directories
for /r "%SCRIPT_DIR%\src" %%F in (*.cpp *.h) do (
    "%CLANG_FORMAT_CMD%" -i "%%F" 2>nul
)
for /r "%SCRIPT_DIR%\include" %%F in (*.cpp *.h) do (
    "%CLANG_FORMAT_CMD%" -i "%%F" 2>nul
)
for /r "%SCRIPT_DIR%\tests" %%F in (*.cpp *.h) do (
    "%CLANG_FORMAT_CMD%" -i "%%F" 2>nul
)

echo [INFO] Code formatting completed successfully.
exit /b 0

:resolve_clang_format
if defined CLANG_FORMAT_EXE (
    set "CLANG_FORMAT_CMD=%CLANG_FORMAT_EXE%"
    set "CLANG_FORMAT_CMD=%CLANG_FORMAT_CMD:"=%"
    if exist "%CLANG_FORMAT_CMD%" goto :resolve_clang_format_done
    echo [WARNING] CLANG_FORMAT_EXE "%CLANG_FORMAT_EXE%" not found.
    exit /b 1
)
for /f "delims=" %%I in ('where clang-format 2^>nul') do (
    set "CLANG_FORMAT_CMD=%%~fI"
    goto :resolve_clang_format_done
)
if exist "%ProgramFiles%\LLVM\bin\clang-format.exe" (
    set "CLANG_FORMAT_CMD=%ProgramFiles%\LLVM\bin\clang-format.exe"
    goto :resolve_clang_format_done
)
if exist "%ProgramFiles(x86)%\LLVM\bin\clang-format.exe" (
    set "CLANG_FORMAT_CMD=%ProgramFiles(x86)%\LLVM\bin\clang-format.exe"
    goto :resolve_clang_format_done
)
echo [WARNING] Could not find clang-format.exe.
exit /b 1
:resolve_clang_format_done
if not exist "%CLANG_FORMAT_CMD%" (
    echo [WARNING] clang-format.exe "%CLANG_FORMAT_CMD%" does not exist.
    exit /b 1
)
exit /b 0
