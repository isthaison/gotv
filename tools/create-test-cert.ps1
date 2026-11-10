<#
.SYNOPSIS
    Generates an Authenticode code-signing certificate for testing and development.

.DESCRIPTION
    Creates an RSA 4096-bit code-signing certificate, exports it to a PKCS#12 (.pfx)
    file, and outputs the Base64 representation suitable for GitHub Secrets
    (WIN_CERT_PFX_B64 or STORE_SIGNING_CERT_PFX_B64).

.PARAMETER OutPfx
    Output path for the .pfx certificate file (default: ./gotv-test-signing.pfx).

.PARAMETER Password
    Password to protect the .pfx file (default: gtv2026).

.PARAMETER Subject
    Subject DN for the certificate (default: CN=GoTV Open Source Code Signing, O=GoTV Project).

.PARAMETER InstallRoot
    If switch is present, imports the certificate to Cert:\CurrentUser\Root so that
    Windows Authenticode verification policy (/pa) trusts it locally without warnings.
#>
[CmdletBinding()]
param(
    [string]$OutPfx = 'gotv-test-signing.pfx',
    [string]$Password = 'gtv2026',
    [string]$Subject = 'CN=GoTV Open Source Code Signing, O=GoTV Project',
    [switch]$InstallRoot
)

$ErrorActionPreference = 'Stop'

Write-Host "Creating Code Signing certificate: $Subject..." -ForegroundColor Cyan

# Code Signing EKU: 1.3.6.1.5.5.7.3.3
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $Subject `
    -KeyLength 4096 `
    -HashAlgorithm SHA256 `
    -KeyAlgorithm RSA `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -NotAfter (Get-Date).AddYears(3)

Write-Host "Certificate generated successfully." -ForegroundColor Green
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host "  Subject    : $($cert.Subject)"
Write-Host "  Valid Until: $($cert.NotAfter)"

$securePassword = ConvertTo-SecureString -String $Password -Force -AsPlainText
$pfxFullPath = [System.IO.Path]::GetFullPath($OutPfx)

Export-PfxCertificate -Cert $cert -FilePath $pfxFullPath -Password $securePassword | Out-Null
Write-Host "Exported PFX to: $pfxFullPath" -ForegroundColor Green

if ($InstallRoot) {
    Write-Host "Installing to Cert:\CurrentUser\Root for local trust..." -ForegroundColor Yellow
    $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store('Root', 'CurrentUser')
    $rootStore.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
    $rootStore.Add($cert)
    $rootStore.Close()
    Write-Host "Certificate installed into CurrentUser\Root. Local Authenticode /pa will now be fully trusted." -ForegroundColor Green
}

# Export Base64 for CI
$pfxBytes = [System.IO.File]::ReadAllBytes($pfxFullPath)
$b64 = [Convert]::ToBase64String($pfxBytes)
$b64Path = "$pfxFullPath.base64.txt"
[System.IO.File]::WriteAllText($b64Path, $b64)

Write-Host ""
Write-Host "------------------------------------------------------------------" -ForegroundColor Cyan
Write-Host "GitHub Actions Secrets Configuration:" -ForegroundColor Cyan
Write-Host "  WIN_CERT_PFX_B64 or STORE_SIGNING_CERT_PFX_B64 saved to:"
Write-Host "  $b64Path" -ForegroundColor Yellow
Write-Host "  Password: $Password" -ForegroundColor Yellow
Write-Host "------------------------------------------------------------------" -ForegroundColor Cyan
