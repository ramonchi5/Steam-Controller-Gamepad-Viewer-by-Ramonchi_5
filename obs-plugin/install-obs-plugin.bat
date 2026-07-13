@echo off
setlocal EnableExtensions

set "PLUGIN_NAME=steam-controller-gamepad-viewer"
set "SOURCE_DIR=%~dp0%PLUGIN_NAME%"
set "TARGET_DIR=%ProgramData%\obs-studio\plugins\%PLUGIN_NAME%"

if not exist "%SOURCE_DIR%\bin\64bit\%PLUGIN_NAME%.dll" (
  echo Could not find "%SOURCE_DIR%\bin\64bit\%PLUGIN_NAME%.dll".
  echo Run this installer from the extracted OBS plugin release folder.
  pause
  exit /b 1
)

tasklist /FI "IMAGENAME eq obs64.exe" 2>nul | find /I "obs64.exe" >nul
if not errorlevel 1 (
  echo Close OBS Studio before installing or updating this plugin.
  pause
  exit /b 1
)

if exist "%TARGET_DIR%" rmdir /s /q "%TARGET_DIR%"
if exist "%TARGET_DIR%" (
  echo Could not replace the existing plugin folder.
  echo Try running this installer as administrator.
  pause
  exit /b 1
)

mkdir "%TARGET_DIR%" >nul 2>nul
if errorlevel 1 goto :install_failed

xcopy "%SOURCE_DIR%" "%TARGET_DIR%" /e /i /y >nul
if errorlevel 2 goto :install_failed

echo Installed Steam Controller Gamepad Viewer OBS plugin to:
echo %TARGET_DIR%
echo.
echo Restart OBS, then add "Steam Controller Gamepad Viewer" from Sources.
pause
exit /b 0

:install_failed
echo OBS plugin installation failed.
echo Try running this installer as administrator.
pause
exit /b 1
