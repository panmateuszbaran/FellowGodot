@echo off
rem FellowEngine Windows build helper (MSVC via SCons auto-detection).
rem Usage: build_windows.bat <target> [extra scons args]
rem   target: editor | template_release | template_debug
rem Common flags are fixed here so STOCK and FELLOW binaries are built identically.
rem disable_path_overrides=no only re-enables the --path CLI switch in export templates (benchmark harness);
rem it does not touch rendering code.
setlocal
set ROOT=%~dp0..\..
pushd "%ROOT%"
set PY=.tools\venv\Scripts\python.exe
if not exist "%PY%" set PY=python
set TARGET=%1
if "%TARGET%"=="" set TARGET=template_release
shift
set EXTRA=
:collect
if "%~1"=="" goto run
set EXTRA=%EXTRA% %1
shift
goto collect
:run
echo [fellow] scons platform=windows target=%TARGET% production=yes disable_path_overrides=no winrt=no accesskit=no d3d12=no %EXTRA%
"%PY%" -m SCons platform=windows target=%TARGET% production=yes disable_path_overrides=no winrt=no accesskit=no d3d12=no %EXTRA%
set RC=%ERRORLEVEL%
popd
exit /b %RC%
