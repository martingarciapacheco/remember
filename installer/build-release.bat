@echo off
REM Build Windows and macOS installers together in GitHub Actions.
if not defined REMEMBER_PWSH set "REMEMBER_PWSH=pwsh.exe"
"%REMEMBER_PWSH%" -NoProfile -File "%~dp0build-installers.ps1" %*
exit /b %errorlevel%
