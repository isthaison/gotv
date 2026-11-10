<#
.SYNOPSIS
    Inno Setup SignTool helper script.
.DESCRIPTION
    Called by Inno Setup during installer compilation to sign the embedded uninstaller
    and output executable.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

function Get-SignToolPath {
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) {
        return $cmd.Source
    }

    $sdkSearchPaths = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe",
        "${env:ProgramFiles}\Windows Kits\10\bin\*\x64\signtool.exe",
        "${env:ProgramFiles(x86)}\Windows Kits\11\bin\*\x64\signtool.exe",
        "${env:ProgramFiles}\Windows Kits\11\bin\*\x64\signtool.exe",
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x86\signtool.exe",
        "${env:ProgramFiles}\Windows Kits\10\bin\*\x86\signtool.exe"
    )

    foreach ($pattern in $sdkSearchPaths) {
        $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue |
            Sort-Object FullName |
            Select-Object -Last 1
        if ($found) {
            return $found.FullName
        }
    }

    return $null
}

$signTool = Get-SignToolPath
if (-not $signTool) {
    throw 'Windows SDK signtool.exe was not found for Inno Setup signing.'
}

$certPath = if (-not [string]::IsNullOrWhiteSpace($env:STORE_SIGNING_CERT_PATH)) { $env:STORE_SIGNING_CERT_PATH }
            elseif (-not [string]::IsNullOrWhiteSpace($env:GTV_SIGNING_CERT_PATH)) { $env:GTV_SIGNING_CERT_PATH }
            elseif (-not [string]::IsNullOrWhiteSpace($env:WIN_CERT_PFX_PATH)) { $env:WIN_CERT_PFX_PATH }
            else { $null }

if ([string]::IsNullOrWhiteSpace($certPath) -or -not (Test-Path -LiteralPath $certPath)) {
    throw "Signing certificate is not configured for Inno Setup. Set STORE_SIGNING_CERT_PATH or GTV_SIGNING_CERT_PATH."
}

$certPassword = if ($env:STORE_SIGNING_CERT_PASSWORD -ne $null) { $env:STORE_SIGNING_CERT_PASSWORD }
                elseif ($env:GTV_SIGNING_CERT_PASSWORD -ne $null) { $env:GTV_SIGNING_CERT_PASSWORD }
                elseif ($env:WIN_CERT_PFX_PASSWORD -ne $null) { $env:WIN_CERT_PFX_PASSWORD }
                else { '' }

$timestampServers = @(
    'http://timestamp.digicert.com',
    'http://timestamp.sectigo.com',
    'http://tsa.starfieldtech.com',
    'http://timestamp.globalsign.com/scripts/timstamp.dll'
)

$signed = $false
$lastError = ''
foreach ($tsUrl in $timestampServers) {
    Write-Host "Inno SignTool: Signing '$Path' (timestamp: $tsUrl)..." -NoNewline
    $signArgs = @('sign', '/fd', 'SHA256', '/tr', $tsUrl, '/td', 'SHA256',
                  '/f', $certPath, '/p', $certPassword,
                  '/d', 'GoTV Setup', '/du', 'https://github.com/isthaison/gotv', $Path)

    $output = & $signTool @signArgs 2>&1
    if ($LASTEXITCODE -eq 0) {
        $signed = $true
        Write-Host ' OK' -ForegroundColor Green
        break
    } else {
        $lastError = ($output -join "`n")
        Write-Host ' Retrying with next TSA...' -ForegroundColor Yellow
        Start-Sleep -Milliseconds 400
    }
}

if (-not $signed) {
    throw "signtool failed to sign '$Path':`n$lastError"
}

$allowUntrusted = ($env:GTV_ALLOW_UNTRUSTED_ROOT -in '1', 'true', 'TRUE')
if ($allowUntrusted) {
    $sig = Get-AuthenticodeSignature -LiteralPath $Path
    if (-not $sig -or -not $sig.SignerCertificate) {
        throw "Authenticode signature verification failed for Inno output '$Path'."
    }
    Write-Host "Inno SignTool: Successfully signed and verified '$Path' (Signer: $($sig.SignerCertificate.Subject))." -ForegroundColor Green
} else {
    $vOutput = & $signTool verify /pa /all $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Authenticode verification failed for Inno output '$Path':`n$($vOutput -join "`n")"
    }
    Write-Host "Inno SignTool: Successfully signed and verified '$Path' (Microsoft Trusted Root)." -ForegroundColor Green
}
