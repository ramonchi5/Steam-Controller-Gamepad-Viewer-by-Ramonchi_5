@echo off
setlocal

set "RELEASE_NAME=%~1"
if "%RELEASE_NAME%"=="" set "RELEASE_NAME=v2.0.1.Steam.Controller.Viewer"

set "ROOT=%~dp0"
set "PROJECT=%ROOT%src\SteamControllerGamepadViewer\SteamControllerGamepadViewer.csproj"
set "RELEASE_ROOT=%ROOT%artifacts\release"
set "BASE_NAME=%RELEASE_NAME%"
set "BASE_OUT=%RELEASE_ROOT%\%BASE_NAME%"

dotnet publish "%PROJECT%" -c Release -r win-x64 --self-contained true -o "%BASE_OUT%" -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -p:DebugSymbols=false
if errorlevel 1 exit /b %errorlevel%

call :CleanPublish "%BASE_OUT%"
call :CopyReleaseDocs "%BASE_OUT%"
if errorlevel 1 exit /b %errorlevel%
call :CopyStartupScripts "%BASE_OUT%"
if errorlevel 1 exit /b %errorlevel%

call :ZipFolder "%BASE_OUT%" "%RELEASE_ROOT%\%BASE_NAME%.zip"
if errorlevel 1 exit /b %errorlevel%

call :DeleteLegacyVariant
call :DeleteOldV3Variant

echo Created release zip in "%RELEASE_ROOT%"
exit /b 0

:CleanPublish
set "OUT=%~1"
if exist "%OUT%\wwwroot" rmdir /s /q "%OUT%\wwwroot"
del /q "%OUT%\*.staticwebassets.*.json" 2>nul
del /q "%OUT%\web.config" 2>nul
del /q "%OUT%\*.pdb" 2>nul
exit /b 0

:CopyReleaseDocs
set "OUT=%~1"
copy "%ROOT%README.md" "%OUT%\" >nul
if errorlevel 1 exit /b %errorlevel%
copy "%ROOT%LICENSE" "%OUT%\" >nul
if errorlevel 1 exit /b %errorlevel%
copy "%ROOT%THIRD_PARTY_NOTICES.md" "%OUT%\" >nul
exit /b %errorlevel%

:CopyStartupScripts
set "OUT=%~1"
copy "%ROOT%release-assets\Install Start With Windows.cmd" "%OUT%\" >nul
if errorlevel 1 exit /b %errorlevel%
copy "%ROOT%release-assets\Uninstall Start With Windows.cmd" "%OUT%\" >nul
exit /b %errorlevel%

:DeleteLegacyVariant
set "LEGACY_NAME=%BASE_NAME% (start with Windows)"
if exist "%RELEASE_ROOT%\%LEGACY_NAME%.zip" del /q "%RELEASE_ROOT%\%LEGACY_NAME%.zip" 2>nul
if exist "%RELEASE_ROOT%\%LEGACY_NAME%" rmdir /s /q "%RELEASE_ROOT%\%LEGACY_NAME%" 2>nul
exit /b 0

:DeleteOldV3Variant
if /i "%BASE_NAME%"=="v3.Steam.Controller.Viewer" exit /b 0
if exist "%RELEASE_ROOT%\v3.Steam.Controller.Viewer.zip" del /q "%RELEASE_ROOT%\v3.Steam.Controller.Viewer.zip" 2>nul
if exist "%RELEASE_ROOT%\v3.Steam.Controller.Viewer" rmdir /s /q "%RELEASE_ROOT%\v3.Steam.Controller.Viewer" 2>nul
exit /b 0

:ZipFolder
set "ZIP_SOURCE=%~1"
set "ZIP_PATH=%~2"
powershell -NoProfile -ExecutionPolicy Bypass -Command "if (Test-Path -LiteralPath $env:ZIP_PATH) { Remove-Item -LiteralPath $env:ZIP_PATH -Force }; Compress-Archive -Path (Join-Path $env:ZIP_SOURCE '*') -DestinationPath $env:ZIP_PATH -Force"
exit /b %errorlevel%
