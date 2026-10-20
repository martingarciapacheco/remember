@echo off
REM Requires -Installer and -Manifest. Use -ValidateOnly to inspect.
if not defined REMEMBER_PWSH set "REMEMBER_PWSH=pwsh.exe"
"%REMEMBER_PWSH%" -NoProfile -File "%~dp0publish-release.ps1" %*
exit /b %errorlevel%
