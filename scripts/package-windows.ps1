param(
    [string]$OutputDirectory = 'D:\CODICE\INSTALLER\WINDOWS\MOBIDOC'
)

$ErrorActionPreference = 'Stop'
$englishTranslationPattern = '(?i)_en(?:_[a-z0-9-]+)?\.qm$'
$videoRuntimeFiles = @(
    'aom.dll', 'avcodec-62.dll', 'avdevice-62.dll', 'avfilter-11.dll',
    'avformat-62.dll', 'avutil-60.dll', 'avif.dll', 'dav1d.dll',
    'heif.dll', 'jxl.dll', 'lame_enc.dll', 'libmp3lame.dll',
    'libvpl.dll', 'libx264-165.dll', 'libx265.dll', 'opus.dll',
    'SDL2.dll', 'SvtAv1Enc.dll', 'swresample-6.dll', 'swscale-9.dll',
    'vorbis.dll', 'vorbisenc.dll', 'vorbisfile.dll'
)

function Get-MobiDocRuntimeExclusionReason([string]$RelativePath) {
    $path = $RelativePath.Replace('\', '/')
    if ($path -match '^(?:bin/data/locale|share/locale)/([^/]+)/') {
        if ($Matches[1] -notmatch '^en(?:$|[_@.-])') { return 'non-English locale' }
    }
    if ($path -match '^bin/translations/([^/]+)$') {
        if ($Matches[1] -ne 'catalogs.json' -and $Matches[1] -notmatch $englishTranslationPattern) {
            return 'non-English Qt translation'
        }
    }
    if ($path -match '^bin/imageformats/kimg_[^/]+\.dll$') { return 'non-PDF image format plugin' }
    if ($path -match '^share/vpl/examples/') { return 'VPL example asset' }
    if ($path -match '^bin/(?:multimedia|QtMultimedia)/') { return 'multimedia plugin' }
    if ($path -match '^bin/(?:texttospeech/[^/]+\.dll|QtTextToSpeech/texttospeechqmlplugin\.dll)$') {
        return 'disabled text-to-speech plugin'
    }
    if ($path -eq 'bin/sasl2/saslSASLDB.dll') { return 'optional SASL database plugin' }
    if ($path -match '^bin/Qt6(?:MultimediaQuick|MultimediaWidgets|SpatialAudio)\.dll$') {
        return 'unused multimedia runtime module'
    }
    if ($path -match '^bin/([^/]+)$' -and $videoRuntimeFiles -contains $Matches[1]) {
        return 'video or removed image codec'
    }
    return $null
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$buildRoot = Join-Path $outputRoot '.build-windows'
$craftPrefix = 'D:\CODICE\SDK\KDE-WINDOWS\craft'
$env:MOBIDOC_CRAFT_BUILD_ROOT = Join-Path $buildRoot 'craft-build'
$craftEnvironment = Join-Path $craftPrefix 'craft\craftenv.ps1'
$applicationInSdk = Join-Path $craftPrefix 'bin\MobiDoc.exe'
$blueprint = Join-Path $craftPrefix 'etc\blueprints\locations\craft-blueprints-kde\kde\applications\okular\okular.py'
if (-not (Test-Path -LiteralPath $applicationInSdk)) { throw "Build MobiDoc first: $applicationInSdk" }
if (-not (Test-Path -LiteralPath $craftEnvironment)) { throw "KDE SDK missing: $craftPrefix" }
if (-not (Select-String -LiteralPath $blueprint -SimpleMatch '# MOBIDOC_WINDOWS_PDF_ONLY' -Quiet)) {
    throw 'Run the MOBIDOC build step first to configure Craft.'
}
New-Item -ItemType Directory -Force -Path $outputRoot,$buildRoot | Out-Null
foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
    if ([string]::IsNullOrEmpty($entry.Value)) {
        [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
    }
}
. $craftEnvironment
if ($LASTEXITCODE -ne 0 -or -not $env:KDEROOT) { throw 'Craft environment setup failed.' }
Set-Location -LiteralPath $projectRoot
# The moved SDK's old cmake shim has a stale target. Resolve the real CMake first.
$cmakeBin = Join-Path $craftPrefix 'dev-utils\cmake-base\bin'
if (-not (Test-Path (Join-Path $cmakeBin 'cmake.exe'))) { throw "CMake missing: $cmakeBin" }
$sevenZipBin = Join-Path $craftPrefix 'dev-utils\7z'
$sevenZipExe = Join-Path $sevenZipBin '7za.exe'
if (-not (Test-Path $sevenZipExe)) { throw "7-Zip missing: $sevenZipBin" }
$env:PATH = "$sevenZipBin;$cmakeBin;$env:PATH"
$packageArguments = @(
    '--ci-mode', '--options', "okular.srcDir=$projectRoot",
    '--options', 'okular.version=master', '--options', 'okular.buildTests=False',
    '--options', 'libs/llvm.ignored=True',
    '--options', '[Packager]PackageDebugSymbols=False',
    '--options', '[Packager]PackageType=PortablePackager',
    '--options', '[Packager]7ZipArchiveType=zip',
    '--package', 'okular'
)
$packageStarted = Get-Date
$logPath = Join-Path $buildRoot 'package.log'
try {
    $ErrorActionPreference = 'Continue'
    & craft @packageArguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $logPath
    $craftExit = $LASTEXITCODE
} finally {
    $ErrorActionPreference = 'Stop'
}
if ($craftExit -ne 0) { throw "Runtime packaging failed (exit $craftExit). See $logPath" }
$archive = Get-ChildItem -LiteralPath (Join-Path $craftPrefix 'tmp') -Filter 'okular-*.zip' -File |
    Where-Object { $_.Name -notmatch '-dbg' -and $_.LastWriteTime -ge $packageStarted } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $archive) { throw 'Craft did not create a new portable runtime ZIP.' }

$staging = Join-Path $buildRoot ('.runtime-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
& $sevenZipExe x $archive.FullName "-o$staging" -y | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Portable runtime extraction failed (exit $LASTEXITCODE)." }

$prunedCounts = @{}
$prunedBytes = 0L
foreach ($file in Get-ChildItem -LiteralPath $staging -Recurse -File) {
    $relative = $file.FullName.Substring($staging.Length).TrimStart('\')
    $reason = Get-MobiDocRuntimeExclusionReason $relative
    if ($reason) {
        if (-not $prunedCounts.ContainsKey($reason)) { $prunedCounts[$reason] = 0 }
        $prunedCounts[$reason]++
        $prunedBytes += $file.Length
        Remove-Item -LiteralPath $file.FullName -Force
    }
}
Get-ChildItem -LiteralPath $staging -Directory -Recurse |
    Sort-Object { $_.FullName.Length } -Descending |
    Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } |
    Remove-Item -Force
Write-Host "Removed $($prunedCounts.Values | Measure-Object -Sum | Select-Object -ExpandProperty Sum) runtime files ($([math]::Round($prunedBytes / 1MB, 1)) MiB) for the English-only PDF build."
foreach ($reason in ($prunedCounts.Keys | Sort-Object)) {
    Write-Host "  $reason`: $($prunedCounts[$reason]) files"
}

foreach ($iconName in @('icona.png', 'icona.ico')) {
    $iconSource = Join-Path $projectRoot "icons\$iconName"
    if (-not (Test-Path -LiteralPath $iconSource -PathType Leaf)) {
        throw "MobiDoc program icon missing: $iconSource"
    }
    Copy-Item -LiteralPath $iconSource -Destination (Join-Path $staging $iconName) -Force
}

foreach ($required in @('bin\MobiDoc.exe','bin\KF6Parts.dll','bin\Qt6Core.dll','bin\poppler-qt6.dll','bin\qpdf29.dll','icona.png','icona.ico')) {
    if (-not (Test-Path -LiteralPath (Join-Path $staging $required))) {
        throw "Portable runtime is incomplete: $required"
    }
}
$packageRoot = Join-Path $outputRoot 'Package'
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $packageRoot -Recurse -File) {
    $relative = $file.FullName.Substring($packageRoot.Length).TrimStart('\')
    if (Get-MobiDocRuntimeExclusionReason $relative) {
        Remove-Item -LiteralPath $file.FullName -Force
    }
}
Get-ChildItem -LiteralPath $packageRoot -Directory -Recurse |
    Sort-Object { $_.FullName.Length } -Descending |
    Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } |
    Remove-Item -Force
foreach ($file in Get-ChildItem -LiteralPath $staging -Recurse -File) {
    $relative = $file.FullName.Substring($staging.Length).TrimStart('\')
    $destination = Join-Path $packageRoot $relative
    $same = (Test-Path -LiteralPath $destination -PathType Leaf) -and
        (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -eq
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if (-not $same) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
            throw "Runtime copy verification failed: $destination"
        }
    }
}
$zipDestination = Join-Path $outputRoot 'MobiDoc-Windows-x64.zip'
$trimmedArchive = Join-Path $buildRoot ('.runtime-' + [guid]::NewGuid().ToString('N') + '.zip')
try {
    & $sevenZipExe a -tzip -r $trimmedArchive (Join-Path $staging '*') | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $trimmedArchive)) {
        throw 'Unable to create the trimmed portable ZIP.'
    }
    Copy-Item -LiteralPath $trimmedArchive -Destination $zipDestination -Force
} finally {
    if (Test-Path -LiteralPath $trimmedArchive) { Remove-Item -LiteralPath $trimmedArchive -Force }
}
$rootExe = Join-Path $outputRoot 'MobiDoc.exe'
if (Test-Path -LiteralPath $rootExe) {
    Move-Item -LiteralPath $rootExe -Destination (Join-Path $buildRoot 'MobiDoc-build-only.exe') -Force
}
$debugSymbols = Join-Path $outputRoot 'MobiDoc.pdb'
if (Test-Path -LiteralPath $debugSymbols) {
    Move-Item -LiteralPath $debugSymbols -Destination (Join-Path $buildRoot 'MobiDoc.pdb') -Force
}
# Remove only this run's temporary extraction, after all output has been verified.
$resolvedStaging = [IO.Path]::GetFullPath($staging)
if ((Split-Path -Parent $resolvedStaging) -ne [IO.Path]::GetFullPath($buildRoot).TrimEnd('\') -or
    (Split-Path -Leaf $resolvedStaging) -notmatch '^\.runtime-[0-9a-f]{32}$') {
    throw 'Unexpected staging path; cleanup refused.'
}
Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
Write-Host "Portable application ready: $packageRoot\bin\MobiDoc.exe"
Write-Host "Complete runtime: $packageRoot"
Write-Host "Portable archive: $zipDestination"
