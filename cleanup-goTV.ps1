$ErrorActionPreference = 'Stop'

Write-Host "== GoTV cleanup before install ==" -ForegroundColor Cyan

$processNames = @(
    'gotv',
    'GoTV',
    'explorer'
)

foreach ($name in $processNames) {
    $p = Get-Process -Name $name -ErrorAction SilentlyContinue
    if ($p) {
        Write-Host "Stopping $($p.Count) process(es): $name" -ForegroundColor Yellow
        Stop-Process -Name $name -Force -ErrorAction SilentlyContinue
    }
}

$paths = @(
    "$env:LOCALAPPDATA\Programs\GoTV",
    "$env:LOCALAPPDATA\Programs\GoTiengViet"
)

foreach ($p in $paths) {
    if (Test-Path $p) {
        Write-Host "Removing stale install: $p" -ForegroundColor Yellow
        Remove-Item $p -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# Restart Explorer so the TSF DLL is no longer held by the shell.
Start-Sleep -Seconds 2
Start-Process explorer.exe -ErrorAction SilentlyContinue

Write-Host "Cleanup complete. You can now run the installer." -ForegroundColor Green
