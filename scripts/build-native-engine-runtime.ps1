[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("win-x64", "win-arm64")]
    [string] $Rid,

    [string] $Output,
    [string] $PackageVersion,
    [string] $V8Root,
    [string] $V8Workspace
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$v8Revision = "14.7.173.23"
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repoRoot "artifacts/native-engine-runtime"
}
$cpu = if ($Rid -eq "win-arm64") { "arm64" } else { "x64" }
if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    $versionOutput = & dotnet msbuild `
        (Join-Path $repoRoot "src/HtmlML.Core/HtmlML.Core.csproj") `
        -getProperty:PackageVersion -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to evaluate the HtmlML package version."
    }
    $PackageVersion = ($versionOutput | Select-Object -Last 1).Trim()
}
if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    throw "Unable to resolve the native runtime package version."
}

if ([string]::IsNullOrWhiteSpace($V8Root)) {
    if ([string]::IsNullOrWhiteSpace($V8Workspace)) {
        $V8Workspace = Join-Path $repoRoot "artifacts/native-engine-v8/$Rid"
    }
    $depotTools = Join-Path $V8Workspace "depot_tools"
    $V8Root = Join-Path $V8Workspace "v8"
    New-Item -ItemType Directory -Force -Path $V8Workspace | Out-Null
    # depot_tools may run without a persistent global Git configuration on
    # hosted Windows runners. Force LF checkouts for the pinned V8 sources so
    # the upstream ClearScript patches apply identically on every platform.
    $env:GIT_CONFIG_COUNT = "2"
    $env:GIT_CONFIG_KEY_0 = "core.autocrlf"
    $env:GIT_CONFIG_VALUE_0 = "false"
    $env:GIT_CONFIG_KEY_1 = "core.eol"
    $env:GIT_CONFIG_VALUE_1 = "lf"
    if (-not (Test-Path (Join-Path $depotTools ".git"))) {
        & git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git $depotTools
        if ($LASTEXITCODE -ne 0) { throw "Failed to clone depot_tools." }
    }
    $env:Path = "$depotTools;$env:Path"
    $depotToolsGit = Join-Path $depotTools "git.bat"
    if (-not (Test-Path $depotToolsGit)) {
        & (Join-Path $depotTools "bootstrap/win_tools.bat")
        if ($LASTEXITCODE -ne 0) { throw "Failed to bootstrap depot_tools for Windows." }
    }
    $env:DEPOT_TOOLS_UPDATE = "0"
    $env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"

    if (-not (Test-Path (Join-Path $V8Workspace ".gclient"))) {
        Push-Location $V8Workspace
        try { & gclient.bat config https://chromium.googlesource.com/v8/v8 }
        finally { Pop-Location }
        if ($LASTEXITCODE -ne 0) { throw "Failed to configure the V8 checkout." }
    }
    Push-Location $V8Workspace
    try { & gclient.bat sync --no-history -r $v8Revision }
    finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Failed to synchronize V8 $v8Revision." }

    function Apply-PatchOnce([string] $Checkout, [string] $PatchPath) {
        & git -C $Checkout apply --check --ignore-space-change $PatchPath
        if ($LASTEXITCODE -eq 0) {
            & git -C $Checkout apply --ignore-space-change $PatchPath
            if ($LASTEXITCODE -ne 0) { throw "Failed to apply V8 patch '$PatchPath'." }
            return
        }
        & git -C $Checkout apply --reverse --check --ignore-space-change $PatchPath
        if ($LASTEXITCODE -ne 0) {
            throw "Cannot apply or recognize V8 patch '$PatchPath' in '$Checkout'."
        }
    }
    Apply-PatchOnce $V8Root (Join-Path $repoRoot "third-party/clearscript/V8/V8Patch.txt")
    Apply-PatchOnce $V8Root (Join-Path $repoRoot "packaging/HtmlML.NativeEngine.Runtime/patches/V8ToolchainPatch.txt")
    Apply-PatchOnce (Join-Path $V8Root "build") (Join-Path $repoRoot "third-party/clearscript/V8/BuildPatch.txt")
    Apply-PatchOnce (Join-Path $V8Root "third_party/icu") (Join-Path $repoRoot "third-party/clearscript/V8/ICUPatch.txt")

    # Backslash-escaped quotes survive PowerShell's native argument marshalling
    # and reach GN as string delimiters (the form used by ClearScript itself).
    $gnArgs = 'chrome_pgo_phase=0 fatal_linker_warnings=false is_cfi=false is_component_build=false is_debug=false symbol_level=0 target_cpu=\"{0}\" treat_warnings_as_errors=false use_clang_modules=false use_custom_libcxx=false use_thin_lto=false v8_embedder_string=\"-HtmlML\" v8_enable_fuzztest=false v8_enable_pointer_compression=false v8_enable_31bit_smis_on_64bit_arch=false v8_enable_temporal_support=false v8_monolithic=true v8_use_external_startup_data=false v8_target_cpu=\"{0}\"' -f $cpu
    Push-Location $V8Root
    try {
        & gn.bat gen "out/$cpu/Release" "--args=$gnArgs"
        if ($LASTEXITCODE -ne 0) { throw "Failed to generate the V8 build." }
        & ninja.exe -C "out/$cpu/Release" "obj/v8_monolith.lib"
        if ($LASTEXITCODE -ne 0) { throw "Failed to build the V8 monolith." }
    }
    finally { Pop-Location }
}

$v8Monolith = Join-Path $V8Root "out/$cpu/Release/obj/v8_monolith.lib"
$icuData = Join-Path $V8Root "out/$cpu/Release/icudtl.dat"
$v8License = Join-Path $V8Root "LICENSE"
$icuLicense = Join-Path $V8Root "third_party/icu/LICENSE"
@((Join-Path $V8Root "include/v8.h"), $v8Monolith, $icuData, $v8License, $icuLicense) | ForEach-Object {
    if (-not (Test-Path $_)) { throw "Required native runtime input is missing: $_" }
}

$buildDir = Join-Path $repoRoot "artifacts/native-engine-runtime-build/$Rid"
& cmake -S (Join-Path $repoRoot "experiments/HtmlML.NativeEngine.Probe") -B $buildDir `
    -A $(if ($cpu -eq "arm64") { "ARM64" } else { "x64" }) `
    -DHTMLML_NATIVE_ENGINE_ENABLE_V8=ON `
    "-DHTMLML_V8_ROOT=$V8Root"
if ($LASTEXITCODE -ne 0) { throw "Failed to configure the native HtmlML engine." }
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Failed to build the native HtmlML engine." }

$nativePath = Join-Path $buildDir "Release/htmlml_native_engine.dll"
if (-not (Test-Path $nativePath)) { throw "Native engine build did not produce '$nativePath'." }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$packArguments = @(
    "pack", (Join-Path $repoRoot "packaging/HtmlML.NativeEngine.Runtime/HtmlML.NativeEngine.Runtime.csproj"),
    "-c", "Release", "-o", $Output,
    "-p:HtmlMlNativeEngineRid=$Rid",
    "-p:HtmlMlNativeEnginePath=$nativePath",
    "-p:HtmlMlNativeEngineIcuDataPath=$icuData",
    "-p:HtmlMlNativeEngineV8LicensePath=$v8License",
    "-p:HtmlMlNativeEngineIcuLicensePath=$icuLicense"
)
$packArguments += "-p:PackageVersion=$PackageVersion"
& dotnet @packArguments
if ($LASTEXITCODE -ne 0) { throw "Failed to pack the native HtmlML engine." }

$packagePath = Join-Path $Output "HtmlML.NativeEngine.Runtime.$Rid.$PackageVersion.nupkg"
if (-not (Test-Path $packagePath)) { throw "The RID package was not produced at '$packagePath'." }
$packageSmokeDir = Join-Path $buildDir "package-smoke"
if (Test-Path $packageSmokeDir) { Remove-Item -Recurse -Force $packageSmokeDir }
New-Item -ItemType Directory -Force -Path $packageSmokeDir | Out-Null
$packageZip = Join-Path $buildDir "package-smoke.zip"
Copy-Item $packagePath $packageZip -Force
Expand-Archive -Path $packageZip -DestinationPath $packageSmokeDir -Force
$packageNativePath = Join-Path $packageSmokeDir "runtimes/$Rid/native/htmlml_native_engine.dll"

& dotnet run `
    --project (Join-Path $repoRoot "tests/WebPlatformSubset/runner/HtmlML.WebPlatformSubset.Runner.csproj") `
    -c Release -- `
    --engine native `
    --selection required `
    --test contracts/responsive-release-list.html `
    --native-library $packageNativePath `
    --native-cache-directory (Join-Path $buildDir "code-cache") `
    --output (Join-Path $buildDir "wpt-results")
if ($LASTEXITCODE -ne 0) { throw "Native package relocation smoke failed." }

$consumerRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("htmlml-native-consumer-" + [Guid]::NewGuid().ToString("N"))
$consumerDir = Join-Path $consumerRoot "consumer"
$previousPackages = $env:NUGET_PACKAGES
$env:NUGET_PACKAGES = Join-Path $consumerRoot "packages"
try {
    & dotnet new console --framework net8.0 --no-restore --output $consumerDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to create the native package consumer smoke project." }
    $consumerProject = Join-Path $consumerDir "consumer.csproj"
    & dotnet add $consumerProject package "HtmlML.NativeEngine.Runtime.$Rid" `
        --version $PackageVersion --no-restore
    if ($LASTEXITCODE -ne 0) { throw "Failed to add the native runtime package to a consumer." }
    & dotnet restore $consumerProject -r $Rid `
        --source $Output `
        --source https://api.nuget.org/v3/index.json
    if ($LASTEXITCODE -ne 0) { throw "Failed to restore the native runtime package into a consumer." }
    & dotnet build $consumerProject -c Release -r $Rid --no-restore
    if ($LASTEXITCODE -ne 0) { throw "Failed to build the native runtime package consumer." }
    $consumerOutput = Join-Path $consumerDir "bin/Release/net8.0/$Rid"
    @("htmlml_native_engine.dll", "icudtl.dat", "htmlml-native-runtime.json") | ForEach-Object {
        if (-not (Test-Path (Join-Path $consumerOutput $_))) {
            throw "The runtime package did not copy '$_' to consumer output."
        }
    }
}
finally {
    $env:NUGET_PACKAGES = $previousPackages
}

Write-Host "Native runtime: $nativePath"
Write-Host "RID package output: $Output"
