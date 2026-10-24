@echo off
setlocal enabledelayedexpansion


REM Check if Git is installed
where git >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Git is not installed or not found in PATH.
    exit /b 1
)

REM Check if CMake is installed
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo CMake is not installed or not found in PATH.
    exit /b 1
)

REM Initialize flags
set CLEAN=0
set REMOTE=0
set FRESH=0

REM Parse command line arguments
:parse_args
if "%~1"=="" goto end_parse
if "%~1"=="--clean" (
    set CLEAN=1
) else if "%~1"=="--remote" (
    set REMOTE=1
) else if "%~1"=="--fresh" (
    set FRESH=1
)
shift
goto parse_args

:end_parse

REM Handle "clean" argument
if !CLEAN! EQU 1 (
    echo Cleaning build directory...
    if exist "build" (
        rmdir /S /Q "build"
    )
)

REM Initialize and update git submodules
echo Updating Git submodules...
git submodule init
if %ERRORLEVEL% neq 0 (
    echo Failed to initialize submodules.
    exit /b 1
)

REM Prepare git submodule update command based on the REMOTE flag
set GIT_CMD=git submodule update --init --recursive
if !REMOTE! EQU 1 (
    set GIT_CMD=git submodule update --init --recursive --remote
)

REM Execute the git command
echo Running command: !GIT_CMD!
call !GIT_CMD!
if %ERRORLEVEL% neq 0 (
    echo Failed to update submodules.
    exit /b 1
)

REM Create the build directory if it doesn't exist
if not exist build (
    echo Creating build directory...
    mkdir build
)

REM Prepare cmake command based on the FRESH flag
set CMAKE_CMD=cmake -S . -B build -A x64 -G "Visual Studio 18 2026" -DCMAKE_BUILD_TYPE=Release
if !FRESH! EQU 1 (
    set CMAKE_CMD=cmake --fresh -S . -B build -A x64 -G "Visual Studio 18 2026" -DCMAKE_BUILD_TYPE=Release
)

REM Run CMake to generate Visual Studio solution files
echo Running command: !CMAKE_CMD!
call !CMAKE_CMD!
if %ERRORLEVEL% neq 0 (
    echo CMake configuration failed.
    exit /b 1
)

echo Build setup completed successfully.