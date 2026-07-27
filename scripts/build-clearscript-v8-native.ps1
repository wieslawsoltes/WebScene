[CmdletBinding()]
param(
    [string] $Source,

    [Parameter(Mandatory = $true)]
    [ValidateSet("win-x86", "win-x64", "win-arm64")]
    [string] $Rid,

    [switch] $ReuseV8,

    [string] $Output
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$patchPaths = @(
    (Join-Path $root "third-party/clearscript-patches/ClearScript-7.5.1-SharedContextSecurityToken.patch"),
    (Join-Path $root "third-party/clearscript-patches/ClearScript-7.5.1-TypedManagedAbi.patch")
)
$packageProject = Join-Path $root "packaging/JavaScript.Avalonia.ClearScript.Native/JavaScript.Avalonia.ClearScript.Native.csproj"
if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Join-Path $root "third-party/clearscript"
}
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $root "artifacts/v8-native"
}

if (-not (Test-Path (Join-Path $Source ".git")) -or
    -not (Test-Path (Join-Path $Source "V8Update.cmd"))) {
    throw "ClearScript source checkout not found at: $Source"
}

$sourceCommit = (& git -C $Source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Cannot read the ClearScript source commit."
}
$sourceBaseCommit = (& git -C $Source rev-parse "7.5.1^{commit}" 2>$null).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceBaseCommit)) {
    throw "ClearScript source does not contain the 7.5.1 base tag: $Source"
}
& git -C $Source merge-base --is-ancestor $sourceBaseCommit $sourceCommit
if ($LASTEXITCODE -ne 0) {
    throw "ClearScript source must descend from the exact 7.5.1 tag; got $sourceCommit."
}
$sourceBranch = (& git -C $Source branch --show-current).Trim()

foreach ($patchPath in $patchPaths) {
    & git -C $Source apply --check $patchPath 2>$null
    if ($LASTEXITCODE -eq 0) {
        & git -C $Source apply $patchPath
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to apply the WebScene ClearScript patch: $patchPath"
        }
        Write-Host "Applied WebScene ClearScript patch: $patchPath"
    }
    else {
        & git -C $Source apply --reverse --check $patchPath 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "Cannot apply or recognize the WebScene patch against ClearScript 7.5.1: $patchPath"
        }
        Write-Host "WebScene ClearScript patch is already applied: $patchPath"
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Installer vswhere.exe was not found."
}
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudio)) {
    throw "Visual Studio with MSBuild was not found."
}
$developerCommand = Join-Path $visualStudio "Common7/Tools/VsDevCmd.bat"

$platform = switch ($Rid) {
    "win-x86" { "Win32" }
    "win-x64" { "x64" }
    "win-arm64" { "ARM64" }
}
$project = Join-Path $Source "ClearScriptV8/$Rid/ClearScriptV8.$Rid.vcxproj"
$v8Arguments = if ($ReuseV8) { "/N Release Tested" } else { "Release Tested" }
$command = "call `"$developerCommand`" -arch=x64 -host_arch=x64 && " +
           "cd /d `"$Source`" && " +
           "call V8Update.cmd $v8Arguments && " +
           "msbuild `"$project`" /m /p:Configuration=Release /p:Platform=$platform"
& $env:ComSpec /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "ClearScript V8 native build failed for $Rid."
}

$nativeName = "ClearScriptV8.$Rid.dll"
$nativePath = Join-Path $Source "bin/Release/$nativeName"
if (-not (Test-Path $nativePath)) {
    throw "Expected native output was not produced: $nativePath"
}

$bytes = [System.IO.File]::ReadAllBytes($nativePath)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "Native output is not a valid PE binary: $nativePath"
}
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length -or
    [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
    throw "Native output has an invalid PE header: $nativePath"
}
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
$expectedMachine = switch ($Rid) {
    "win-x86" { 0x014C }
    "win-x64" { 0x8664 }
    "win-arm64" { 0xAA64 }
}
if ($machine -ne $expectedMachine) {
    throw ("PE machine 0x{0:X4} does not match RID '{1}'." -f $machine, $Rid)
}

New-Item -ItemType Directory -Force -Path $Output | Out-Null
& dotnet pack $packageProject `
    -c Release `
    -o $Output `
    "-p:WebSceneClearScriptNativeRid=$Rid" `
    "-p:WebSceneClearScriptNativePath=$nativePath"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to pack the ClearScript V8 native output for $Rid."
}

Write-Host "ClearScript source: $sourceCommit (base tag 7.5.1, branch $sourceBranch)"
Write-Host "V8 revision: 14.7.173.23"
Write-Host "Native output: $nativePath"
Write-Host "Package output: $Output"
