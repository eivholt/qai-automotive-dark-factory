[CmdletBinding()]
param([string]$Archive = '')
$ErrorActionPreference = 'Stop'
$Repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$Url = 'https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/dlss/UE-DLSS-5.8/8.7.2/2026.07.21_UE5.8_DLSS4.5Plugin_v8.7.2.zip'
$ExpectedHash = 'CAEC541CE620CA60E455151E905E0E17CF01BD7337708FF5D3CAE24FC9038BEF'
$Destination = [IO.Path]::GetFullPath((Join-Path $Repo 'unreal\Plugins\NVIDIA'))
if (-not $Archive) { $Archive = Join-Path $Repo 'assets\runtime_plugins\2026.07.21_UE5.8_DLSS4.5Plugin_v8.7.2.zip' }
$Archive = [IO.Path]::GetFullPath($Archive)
if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Archive)) | Out-Null
    Write-Host 'Downloading the official NVIDIA UE 5.8 DLSS 4.5 plugin (about 757 MB).'
    Invoke-WebRequest -Uri $Url -OutFile $Archive -UseBasicParsing
}
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -cne $ExpectedHash) { throw 'Archive hash differs from the recorded official 8.7.2 release. No plugin files were installed.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Zip = [IO.Compression.ZipFile]::OpenRead($Archive)
try {
    $Entries = @($Zip.Entries | Where-Object { $_.FullName.StartsWith('Plugins/DLSS/') -or $_.FullName.StartsWith('Plugins/StreamlineNGXCommon/') })
    if ($Entries.Count -lt 100) { throw 'Required DLSS / StreamlineNGXCommon plugin entries are missing.' }
    foreach ($Entry in $Entries) {
        if ($Entry.FullName.EndsWith('/')) { continue }
        $Relative = $Entry.FullName.Substring('Plugins/'.Length)
        $Target = [IO.Path]::GetFullPath((Join-Path $Destination $Relative))
        if (-not $Target.StartsWith($Destination+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe ZIP path: $Relative" }
        # Preserve locally rebuilt binaries and source. An existing complete
        # compatible installation is left alone; partial installs are rejected.
    }
    $DLSS = Join-Path $Destination 'DLSS\DLSS.uplugin'
    $Common = Join-Path $Destination 'StreamlineNGXCommon\StreamlineNGXCommon.uplugin'
    if (Test-Path -LiteralPath $Destination) {
        if ((Test-Path -LiteralPath $DLSS -PathType Leaf) -and (Test-Path -LiteralPath $Common -PathType Leaf)) {
            $Descriptor = Get-Content -LiteralPath $DLSS -Raw -Encoding utf8 | ConvertFrom-Json
            if ($Descriptor.VersionName -ceq '8.7.2-NGX310.6.0' -and $Descriptor.EngineVersion -ceq '5.8.0') {
                Write-Host "Compatible official DLSS plugin already installed: $Destination"
                return
            }
        }
        throw "An existing partial or different plugin installation is preserved at $Destination. Move it aside explicitly before installing."
    }
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($Entry in $Entries) {
        if ($Entry.FullName.EndsWith('/')) { continue }
        $Target = [IO.Path]::GetFullPath((Join-Path $Destination $Entry.FullName.Substring('Plugins/'.Length)))
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Target)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($Entry,$Target,$false)
    }
    Write-Host "Installed official DLSS 8.7.2 for UE 5.8: $Destination"
    Write-Host 'Rebuild the project. NVIDIA binaries/source remain private and ignored; frame generation and Ray Reconstruction are not enabled by the demo.'
} finally { $Zip.Dispose() }
