@echo off
REM Build, validate and publish the Windows/macOS pair.
if not defined REMEMBER_PWSH set "REMEMBER_PWSH=pwsh.exe"
"%REMEMBER_PWSH%" -NoProfile -File "%~dp0build-installers.ps1" -Publish %*
exit /b %errorlevel%
