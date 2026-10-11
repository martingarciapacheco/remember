@echo off
REM Requires PowerShell 7.2+. Pass explicit tool/source paths.
if not defined REMEMBER_PWSH set "REMEMBER_PWSH=pwsh.exe"
"%REMEMBER_PWSH%" -NoProfile -File "%~dp0build-release.ps1" %*
exit /b %errorlevel%
