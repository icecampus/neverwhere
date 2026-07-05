@echo off
rem ===========================================================================
rem generate_compile_commands.bat
rem
rem Configures the project with the Ninja generator in a separate build
rem directory (_intermediate_ide) so we can emit compile_commands.json for
rem clangd-based tooling (Serena MCP, clangd LSP, etc.).
rem
rem This does NOT replace the main VS2022 flow:
rem   * CMakePresets.json `vs2022`      -> Visual Studio generator
rem   * This script's `ide-index`       -> Ninja generator, indexing only
rem
rem The Ninja generator (unlike VS) honours CMAKE_EXPORT_COMPILE_COMMANDS.
rem We must run from a Developer Prompt (vcvars64.bat) because Ninja does
rem not auto-detect MSVC environment the way the VS generator does.
rem
rem After successful configure, the script drops a compile_commands.json
rem symlink at the project root (or copies it on systems where symlinks
rem are restricted).
rem
rem Usage:
rem   generate_compile_commands.bat          # configure only
rem   generate_compile_commands.bat --clean  # wipe _intermediate_ide first
rem ===========================================================================

setlocal enableextensions

set "REPO_ROOT=%~dp0"
set "REPO_ROOT=%REPO_ROOT:~0,-1%"
set "IDE_DIR=%REPO_ROOT%\_intermediate_ide"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set "VCPKG=%REPO_ROOT%\toolchain\vcpkg\scripts\buildsystems\vcpkg.cmake"

rem --- sanity checks ---
if not exist "%VCVARS%" (
    echo ERROR: vcvars64.bat not found at: %VCVARS%
    echo        Edit this script to point at your VS installation.
    exit /b 1
)
if not exist "%VCPKG%" (
    echo ERROR: vcpkg.cmake not found at: %VCPKG%
    echo        Did you forget to `git submodule update --init toolchain/vcpkg`?
    exit /b 1
)

rem --- optionally wipe the previous configure ---
if /i "%~1"=="--clean" (
    echo [generate_compile_commands] removing %IDE_DIR%
    if exist "%IDE_DIR%" rmdir /s /q "%IDE_DIR%"
)

rem --- set up MSVC environment ---
call "%VCVARS%" >nul
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed.
    exit /b 1
)

rem --- configure with Ninja ---
echo [generate_compile_commands] cmake -G "Ninja" -B "%IDE_DIR%" -S "%REPO_ROOT%"
cmake -G "Ninja" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DVCPKG_OVERLAY_PORTS="%REPO_ROOT%\vcpkg_overlays\ports" ^
    -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=d:/_build" ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
    -S "%REPO_ROOT%" ^
    -B "%IDE_DIR%"
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    exit /b 1
)

rem --- publish compile_commands.json to the project root ---
if not exist "%IDE_DIR%\compile_commands.json" (
    echo ERROR: configure succeeded but compile_commands.json was not produced.
    echo        This should not happen with the Ninja generator.
    exit /b 1
)

set "TARGET=%REPO_ROOT%\compile_commands.json"
if exist "%TARGET%" del /q "%TARGET%"

rem Prefer a symlink (cheap, always in sync) but fall back to a copy if the
rem user cannot create symlinks (no SeCreateSymbolicLinkPrivilege).
mklink "%TARGET%" "%IDE_DIR%\compile_commands.json" >nul 2>&1
if errorlevel 1 (
    echo [generate_compile_commands] symlink denied, copying instead
    copy /y "%IDE_DIR%\compile_commands.json" "%TARGET%" >nul
)

echo.
echo [generate_compile_commands] OK
echo    Indexing build dir: %IDE_DIR%
echo    Published to:       %TARGET%
echo    (Re-run after adding/removing sources or changing compile flags.)
endlocal
