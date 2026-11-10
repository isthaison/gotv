<#
.SYNOPSIS
    Signs Windows PE binaries (DLL and EXE) with Authenticode SHA256 digital signature.

.DESCRIPTION
    Scans a file or directory recursively for all .exe and .dll files, signs them using
    signtool.exe with SHA256 and RFC 3161 timestamps (with automatic server fallback and retries),
    and verifies the Authenticode signature.

.PARAMETER Path
    Path to a single PE file or directory containing PE files to sign.

.PARAMETER CertificatePath
    Path to a PKCS#12 (.pfx) certificate file. If omitted, checks environment variables:
    STORE_SIGNING_CERT_PATH, GTV_SIGNING_CERT_PATH, WIN_CERT_PFX_PATH.

.PARAMETER Password
    Password for the certificate file. If omitted, checks environment variables:
    STORE_SIGNING_CERT_PASSWORD, GTV_SIGNING_CERT_PASSWORD, WIN_CERT_PFX_PASSWORD.

.PARAMETER AllowUntrustedRoot
    If set, verification does not enforce Microsoft Trusted Root (useful for self-signed test certs).

.PARAMETER VerifyOnly
    Only verifies existing signatures without modifying files.

.PARAMETER Description
    Description embedded into the signature.

.PARAMETER Url
    URL embedded into the signature.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path,
    [Parameter(Mandatory = $false)]
    [string]$CertificatePath,
    [Parameter(Mandatory = $false)]
    [string]$Password,
    [switch]$AllowUntrustedRoot,
    [switch]$VerifyOnly,
    [string]$Description = 'GoTV Vietnamese Input Method',
    [string]$Url = 'https://github.com/isthaison/gotv'
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
    throw 'Windows SDK signtool.exe was not found. Please install Windows SDK or Visual Studio Build Tools.'
}

# Resolve Certificate and Password if not provided via parameter
if (-not $VerifyOnly) {
    if ([string]::IsNullOrWhiteSpace($CertificatePath)) {
        $CertificatePath = if (-not [string]::IsNullOrWhiteSpace($env:STORE_SIGNING_CERT_PATH)) { $env:STORE_SIGNING_CERT_PATH }
                           elseif (-not [string]::IsNullOrWhiteSpace($env:GTV_SIGNING_CERT_PATH)) { $env:GTV_SIGNING_CERT_PATH }
                           elseif (-not [string]::IsNullOrWhiteSpace($env:WIN_CERT_PFX_PATH)) { $env:WIN_CERT_PFX_PATH }
                           else { $null }
    }
    if ([string]::IsNullOrWhiteSpace($CertificatePath) -or -not (Test-Path -LiteralPath $CertificatePath)) {
        throw "Certificate file not found or not specified ('$CertificatePath'). Set -CertificatePath or STORE_SIGNING_CERT_PATH."
    }

    if ($Password -eq $null) {
        $Password = if ($env:STORE_SIGNING_CERT_PASSWORD -ne $null) { $env:STORE_SIGNING_CERT_PASSWORD }
                    elseif ($env:GTV_SIGNING_CERT_PASSWORD -ne $null) { $env:GTV_SIGNING_CERT_PASSWORD }
                    elseif ($env:WIN_CERT_PFX_PASSWORD -ne $null) { $env:WIN_CERT_PFX_PASSWORD }
                    else { '' }
    }
}

if (-not $AllowUntrustedRoot -and ($env:GTV_ALLOW_UNTRUSTED_ROOT -in '1', 'true', 'TRUE')) {
    $AllowUntrustedRoot = $true
}

$timestampServers = @(
    'http://timestamp.digicert.com',
    'http://timestamp.sectigo.com',
    'http://tsa.starfieldtech.com',
    'http://timestamp.globalsign.com/scripts/timstamp.dll'
)

$target = Get-Item -LiteralPath $Path
if ($target.PSIsContainer) {
    $files = @(Get-ChildItem -LiteralPath $target.FullName -Recurse -File |
        Where-Object { $_.Extension -in '.exe', '.dll' } |
        Sort-Object FullName)
} else {
    $files = @($target)
}

if ($files.Count -eq 0) {
    throw "No PE files (.exe, .dll) found to sign at '$Path'."
}

Write-Host "Found $($files.Count) PE file(s) at '$Path' using signtool '$signTool'." -ForegroundColor Cyan

foreach ($file in $files) {
    if ($file.Extension -notin '.exe', '.dll') {
        throw "Refusing to sign non-PE file '$($file.FullName)'."
    }

    if (-not $VerifyOnly) {
        $signed = $false
        $lastError = ''
        foreach ($tsUrl in $timestampServers) {
            Write-Host "  Signing $($file.Name) (timestamp: $tsUrl)..." -NoNewline
            $signArgs = @('sign', '/fd', 'SHA256', '/tr', $tsUrl, '/td', 'SHA256',
                          '/f', $CertificatePath, '/p', $Password,
                          '/d', $Description, '/du', $Url, $file.FullName)

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
            throw "Failed to sign '$($file.FullName)':`n$lastError"
        }
    }

    # Verify signature
    Write-Host "  Verifying Authenticode: $($file.Name)..." -NoNewline
    if ($AllowUntrustedRoot) {
        $sig = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if (-not $sig -or -not $sig.SignerCertificate) {
            Write-Host ' FAILED' -ForegroundColor Red
            throw "Signature verification failed: File has no valid Authenticode signature ($($sig.StatusMessage))."
        }
        Write-Host " Verified (Signer: $($sig.SignerCertificate.Subject), Status: $($sig.Status))" -ForegroundColor Green
    } else {
        $vOutput = & $signTool verify /pa /all $file.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host ' FAILED' -ForegroundColor Red
            throw "Authenticode verification failed for '$($file.FullName)':`n$($vOutput -join "`n")"
        }
        Write-Host ' Verified (Microsoft Trusted Root)' -ForegroundColor Green
    }
}

Write-Host "All $($files.Count) PE file(s) successfully signed and verified." -ForegroundColor Green
