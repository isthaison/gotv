@echo off
REM GoTV entry point for cmd.exe: forwards to gtv.sh (needs MSYS2/Git bash).
REM Usage: gtv <build|test|vet|clean|install|uninstall|package|bump|help>
REM NOTE: plain `where bash` may resolve to the WSL stub, so real
REM installations are probed first.
setlocal
set "HERE=%~dp0"

if "%1"=="install" goto do_install

if exist "C:\msys64\usr\bin\bash.exe" (
  "C:\msys64\usr\bin\bash.exe" "%HERE%gtv.sh" %*
  exit /b %ERRORLEVEL%
)
if exist "C:\Program Files\Git\bin\bash.exe" (
  "C:\Program Files\Git\bin\bash.exe" "%HERE%gtv.sh" %*
  exit /b %ERRORLEVEL%
)
where bash >nul 2>nul
if %ERRORLEVEL%==0 (
  bash "%HERE%gtv.sh" %*
  exit /b %ERRORLEVEL%
)
echo gtv.cmd: bash not found. Install MSYS2 or Git for Windows, then run: gtv build
exit /b 1

:do_install
shift
if exist "C:\msys64\usr\bin\bash.exe" (
    "C:\msys64\usr\bin\bash.exe" "%HERE%gtv.sh" package
) else if exist "C:\Program Files\Git\bin\bash.exe" (
    "C:\Program Files\Git\bin\bash.exe" "%HERE%gtv.sh" package
) else (
    bash "%HERE%gtv.sh" package
)
if errorlevel 1 exit /b %ERRORLEVEL%

for /f "usebackq delims=" %%v in ("%HERE%VERSION") do set "VER=%%v"
set "SETUP=%HERE%gotv-%VER%-x64-setup.exe"
if not exist "%SETUP%" (
    echo gtv.cmd: Setup not found: %SETUP%
    exit /b 1
)
echo Dang cai dat GoTV %VER% vao he thong...
start "" /wait "%SETUP%" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES
if errorlevel 1 (
    echo gtv.cmd: Cai dat that bai (exit %ERRORLEVEL%)
    exit /b %ERRORLEVEL%
)
echo Cai dat thanh cong GoTV %VER%.
start "" "%LOCALAPPDATA%\Programs\GoTV\ver\%VER%\gotv.exe" --takeover
exit /b 0
