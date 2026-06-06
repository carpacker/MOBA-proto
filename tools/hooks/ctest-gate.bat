@echo off
rem ctest-gate (M1.4): locate MSVC, build the ci /WX preset, run ctest. Exits nonzero
rem on ANY failure so tools/hooks/pre-push can block the push. Can also be run directly
rem from any shell to reproduce the gate:  tools\hooks\ctest-gate.bat
setlocal

set "ROOT=%~dp0..\.."
pushd "%ROOT%" || exit /b 1

rem --- locate the VS toolchain (cl/cmake/ninja) via vswhere, then source vcvars64 ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( set "MSG=vswhere not found; install the Visual Studio C++ workload." & goto fail )
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH ( set "MSG=no Visual Studio install found." & goto fail )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( set "MSG=vcvars64 failed." & goto fail )

rem --- configure (only if the build dir is missing), then /WX build, then test ---
if not exist build-ci\CMakeCache.txt (
    cmake --preset ci
    if errorlevel 1 ( set "MSG=configure failed." & goto fail )
)
cmake --build build-ci --config Debug
if errorlevel 1 ( set "MSG=build failed -- warnings are errors under /WX." & goto fail )

rem --no-tests=error: ctest exits 0 if it finds NO tests -- that would let the gate go
rem green having run nothing (e.g. test registration silently broke). Make it fail.
ctest --test-dir build-ci -C Debug --output-on-failure --no-tests=error
set "RC=%errorlevel%"
popd
exit /b %RC%

:fail
echo [gate] %MSG%
popd
exit /b 1
