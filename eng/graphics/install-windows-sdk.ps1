# Install the SDK required by the pinned ANGLE Chromium build toolchain.
# Explicitly invoked by hosted CI; never run implicitly on developer machines.
$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'Windows SDK installation requires Windows.' }
$sdkVersion = '10.0.28000.0'
$release = '10.0.28000.2526'
# Microsoft distribution; checksum from Microsoft's winget-pkgs installer manifest.
$url = 'https://download.microsoft.com/download/06fc99ac-527e-451e-a536-8866695a2e7e/KIT_BUNDLE_WINDOWSSDK_MEDIACREATION/winsdksetup.exe'
$expectedHash = '02988EA51EAB2A2DB53E19735E51C97A6D221ADA74B9174FA0868870B9403BA0'
$installer = Join-Path ([IO.Path]::GetTempPath()) "webscene-winsdk-$release.exe"
Invoke-WebRequest -Uri $url -OutFile $installer
if ((Get-FileHash $installer -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Windows SDK installer checksum mismatch.'
}
$signature = Get-AuthenticodeSignature $installer
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    throw 'Windows SDK installer does not have a valid Microsoft signature.'
}
$process = Start-Process -FilePath $installer -ArgumentList '/q', '/norestart' -Wait -PassThru
if ($process.ExitCode -notin @(0, 3010)) { throw "SDK installation failed: $($process.ExitCode)" }
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
foreach ($relative in @(
    "Include/$sdkVersion/um/Windows.h",
    "Include/$sdkVersion/shared/sdkddkver.h",
    "Include/$sdkVersion/ucrt/stdio.h",
    "Lib/$sdkVersion/um/x86/kernel32.lib",
    "Lib/$sdkVersion/um/x64/kernel32.lib",
    "Lib/$sdkVersion/ucrt/x86/ucrt.lib",
    "Lib/$sdkVersion/ucrt/x64/ucrt.lib",
    "bin/$sdkVersion/x64/rc.exe"
)) {
    if (!(Test-Path (Join-Path $sdkRoot $relative))) { throw "SDK installation missing $relative" }
}
Write-Host "Verified Windows SDK $release installer $expectedHash; headers/libraries $sdkVersion at $sdkRoot"
