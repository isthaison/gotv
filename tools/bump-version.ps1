param (
    [Parameter(Mandatory = $true)]
    [string]$Version
)

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Error "Version must be in X.Y.Z format, got '$Version'"
    exit 1
}

$rootDir = Split-Path -Parent $PSScriptRoot
Write-Host "Updating GoTV to version $Version across all platforms..."

# 1. VERSION
Set-Content -Path (Join-Path $rootDir "VERSION") -Value $Version -NoNewline

# 2. web/src/version.ts
$webVersionTs = @"
/**
 * Single source of truth for GoTV Web application version.
 * Synchronized with the root VERSION file.
 */
export const APP_VERSION = '$Version';
"@
Set-Content -Path (Join-Path $rootDir "web\src\version.ts") -Value $webVersionTs

# 3. web/package.json
$pkgJsonPath = Join-Path $rootDir "web\package.json"
if (Test-Path $pkgJsonPath) {
    (Get-Content $pkgJsonPath) -replace '"version": "[^"]*"', "`"version`": `"$Version`"" | Set-Content $pkgJsonPath
}

# 4. windows/version.h
$winVersionH = Join-Path $rootDir "windows\version.h"
if (Test-Path $winVersionH) {
    (Get-Content $winVersionH) `
        -replace '#define GTV_VERSION "[^"]*"', "#define GTV_VERSION `"$Version`"" `
        -replace 'Tags like v[0-9.]*', "Tags like v$Version" | Set-Content $winVersionH
}

# 5. windows/installer.iss
$issPath = Join-Path $rootDir "windows\installer.iss"
if (Test-Path $issPath) {
    (Get-Content $issPath) `
        -replace '#define AppVersion "[^"]*"', "#define AppVersion `"$Version`"" `
        -replace '#define AppVerNum "[^"]*"', "#define AppVerNum `"$Version.0`"" | Set-Content -Encoding utf8 $issPath
}

# 6. windows/resource.rc
$rcPath = Join-Path $rootDir "windows\resource.rc"
if (Test-Path $rcPath) {
    $csv = ($Version -replace '\.', ',') + ',0'
    (Get-Content $rcPath) `
        -replace 'FILEVERSION\s+[0-9,]+', "FILEVERSION     $csv" `
        -replace 'PRODUCTVERSION\s+[0-9,]+', "PRODUCTVERSION  $csv" `
        -replace 'VALUE "(File|Product)Version", "[0-9.]+"', "VALUE `"`$1Version`", `"$Version.0\0`"" | Set-Content $rcPath
}

# 7. macos/GoTVInputController.m & Info.plist
$macosController = Join-Path $rootDir "macos\GoTVInputController.m"
if (Test-Path $macosController) {
    (Get-Content $macosController) -replace 'ver = @"[^"]*"', "ver = @`"$Version`"" | Set-Content $macosController
}
$macosPlist = Join-Path $rootDir "macos\Info.plist"
if (Test-Path $macosPlist) {
    (Get-Content $macosPlist) `
        -replace '<key>CFBundleShortVersionString</key>\s*<string>[^<]*</string>', "<key>CFBundleShortVersionString</key>`n`t<string>$Version</string>" `
        -replace '<key>CFBundleVersion</key>\s*<string>[^<]*</string>', "<key>CFBundleVersion</key>`n`t<string>$Version</string>" | Set-Content $macosPlist
}

# 8. linux
$ibusXml = Join-Path $rootDir "linux\ibus\gotv.xml"
if (Test-Path $ibusXml) {
    (Get-Content $ibusXml) -replace '<version>[^<]*</version>', "<version>$Version</version>" | Set-Content $ibusXml
}
$ibusEngineC = Join-Path $rootDir "linux\ibus\engine.c"
if (Test-Path $ibusEngineC) {
    (Get-Content $ibusEngineC) -replace '"[0-9][0-9.]*","GPL"', "`"$Version`",`"GPL`"" | Set-Content $ibusEngineC
}
$ibusMeta = Join-Path $rootDir "linux\ibus\gotv.metainfo.xml"
if (Test-Path $ibusMeta) {
    $metaContent = Get-Content $ibusMeta -Raw
    if ($metaContent -notmatch "<release version=`"$Version`"") {
        $today = (Get-Date).ToString("yyyy-MM-dd")
        $newRelease = "    <release version=`"$Version`" date=`"$today`">`n      <description>`n        <p>Phát hành ${Version}: Hỗ trợ song ngữ giao diện Tiếng Việt và Tiếng Anh.</p>`n      </description>`n    </release>"
        $metaContent = $metaContent -replace '<releases>', "<releases>`n$newRelease"
        Set-Content -Path $ibusMeta -Value $metaContent
    }
}

# 9. dict tags
Get-ChildItem -Path (Join-Path $rootDir "data\dict-*.txt") | ForEach-Object {
    (Get-Content $_.FullName) -replace '^# dict-tag: v.*', "# dict-tag: v$Version" | Set-Content $_.FullName
}

Write-Host "✅ Version bumped to $Version successfully!"
