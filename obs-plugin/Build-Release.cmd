@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%~dp0.."

for /f "delims=" %%V in ('powershell -NoProfile -Command "(Get-Content -LiteralPath '%SCRIPT_DIR%buildspec.json' -Raw | ConvertFrom-Json).version"') do set "VERSION=%%V"
if not defined VERSION (
  echo Could not read the plugin version from buildspec.json.
  exit /b 1
)

set "STAGE_DIR=%SCRIPT_DIR%release\v%VERSION%"
set "RELEASE_ROOT=%REPO_ROOT%\artifacts\release"
set "ZIP_NAME=Steam.Controller.Gamepad.Viewer-v%VERSION%-OBS-Plugin-Windows-x64.zip"
set "ZIP_PATH=%RELEASE_ROOT%\%ZIP_NAME%"

set "CMAKE_EXE="
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%C"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined CMAKE_EXE (
  if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%C in (`"%VSWHERE%" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE_EXE=%%C"
  )
)

if not defined CMAKE_EXE (
  echo CMake was not found. Install Visual Studio's Desktop development with C++ workload.
  exit /b 1
)

pushd "%SCRIPT_DIR%"
"%CMAKE_EXE%" --preset windows-x64
if errorlevel 1 goto :failed

"%CMAKE_EXE%" --build --preset windows-x64 --config RelWithDebInfo
if errorlevel 1 goto :failed

if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
"%CMAKE_EXE%" --install build_x64 --prefix "%STAGE_DIR%" --config RelWithDebInfo
if errorlevel 1 goto :failed

del /q "%STAGE_DIR%\steam-controller-gamepad-viewer\bin\64bit\*.pdb" 2>nul

if not exist "%STAGE_DIR%\steam-controller-gamepad-viewer\bin\64bit\steam-controller-gamepad-viewer.dll" goto :missing_files
if not exist "%STAGE_DIR%\steam-controller-gamepad-viewer\data\backend\SteamControllerGamepadViewer.exe" goto :missing_files

if not exist "%RELEASE_ROOT%" mkdir "%RELEASE_ROOT%"
set "ZIP_SOURCE=%STAGE_DIR%"
set "ZIP_DESTINATION=%ZIP_PATH%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "if (Test-Path -LiteralPath $env:ZIP_DESTINATION) { Remove-Item -LiteralPath $env:ZIP_DESTINATION -Force }; Compress-Archive -Path (Join-Path $env:ZIP_SOURCE '*') -DestinationPath $env:ZIP_DESTINATION -Force"
if errorlevel 1 goto :failed

popd
echo Built:
echo %ZIP_PATH%
exit /b 0

:missing_files
echo The staged release is missing the plugin DLL or bundled backend.

:failed
popd
echo Native OBS release build failed.
exit /b 1
