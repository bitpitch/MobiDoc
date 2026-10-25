param(
    [switch]$PrepareOnly,
    [switch]$SkipDeps,
    [switch]$RunTests,
    [switch]$Run,
    [ValidateRange(1, 128)][int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = 'D:\CODICE\INSTALLER\WINDOWS\MOBIDOC'
$buildRoot = Join-Path $outputRoot '.build-windows'
$applicationOutput = Join-Path $outputRoot 'MobiDoc.exe'
$craftPrefix = 'D:\CODICE\SDK\KDE-WINDOWS\craft'

# Run this script in a separate PowerShell process: Craft replaces its environment.
function Invoke-Logged {
    param([string]$Log, [scriptblock]$Command)
    $savedPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 turns native stderr into ErrorRecord objects.
        # Preserve the full diagnostic and check the native exit code afterwards.
        $ErrorActionPreference = 'Continue'
        & $Command 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath (Join-Path $buildRoot $Log)
        $commandExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($commandExit -ne 0) {
        throw "Command failed (exit $commandExit). See .build-windows\$Log"
    }
}

if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) {
    throw 'This script requires 64-bit Windows.'
}
if ($PrepareOnly -and $Run) { throw 'Choose either -PrepareOnly or -Run.' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Install Visual Studio 2022 with the Desktop development with C++ workload.'
}
$visualStudio = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio 2022 C++ tools were not found.' }

$python = (Get-Command python -ErrorAction Stop).Source
& $python -c 'import sys; sys.exit(sys.version_info < (3, 11))'
if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 or newer is required.' }
New-Item -ItemType Directory -Force $buildRoot | Out-Null
New-Item -ItemType Directory -Force $outputRoot | Out-Null

if (-not (Test-Path -LiteralPath "$craftPrefix\craft\craftenv.ps1")) {
    throw "KDE Craft SDK missing at $craftPrefix."
}
# Windows PowerShell 5.1 enumerates inherited empty environment variables but
# cannot remove them through its Env: provider. Craft clears that provider;
# remove those empty entries through the Windows API first (this process only).
foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
    if ([string]::IsNullOrEmpty($entry.Value)) {
        [Environment]::SetEnvironmentVariable($entry.Key, $null, 'Process')
    }
}
. "$craftPrefix\craft\craftenv.ps1"
$setupExit = $LASTEXITCODE
Set-Location -LiteralPath $projectRoot
if ($setupExit -ne 0 -or -not $env:KDEROOT) { throw 'Craft environment setup failed.' }
# The old Craft CMake shim embeds its pre-move target path. Prefer the real
# relocatable CMake binary from cmake-base after moving the shared SDK.
$cmakeBin = Join-Path $craftPrefix 'dev-utils\cmake-base\bin'
if (-not (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe'))) {
    throw "CMake is missing from the shared SDK: $cmakeBin"
}
$env:PATH = "$cmakeBin;$env:PATH"
if ($Run) {
    $application = Join-Path $craftPrefix 'bin\MobiDoc.exe'
    if (-not (Test-Path -LiteralPath $application)) { throw 'Build MobiDoc first.' }
    & $application
    exit $LASTEXITCODE
}
if (-not $SkipDeps) {
    Write-Host 'Using D:\CODICE\SDK\KDE-WINDOWS. No new SDK bootstrap or dependency installation will run.'
}
if ($RunTests) { $buildTests = 'True' } else { $buildTests = 'False' }
$env:MOBIDOC_CRAFT_BUILD_ROOT = Join-Path $buildRoot 'craft-build'

$options = @('--options', "okular.srcDir=$projectRoot", '--options', 'okular.version=master',
    '--options', "okular.buildTests=$buildTests", '--options', 'libs/llvm.ignored=True',
    '--options', "[Compile]Jobs=$Jobs")

# Only change the recipe in this project's private Craft environment.
# QPDF must be a runtime dependency, so it is included by future packaging.
$blueprint = Join-Path $craftPrefix 'etc\blueprints\locations\craft-blueprints-kde\kde\applications\okular\okular.py'
$recipe = [IO.File]::ReadAllText($blueprint)
if (-not $recipe.Contains('self.runtimeDependencies["libs/qpdf"]')) {
    $anchor = '        self.runtimeDependencies["qt-libs/poppler"] = None'
    if (-not $recipe.Contains($anchor)) { throw 'The Craft Okular recipe changed: check the QPDF dependency insertion.' }
    $recipe = $recipe.Replace($anchor, $anchor + "`n" + '        self.runtimeDependencies["libs/qpdf"] = None')
    [IO.File]::WriteAllText($blueprint, $recipe, (New-Object Text.UTF8Encoding($false)))
}
& $python (Join-Path $PSScriptRoot 'configure-windows-recipe.py') $blueprint
if ($LASTEXITCODE -ne 0) { throw 'Unable to configure the private MobiDoc Craft recipe.' }

if ($PrepareOnly) {
    Write-Host 'The existing KDE SDK is ready. Run scripts\build-windows.bat to compile.'
    exit 0
}

Invoke-Logged 'build.log' { craft --ci-mode @options --configure --compile --install --qmerge okular }
if ($RunTests) {
    $appBuild = craft --ci-mode @options -q --get 'buildDir()' okular | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'Unable to locate the Okular build directory.' }
    Invoke-Logged 'tests.log' { ctest --test-dir $appBuild.Trim() -R '^(pdfBookmarksTest|documenttest)$' --output-on-failure }
}
if (-not (Test-Path -LiteralPath "$craftPrefix\bin\MobiDoc.exe")) {
    throw 'Craft completed without producing bin\MobiDoc.exe.'
}
Copy-Item -LiteralPath "$craftPrefix\bin\MobiDoc.exe" -Destination $applicationOutput -Force
if ((Get-FileHash -LiteralPath "$craftPrefix\bin\MobiDoc.exe" -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $applicationOutput -Algorithm SHA256).Hash) {
    throw 'The copied MobiDoc.exe does not match the compiled executable.'
}
$debugSymbols = "$craftPrefix\bin\MobiDoc.pdb"
if (Test-Path -LiteralPath $debugSymbols) {
    Copy-Item -LiteralPath $debugSymbols -Destination (Join-Path $outputRoot 'MobiDoc.pdb') -Force
}
Write-Host "Executable ready: $applicationOutput"
Write-Host 'Launch with scripts\build-windows.bat -Run (loads the required KDE environment).'
