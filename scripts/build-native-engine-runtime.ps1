[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("win-x64", "win-arm64")]
    [string] $Rid,

    [string] $Output,
    [string] $PackageVersion,
    [string] $V8Root,
    [string] $V8Workspace,
    [string] $V8Revision = "14.7.173.23",
    [switch] $UpstreamV8,
    [switch] $ThinLto,
    [switch] $PartitionAlloc
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$v8Revision = $V8Revision
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $repoRoot "artifacts/native-engine-runtime"
}
$cpu = if ($Rid -eq "win-arm64") { "arm64" } else { "x64" }
$v8Configuration = if ($ThinLto) { "ReleaseThinLto" } else { "Release" }
$v8Configuration += if ($PartitionAlloc) { "PartitionAlloc" } else { "" }
$thinLtoValue = if ($ThinLto) { "true" } else { "false" }
$thinLtoCMake = if ($ThinLto) { "ON" } else { "OFF" }
$partitionAllocValue = if ($PartitionAlloc) { "true" } else { "false" }
$partitionAllocCMake = if ($PartitionAlloc) { "ON" } else { "OFF" }
$buildVariant = if ($ThinLto) { "-thinlto" } else { "" }
$buildVariant += if ($PartitionAlloc) { "-partitionalloc" } else { "" }
if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    $versionOutput = & dotnet msbuild `
        (Join-Path $repoRoot "src/WebScene.Core/WebScene.Core.csproj") `
        -getProperty:PackageVersion -nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to evaluate the WebScene package version."
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
    if (-not $UpstreamV8) {
        Apply-PatchOnce $V8Root (Join-Path $repoRoot "third-party/clearscript/V8/V8Patch.txt")
        Apply-PatchOnce $V8Root (Join-Path $repoRoot "packaging/WebScene.NativeEngine.Runtime/patches/V8ToolchainPatch.txt")
    }
    if ($ThinLto) {
        Apply-PatchOnce $V8Root (Join-Path $repoRoot "packaging/WebScene.NativeEngine.Runtime/patches/V8ThinLtoPatch.txt")
    }
    if ($v8Revision -eq "15.3.10") {
        # V8 15.3 needs two clang-cl/MSVC compatibility fixes: model the
        # ExtendedMap ABI padding in Torque and use V8's FunctionRef directly
        # in the backing-store allocation retry path.
        Apply-PatchOnce $V8Root (Join-Path $repoRoot "packaging/WebScene.NativeEngine.Runtime/patches/V8WindowsCompatibilityPatch.txt")
    }
    if (-not $UpstreamV8) {
        Apply-PatchOnce (Join-Path $V8Root "build") (Join-Path $repoRoot "third-party/clearscript/V8/BuildPatch.txt")
        Apply-PatchOnce (Join-Path $V8Root "third_party/icu") (Join-Path $repoRoot "third-party/clearscript/V8/ICUPatch.txt")
    }

    # Backslash-escaped quotes survive PowerShell's native argument marshalling
    # and reach GN as string delimiters (the form used by ClearScript itself).
    $gnArgs = 'chrome_pgo_phase=0 fatal_linker_warnings=false is_cfi=false is_component_build=false is_debug=false symbol_level=0 target_cpu=\"{0}\" treat_warnings_as_errors=false use_clang_modules=false use_custom_libcxx=false use_thin_lto={1} v8_embedder_string=\"-WebScene\" v8_enable_fuzztest=false v8_enable_partition_alloc={2} v8_enable_pointer_compression=true v8_enable_pointer_compression_shared_cage=true v8_enable_sandbox=false v8_enable_static_roots=false v8_enable_31bit_smis_on_64bit_arch=false v8_enable_temporal_support=false v8_monolithic=true v8_use_external_startup_data=false v8_target_cpu=\"{0}\"' -f $cpu, $thinLtoValue, $partitionAllocValue
    Push-Location $V8Root
    try {
        & gn.bat gen "out/$cpu/$v8Configuration" "--args=$gnArgs"
        if ($LASTEXITCODE -ne 0) { throw "Failed to generate the V8 build." }
        & ninja.exe -C "out/$cpu/$v8Configuration" "obj/v8_monolith.lib"
        if ($LASTEXITCODE -ne 0) { throw "Failed to build the V8 monolith." }
    }
    finally { Pop-Location }
}

$v8OutputRoot = Join-Path $V8Root "out/$cpu/$v8Configuration"
$v8Monolith = Join-Path $v8OutputRoot "obj/v8_monolith.lib"
$icuData = Join-Path $v8OutputRoot "icudtl.dat"
$v8Args = Join-Path $v8OutputRoot "args.gn"
$v8License = Join-Path $V8Root "LICENSE"
$icuLicense = Join-Path $V8Root "third_party/icu/LICENSE"
@((Join-Path $V8Root "include/v8.h"), $v8Monolith, $icuData, $v8Args, $v8License, $icuLicense) | ForEach-Object {
    if (-not (Test-Path $_)) { throw "Required native runtime input is missing: $_" }
}
$hasPointerCompression = Select-String `
    -Path $v8Args `
    -Pattern '^v8_enable_pointer_compression\s*=\s*true$' `
    -Quiet
$hasSharedCage = Select-String `
    -Path $v8Args `
    -Pattern '^v8_enable_pointer_compression_shared_cage\s*=\s*true$' `
    -Quiet
if (-not $hasPointerCompression -or -not $hasSharedCage) {
    throw "The V8 SDK at '$V8Root' is not the required pointer-compressed shared-cage build."
}
$hasRequestedThinLto = Select-String `
    -Path $v8Args `
    -Pattern "^use_thin_lto\s*=\s*$thinLtoValue$" `
    -Quiet
if (-not $hasRequestedThinLto) {
    throw "The V8 SDK at '$v8OutputRoot' does not match requested ThinLTO=$thinLtoValue."
}
$hasRequestedPartitionAlloc = Select-String `
    -Path $v8Args `
    -Pattern "^v8_enable_partition_alloc\s*=\s*$partitionAllocValue$" `
    -Quiet
if (-not $hasRequestedPartitionAlloc) {
    throw "The V8 SDK at '$v8OutputRoot' does not match requested PartitionAlloc=$partitionAllocValue."
}

$buildDir = Join-Path $repoRoot "artifacts/native-engine-runtime-build/$Rid$buildVariant"
& cmake -S (Join-Path $repoRoot "experiments/WebScene.NativeEngine.Probe") -B $buildDir `
    -A $(if ($cpu -eq "arm64") { "ARM64" } else { "x64" }) `
    -DWEBSCENE_NATIVE_ENGINE_ENABLE_V8=ON `
    -DWEBSCENE_V8_POINTER_COMPRESSION=ON `
    -DWEBSCENE_V8_POINTER_COMPRESSION_SHARED_CAGE=ON `
    -DWEBSCENE_V8_OPTIMIZE_FOR_SIZE_DEFAULT=ON `
    "-DWEBSCENE_V8_PARTITION_ALLOC=$partitionAllocCMake" `
    -DWEBSCENE_NATIVE_ENGINE_DENSE_LINK=ON `
    "-DWEBSCENE_NATIVE_ENGINE_THIN_LTO=$thinLtoCMake" `
    -DWEBSCENE_NATIVE_ENGINE_CERTIFICATION=OFF `
    "-DWEBSCENE_V8_ROOT=$V8Root" `
    "-DWEBSCENE_V8_OUTPUT_ROOT=$v8OutputRoot"
if ($LASTEXITCODE -ne 0) { throw "Failed to configure the native WebScene engine." }
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Failed to build the native WebScene engine." }

$nativePath = Join-Path $buildDir "Release/webscene_native_engine.dll"
if (-not (Test-Path $nativePath)) { throw "Native engine build did not produce '$nativePath'." }
$ixWebSocketLicense = Join-Path $buildDir "_deps/webscene_ixwebsocket-src/LICENSE.txt"
$mbedTlsLicense = Join-Path $buildDir "_deps/webscene_mbedtls-src/LICENSE"
if (-not (Test-Path $ixWebSocketLicense)) {
    throw "IXWebSocket license was not found at '$ixWebSocketLicense'."
}
if (-not (Test-Path $mbedTlsLicense)) {
    throw "Mbed TLS license was not found at '$mbedTlsLicense'."
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$packArguments = @(
    "pack", (Join-Path $repoRoot "packaging/WebScene.NativeEngine.Runtime/WebScene.NativeEngine.Runtime.csproj"),
    "-c", "Release", "-o", $Output,
    "-p:WebSceneNativeEngineRid=$Rid",
    "-p:WebSceneNativeEnginePath=$nativePath",
    "-p:WebSceneNativeEngineIcuDataPath=$icuData",
    "-p:WebSceneNativeEngineV8LicensePath=$v8License",
    "-p:WebSceneNativeEngineIcuLicensePath=$icuLicense",
    "-p:WebSceneNativeEngineIXWebSocketLicensePath=$ixWebSocketLicense",
    "-p:WebSceneNativeEngineMbedTlsLicensePath=$mbedTlsLicense",
    "-p:WebSceneNativeEngineV8PointerCompression=true",
    "-p:WebSceneNativeEngineV8SharedCage=true",
    "-p:WebSceneNativeEngineV8OptimizeForSizeDefault=true",
    "-p:WebSceneNativeEngineV8PartitionAlloc=$partitionAllocValue",
    "-p:WebSceneNativeEngineDenseLink=true",
    "-p:WebSceneNativeEngineThinLto=$thinLtoValue",
    "-p:WebSceneNativeEngineV8Revision=$v8Revision"
)
$packArguments += "-p:PackageVersion=$PackageVersion"
& dotnet @packArguments
if ($LASTEXITCODE -ne 0) { throw "Failed to pack the native WebScene engine." }

$packagePath = Join-Path $Output "WebScene.NativeEngine.Runtime.$Rid.$PackageVersion.nupkg"
if (-not (Test-Path $packagePath)) { throw "The RID package was not produced at '$packagePath'." }
$packageSmokeDir = Join-Path $buildDir "package-smoke"
if (Test-Path $packageSmokeDir) { Remove-Item -Recurse -Force $packageSmokeDir }
New-Item -ItemType Directory -Force -Path $packageSmokeDir | Out-Null
$packageZip = Join-Path $buildDir "package-smoke.zip"
Copy-Item $packagePath $packageZip -Force
Expand-Archive -Path $packageZip -DestinationPath $packageSmokeDir -Force
$packageNativePath = Join-Path $packageSmokeDir "runtimes/$Rid/native/webscene_native_engine.dll"

& dotnet run `
    --project (Join-Path $repoRoot "tests/WebPlatformSubset/runner/WebScene.WebPlatformSubset.Runner.csproj") `
    -c Release -- `
    --engine native `
    --selection required `
    --test contracts/responsive-release-list.html `
    --native-library $packageNativePath `
    --native-cache-directory (Join-Path $buildDir "code-cache") `
    --output (Join-Path $buildDir "wpt-results")
if ($LASTEXITCODE -ne 0) { throw "Native package relocation smoke failed." }

$consumerSmokeRoot = Join-Path $repoRoot "artifacts/native-engine-consumer-smoke"
New-Item -ItemType Directory -Force -Path $consumerSmokeRoot | Out-Null
$consumerRoot = Join-Path $consumerSmokeRoot ("consumer-" + [Guid]::NewGuid().ToString("N"))
$consumerDir = Join-Path $consumerRoot "consumer"
$consumerNuGetConfig = Join-Path $consumerRoot "nuget.config"
$previousPackages = $env:NUGET_PACKAGES
$env:NUGET_PACKAGES = Join-Path $consumerRoot "packages"
try {
    & dotnet new nugetconfig --force --output $consumerRoot
    if ($LASTEXITCODE -ne 0) { throw "Failed to create the native package consumer NuGet configuration." }
    & dotnet nuget add source $Output `
        --name local-release `
        --configfile $consumerNuGetConfig
    if ($LASTEXITCODE -ne 0) { throw "Failed to add the local native package source." }
    & dotnet new console --framework net8.0 --no-restore --output $consumerDir
    if ($LASTEXITCODE -ne 0) { throw "Failed to create the native package consumer smoke project." }
    $consumerProject = Join-Path $consumerDir "consumer.csproj"
    & dotnet add $consumerProject package "WebScene.NativeEngine.Runtime.$Rid" `
        --version $PackageVersion --no-restore
    if ($LASTEXITCODE -ne 0) { throw "Failed to add the native runtime package to a consumer." }
    & dotnet restore $consumerProject -r $Rid `
        --configfile $consumerNuGetConfig
    if ($LASTEXITCODE -ne 0) { throw "Failed to restore the native runtime package into a consumer." }
    & dotnet build $consumerProject -c Release -r $Rid --no-restore
    if ($LASTEXITCODE -ne 0) { throw "Failed to build the native runtime package consumer." }
    $consumerOutput = Join-Path $consumerDir "bin/Release/net8.0/$Rid"
    @("webscene_native_engine.dll", "icudtl.dat", "webscene-native-runtime.json") | ForEach-Object {
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
