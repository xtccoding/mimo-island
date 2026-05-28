@echo off
setlocal

set RELEASE_DIR=build\Release
set DEPLOY_DIR=deploy
set EXE_NAME=MiMoIsland.exe

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"

copy "%RELEASE_DIR%\%EXE_NAME%" "%DEPLOY_DIR%\" /Y
copy "%RELEASE_DIR%\Qt6Core.dll" "%DEPLOY_DIR%\" /Y
copy "%RELEASE_DIR%\Qt6Gui.dll" "%DEPLOY_DIR%\" /Y
copy "%RELEASE_DIR%\Qt6Widgets.dll" "%DEPLOY_DIR%\" /Y
copy "%RELEASE_DIR%\Qt6Network.dll" "%DEPLOY_DIR%\" /Y
copy "%RELEASE_DIR%\Qt6Svg.dll" "%DEPLOY_DIR%\" /Y

if not exist "%DEPLOY_DIR%\platforms" mkdir "%DEPLOY_DIR%\platforms"
copy "%RELEASE_DIR%\platforms\qwindows.dll" "%DEPLOY_DIR%\platforms\" /Y

if not exist "%DEPLOY_DIR%\tls" mkdir "%DEPLOY_DIR%\tls"
copy "%RELEASE_DIR%\tls\qopensslbackend.dll" "%DEPLOY_DIR%\tls\" /Y
copy "%RELEASE_DIR%\tls\qschannelbackend.dll" "%DEPLOY_DIR%\tls\" /Y

echo.
echo Deploy done: %DEPLOY_DIR%\
echo Total size:
for /f %%a in ('powershell -command "(Get-ChildItem '%DEPLOY_DIR%' -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB"') do echo %%a MB
echo.
echo To create single exe, use Enigma Virtual Box:
echo 1. Open Enigma Virtual Box
echo 2. Select: %DEPLOY_DIR%\%EXE_NAME%
echo 3. Add all DLLs and folders
echo 4. Click "Process"
pause
