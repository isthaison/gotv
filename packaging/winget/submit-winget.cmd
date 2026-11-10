@echo off
setlocal
echo ============================================================
echo   Submitting GoTV 0.9.14 to Windows Package Manager (Winget)
echo ============================================================
echo.
echo Validating local manifests...
winget validate --manifest "%~dp0isthaison\GoTV\0.9.14"
if errorlevel 1 (
    echo [ERROR] Manifest validation failed!
    exit /b 1
)

echo.
echo Submitting Pull Request to microsoft/winget-pkgs via wingetcreate...
echo (If prompted, please authenticate with your GitHub account)
wingetcreate.exe submit -p "New package: isthaison.GoTV version 0.9.14" "%~dp0isthaison\GoTV\0.9.14"
if errorlevel 1 (
    echo [WARNING] Direct submission failed or was cancelled.
    echo You can also create a PR manually from the files in:
    echo   %~dp0isthaison\GoTV\0.9.14
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Submitted successfully!
exit /b 0
