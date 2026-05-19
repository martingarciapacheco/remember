@echo off
REM ============================================================================
REM  Remember - publish the freshly built installer to GitHub Releases
REM
REM   1. Tags the current commit with the version baked into the installer.
REM   2. Pushes the tag.
REM   3. Creates a GitHub Release (marked --prerelease) attached to that tag,
REM      uploads the installer + BETA_NOTES.md, body = BETA_NOTES.md.
REM
REM  Requires:
REM      - Inno-built installer already in dist/
REM      - gh authenticated
REM      - clean working tree (or you accept tagging the current state)
REM ============================================================================

setlocal enabledelayedexpansion

set REPO=%~dp0..
pushd "%REPO%"

REM Store path without quotes; quote at use sites.
set GH=C:\Program Files\GitHub CLI\gh.exe

if not exist "%GH%" (
    echo [ERROR] gh.exe not found at "%GH%"
    echo         winget install --id GitHub.cli
    goto :fail
)

REM Find the installer (assumes only one in dist/)
set INSTALLER=
for %%f in (dist\Remember-*-setup.exe) do set INSTALLER=%%f
if "%INSTALLER%"=="" (
    echo [ERROR] No installer found in dist\. Run installer\build-release.bat first.
    goto :fail
)
echo Installer: %INSTALLER%

REM Derive version (everything between "Remember-" and "-setup.exe")
for %%f in ("%INSTALLER%") do set FN=%%~nf
set VERSION=%FN:Remember-=%
set VERSION=%VERSION:-setup=%
set TAG=v%VERSION%
echo Version:   %VERSION%
echo Tag:       %TAG%

echo.
echo Tagging current commit and pushing tag...
git tag -a %TAG% -m "Release %TAG%"
if errorlevel 1 (
    echo [WARN] Tag may already exist locally. Continuing.
)
git push origin %TAG%
if errorlevel 1 goto :fail

echo.
echo Creating GitHub Release %TAG%...
"%GH%" release create %TAG% ^
    --prerelease ^
    --title "Remember %VERSION%" ^
    --notes-file BETA_NOTES.md ^
    "%INSTALLER%" ^
    BETA_NOTES.md
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  Release published.
echo ============================================================
"%GH%" release view %TAG% --web

popd
endlocal
exit /b 0

:fail
echo.
echo ============================================================
echo  PUBLISH FAILED
echo ============================================================
popd
endlocal
exit /b 1
