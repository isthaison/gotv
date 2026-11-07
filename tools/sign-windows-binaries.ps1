[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [string]$CertificatePath,
    [Parameter(Mandatory = $true)]
    [string]$Password,
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$timestampUrl = 'http://timestamp.digicert.com'

$signTool = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName |
    Select-Object -Last 1
if (-not $signTool) {
    throw 'Windows SDK signtool.exe was not found.'
}

$target = Get-Item -LiteralPath $Path
if ($target.PSIsContainer) {
    $files = @(Get-ChildItem -LiteralPath $target.FullName -Recurse -File |
        Where-Object { $_.Extension -in '.exe', '.dll' } |
        Sort-Object FullName)
} else {
    $files = @($target)
}

if ($files.Count -eq 0) {
    throw "No PE files found to sign at '$Path'."
}

foreach ($file in $files) {
    if ($file.Extension -notin '.exe', '.dll') {
        throw "Refusing to sign non-PE file '$($file.FullName)'."
    }

    if (-not $VerifyOnly) {
        & $signTool.FullName sign /fd SHA256 /tr $timestampUrl /td SHA256 /f $CertificatePath /p $Password $file.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "signtool failed to sign '$($file.FullName)' (exit $LASTEXITCODE)."
        }
    }

    & $signTool.FullName verify /pa /all $file.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Authenticode verification failed for '$($file.FullName)' (exit $LASTEXITCODE)."
    }
}
