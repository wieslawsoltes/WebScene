[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $NativeLibrary,
    [string] $OutputDirectory,
    [switch] $VSync,
    [ValidateRange(1, 20)] [int] $StartupRepetitions = 3
)

$ErrorActionPreference = 'Stop'
if (-not [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
    [System.Runtime.InteropServices.OSPlatform]::Windows)) { throw 'This probe requires Windows.' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$NativeLibrary = (Resolve-Path -LiteralPath $NativeLibrary).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'artifacts/windows-kestrel-verification' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$probeProject = Join-Path $repoRoot 'experiments/WebScene.GpuHost.Probe'
$probe = Join-Path $probeProject 'bin/Release/net10.0/WebScene.GpuHost.Probe.dll'
$fixture = Join-Path $repoRoot 'tests/GraphicsCompatibility/fixtures/Kestrel-CAD.zip'
& python (Join-Path $repoRoot 'tests/GraphicsCompatibility/prepare-kestrel.py')
if ($LASTEXITCODE -ne 0) { throw 'Immutable Kestrel fixture verification failed.' }
& dotnet build $probeProject -c Release *> (Join-Path $OutputDirectory 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'GPU host probe build failed; see build.log.' }

$workloads = @(
    @{ Name = 'pan'; Arguments = @('--pan-kestrel'); Marker = 'Kestrel pan workload validated' },
    @{ Name = 'sidebar-resize'; Arguments = @('--sidebar-kestrel', '--resize-kestrel'); Marker = 'Kestrel sidebar workload validated' },
    @{ Name = 'continuous-resize'; Arguments = @('--continuous-resize-kestrel'); Marker = 'Kestrel continuous window resize workload validated' },
    @{ Name = 'edit'; Arguments = @('--edit-kestrel'); Marker = 'Kestrel edit:' }
)
for ($index = 1; $index -le $StartupRepetitions; $index++) {
    $workloads += @{ Name = "startup-$index"; Arguments = @(); Marker = 'Kestrel WebGPU startup check passed' }
}
$previousLibrary = $env:WEBSCENE_TEST_NATIVE_LIBRARY
$results = @()
try {
    $env:WEBSCENE_TEST_NATIVE_LIBRARY = $NativeLibrary
    foreach ($workload in $workloads) {
        $log = Join-Path $OutputDirectory ($workload.Name + '.log')
        $hostMode = if ($VSync) { '--webgpu-vsync' } else { '--webgpu-document' }
        $probeArguments = @($probe, $hostMode, '--kestrel', $fixture, '--verify-kestrel') + $workload.Arguments
        & dotnet @probeArguments *> $log
        $exitCode = $LASTEXITCODE
        $passed = $exitCode -eq 0 -and
            (Select-String -LiteralPath $log -SimpleMatch 'Kestrel WebGPU startup check passed' -Quiet) -and
            (Select-String -LiteralPath $log -SimpleMatch $workload.Marker -Quiet)
        $results += @{ workload = $workload.Name; exitCode = $exitCode; passed = $passed; log = $log }
        Write-Host "$($workload.Name): $(if ($passed) {'passed'} else {'FAILED'})"
        if (-not $passed) { throw "Kestrel workload failed: $log" }
    }
}
finally {
    $env:WEBSCENE_TEST_NATIVE_LIBRARY = $previousLibrary
    @{
        scope = 'Windows Avalonia WebGPU workloads and awaited shutdown; not physical presentation or epic qualification'
        capturedAtUtc = [DateTime]::UtcNow.ToString('o')
        nativeLibrary = $NativeLibrary
        hostMode = $(if ($VSync) { 'compositor-clock' } else { 'default' })
        incrementalCanvasGpu = ($env:WEBSCENE_INCREMENTAL_CANVAS_GPU -eq '1')
        asynchronousCanvasPreparation = ($env:WEBSCENE_ASYNC_CANVAS_PREPARATION -eq '1')
        nativeSha256 = (Get-FileHash -LiteralPath $NativeLibrary -Algorithm SHA256).Hash.ToLowerInvariant()
        fixtureSha256 = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
        workloads = $results
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json')
}
