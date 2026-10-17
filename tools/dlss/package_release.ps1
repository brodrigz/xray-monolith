param(
    [string]$ReleaseName = '',
    [string]$RuntimeDirectory = '',
    [string]$TestReport = '',
    [switch]$AllowUncommitted,
    [switch]$FullBundle
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $RuntimeDirectory) { $RuntimeDirectory = Join-Path $repo '_build\dlss_310.9.1' }
$revision = (& git -C $repo -c "safe.directory=$repo" rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision' }
$changedPaths = @(& git -C $repo -c "safe.directory=$repo" diff --name-only HEAD -- src sdk extras gamedata)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source changes' }
$untrackedPaths = @(& git -C $repo -c "safe.directory=$repo" ls-files --others --exclude-standard -- src sdk extras gamedata)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect untracked sources' }
$sourceChanges = @(@($changedPaths + $untrackedPaths) | Sort-Object -Unique | ForEach-Object {
    $sourceFile = Join-Path $repo $_
    [ordered]@{ path=$_; sha256=$(if (Test-Path -LiteralPath $sourceFile -PathType Leaf) { (Get-FileHash -LiteralPath $sourceFile).Hash } else { 'deleted' }) }
})
if ($sourceChanges.Count -and -not $AllowUncommitted) { throw 'Commit source changes or explicitly use -AllowUncommitted for a working-tree test package' }
if (-not $ReleaseName) { $ReleaseName = 'experimental-' + (Get-Date -Format yyyyMMdd) + '-' + $revision.Substring(0,8) }
if ($ReleaseName -notmatch '^[a-zA-Z0-9_.-]+$') { throw 'Unsafe release name' }
$out = Join-Path $repo "_build\releases\$ReleaseName"
if (Test-Path -LiteralPath $out) { throw "Refusing to overwrite existing release: $out" }
$standard = Join-Path $repo '_build\_game\bin_dbg\AnomalyDX11.exe'
$avx = Join-Path $repo '_build\_game\bin_dbg\AnomalyDX11AVX.exe'
$runtime = Join-Path $RuntimeDirectory 'nvngx_dlss.dll'
$runtimeLicense = Join-Path $RuntimeDirectory 'LICENSE.txt'
foreach ($required in $standard,$avx,$runtime,$runtimeLicense,$TestReport) {
    if (-not $required -or -not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required release input is missing: $required"
    }
}
if ((Get-FileHash -LiteralPath $runtime).Hash -ne '3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983') {
    throw 'Unexpected NVIDIA runtime; update the pinned version and documentation deliberately'
}
if ((Get-AuthenticodeSignature -LiteralPath $runtime).Status -ne 'Valid') { throw 'NVIDIA runtime signature is not valid' }
$reportText = Get-Content -LiteralPath $TestReport -Raw
if ($sourceChanges.Count -and -not $reportText.Contains('UNCOMMITTED WORKING-TREE BUILD')) { throw 'Test report must identify uncommitted source changes' }
foreach ($stamp in $revision,(Get-FileHash -LiteralPath $standard).Hash,(Get-FileHash -LiteralPath $avx).Hash) {
    if (-not $reportText.Contains($stamp)) { throw 'Test report does not match the current engine revision/binaries' }
}
$docs = Join-Path $PSScriptRoot 'release'
$utf8 = [Text.UTF8Encoding]::new($false)
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item -LiteralPath (Join-Path $docs 'PUBLISHING_TEXT.txt'),(Join-Path $docs 'PUBLISHER_CHECKLIST.txt') -Destination $out
Copy-Item -LiteralPath $TestReport -Destination (Join-Path $out 'TEST_REPORT.txt')
$packages = @(
    @{ Name="Monolith-DLSS-FSR3-MT-Engine-DX11-$ReleaseName"; Exe=$standard; Variant='DX11' },
    @{ Name="Monolith-DLSS-FSR3-MT-Engine-DX11-AVX-$ReleaseName"; Exe=$avx; Variant='DX11-AVX' },
    @{ Name="Monolith-DLSS-FSR3-GAMMA-Compatibility-$ReleaseName"; Exe=$null; Variant='GAMMA shader/UI override' }
)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveHashes = @()
foreach ($package in $packages) {
    $stage = Join-Path $out $package.Name
    $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Path $stage,$licenses | Out-Null
    Copy-Item -LiteralPath (Join-Path $docs 'INSTALL.txt'),(Join-Path $docs 'CREDITS.txt') -Destination $stage
    Copy-Item -LiteralPath $TestReport -Destination (Join-Path $stage 'TEST_REPORT.txt')
    Copy-Item -LiteralPath (Join-Path $repo 'License.txt') -Destination (Join-Path $licenses 'GSC-Engine.txt')
    Copy-Item -LiteralPath (Join-Path $repo 'sdk\FSR3\LICENSE.txt') -Destination (Join-Path $licenses 'AMD-FidelityFX.txt')
    if ($package.Exe) {
        New-Item -ItemType Directory -Path (Join-Path $stage 'bin') | Out-Null
        Copy-Item -LiteralPath $package.Exe,$runtime -Destination (Join-Path $stage 'bin')
        Copy-Item -LiteralPath $runtimeLicense -Destination (Join-Path $licenses 'NVIDIA-DLSS-Runtime.txt')
        Copy-Item -LiteralPath (Join-Path $repo 'sdk\DLSS\LICENSE.txt') -Destination (Join-Path $licenses 'NVIDIA-NGX-SDK.txt')
        Copy-Item -LiteralPath (Join-Path $repo 'sdk\FSR3\Third_party_notices.txt') -Destination (Join-Path $licenses 'FidelityFX-Third-Party.txt')
        Copy-Item -LiteralPath (Join-Path $repo 'src\3rd party\cximage\license.txt') -Destination (Join-Path $licenses 'CxImage.txt')
    } else {
        Copy-Item -LiteralPath (Join-Path $repo 'extras\dlss_gamma\gamedata') -Destination (Join-Path $stage 'gamedata') -Recurse
    }
    $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length + 1).Replace('\','/')
        if ($relative -match '(?i)(savedgames|appdata|shader_cache|fsgame\.ltx|meta\.ini|\.pdb$|\.scoc$|\.scop$|\.log$)') {
            throw "Private/development artifact in package: $relative"
        }
        [ordered]@{ path=$relative; bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName).Hash }
    })
    $manifest = [ordered]@{
        release=$ReleaseName
        source_revision=$revision
        source_state=$(if ($sourceChanges.Count) { 'uncommitted working-tree test build; revision is the base commit only' } else { 'committed' })
        source_changes=$sourceChanges
        source_branch='feat/dlss-sr-mt'
        upstream_base='7b79d2be'
        variant=$package.Variant
        configuration='Release x64; engine solution DX11 or DX11-AVX as named'
        dlss_runtime='310.9.1.0'
        fsr_upscaler='3.1.2 community D3D11'
        frame_generation=$false
        note='Manifest covers payload files, excluding this manifest itself. No gameplay/FPS guarantee.'
        files=$files
    }
    [IO.File]::WriteAllText((Join-Path $stage 'MANIFEST.json'),($manifest | ConvertTo-Json -Depth 7),$utf8)
    $zipPath = Join-Path $out ($package.Name + '.zip')
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zipPath,[IO.Compression.CompressionLevel]::Optimal,$false)
    # Verify actual archive payload, not only the loose staging files.
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        foreach ($file in $files) {
            $entry = $zip.GetEntry($file.path)
            if (-not $entry -or $entry.Length -ne $file.bytes) { throw "Archive mismatch: $($file.path)" }
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
            finally { $sha.Dispose(); $stream.Dispose() }
            if ($hash -ne $file.sha256) { throw "Archive hash mismatch: $($file.path)" }
        }
        if ($zip.Entries.Count -ne $files.Count + 1) { throw 'Unexpected extra ZIP entries' }
    } finally { $zip.Dispose() }
    $archiveHashes += (Get-FileHash -LiteralPath $zipPath).Hash + '  ' + [IO.Path]::GetFileName($zipPath)
    Write-Output "Verified $($package.Name).zip ($($files.Count) payload files)"
}
[IO.File]::WriteAllLines((Join-Path $out 'SHA256SUMS.txt'),$archiveHashes,$utf8)
if ($FullBundle) {
    $bundleStage = Join-Path $out 'full-bundle'
    New-Item -ItemType Directory -Path $bundleStage | Out-Null
    foreach ($package in $packages) {
        Copy-Item -LiteralPath (Join-Path $out ($package.Name + '.zip')) -Destination $bundleStage
    }
    Copy-Item -LiteralPath (Join-Path $docs 'INSTALL.txt') -Destination $bundleStage
    Copy-Item -LiteralPath (Join-Path $out 'TEST_REPORT.txt'),(Join-Path $out 'SHA256SUMS.txt') -Destination $bundleStage
    $bundleFiles = @(Get-ChildItem -LiteralPath $bundleStage -File)
    $bundlePath = Join-Path $out "Monolith-DLSS-FSR3-MT-Full-$ReleaseName.zip"
    [IO.Compression.ZipFile]::CreateFromDirectory($bundleStage,$bundlePath,[IO.Compression.CompressionLevel]::NoCompression,$false)
    $bundleZip = [IO.Compression.ZipFile]::OpenRead($bundlePath)
    try {
        if ($bundleZip.Entries.Count -ne $bundleFiles.Count) { throw 'Unexpected full bundle entries' }
        foreach ($file in $bundleFiles) {
            $entry = $bundleZip.GetEntry($file.Name)
            if (-not $entry -or $entry.Length -ne $file.Length) { throw "Bundle mismatch: $($file.Name)" }
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
            finally { $sha.Dispose(); $stream.Dispose() }
            if ($hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) { throw "Bundle hash mismatch: $($file.Name)" }
        }
    } finally { $bundleZip.Dispose() }
    $bundleHash = (Get-FileHash -LiteralPath $bundlePath).Hash + '  ' + [IO.Path]::GetFileName($bundlePath)
    [IO.File]::WriteAllLines((Join-Path $out 'FULL-BUNDLE-SHA256.txt'),@($bundleHash),$utf8)
    Write-Output "Verified full bundle: $bundlePath"
}
Write-Output "Release output: $out"
