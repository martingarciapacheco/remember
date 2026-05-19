@echo off
REM ============================================================================
REM  Remember - full release build pipeline
REM
REM   1. Re-runs CMake configure on both Plugin and App (so version, git hash
REM      and source-file list are fresh).
REM   2. Builds both in Release with MSBuild.
REM   3. Compiles the Inno Setup installer.
REM
REM  Output: <repo>/dist/Remember-<version>-setup.exe
REM
REM  Run from the repository root:
REM      installer\build-release.bat
REM ============================================================================

setlocal enabledelayedexpansion

set REPO=%~dp0..
pushd "%REPO%"

REM Store paths WITHOUT surrounding quotes — quotes are added at each use site.
REM Storing quoted values breaks `if "%VAR%"==""` checks: it becomes
REM `if ""C:\Inno Setup 6\..."" == ""`, and cmd parses the unquoted middle
REM as command tokens, blowing up on the word "Setup".
set MSBUILD=C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

REM Locate ISCC.exe. winget installs to %LOCALAPPDATA%; the system-wide
REM installer lands in %ProgramFiles%.
set ISCC=
call :tryIscc "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
call :tryIscc "%ProgramFiles%\Inno Setup 6\ISCC.exe"
goto :checkIscc

:tryIscc
if defined ISCC exit /b
if exist %1 set ISCC=%~1
exit /b

:checkIscc

if not exist "%MSBUILD%" (
    echo [ERROR] MSBuild not found at %MSBUILD%
    goto :fail
)
if not exist "%CMAKE%" (
    echo [ERROR] cmake.exe not found at %CMAKE%
    goto :fail
)
if not defined ISCC (
    echo [ERROR] ISCC.exe not found in any standard location.
    echo         Install Inno Setup 6: winget install --id JRSoftware.InnoSetup
    goto :fail
)
echo Using ISCC: %ISCC%

echo.
echo ============================================================
echo  Configuring (refresh CMake project files)
echo ============================================================
"%CMAKE%" -S Plugin -B Plugin\build 1>nul
if errorlevel 1 goto :fail
"%CMAKE%" -S App -B App\build 1>nul
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  Building Plugin (Release)
echo ============================================================
"%MSBUILD%" Plugin\build\RememberPlugin.sln /p:Configuration=Release /m /nologo /v:minimal
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  Building App (Release)
echo ============================================================
"%MSBUILD%" App\build\RememberApp.sln /p:Configuration=Release /t:RememberApp /m /nologo /v:minimal
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  Compiling installer
echo ============================================================
if not exist dist mkdir dist
"%ISCC%" installer\Remember.iss
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  Done.
echo ============================================================
echo  Installer:
for %%f in (dist\Remember-*-setup.exe) do echo   %%~ff
echo.
echo  Next step: installer\publish-release.bat to push to GitHub.
popd
endlocal
exit /b 0

:fail
echo.
echo ============================================================
echo  BUILD FAILED
echo ============================================================
popd
endlocal
exit /b 1
