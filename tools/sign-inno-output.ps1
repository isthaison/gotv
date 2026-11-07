param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:STORE_SIGNING_CERT_PATH) -or
    [string]::IsNullOrWhiteSpace($env:STORE_SIGNING_CERT_PASSWORD)) {
    throw 'Store signing certificate environment is not configured.'
}

$signTool = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName |
    Select-Object -Last 1
if (-not $signTool) {
    throw 'Windows SDK signtool.exe was not found.'
}

& $signTool.FullName sign /fd SHA256 /tr 'http://timestamp.digicert.com' /td SHA256 `
    /f $env:STORE_SIGNING_CERT_PATH /p $env:STORE_SIGNING_CERT_PASSWORD $Path
if ($LASTEXITCODE -ne 0) {
    throw "signtool failed to sign '$Path' (exit $LASTEXITCODE)."
}
