$ErrorActionPreference = 'Stop'
$source = [IO.Path]::GetFullPath('D:\CODICE\WORK\okular\.build-windows')
$destination = [IO.Path]::GetFullPath('D:\CODICE\SDK\KDE-WINDOWS')
if ($source -ne 'D:\CODICE\WORK\okular\.build-windows' -or
    $destination -ne 'D:\CODICE\SDK\KDE-WINDOWS') { throw 'Unexpected SDK paths' }
if (-not (Test-Path -LiteralPath "$source\craft\bin\Qt6Core.dll")) { throw 'Existing Qt SDK not found' }
if (Test-Path -LiteralPath $destination) { throw 'SDK destination already exists; no files moved' }
$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and $_.Path.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase)
})
if ($running.Count) { throw 'Processes are still using the old SDK' }
$links = @(Get-ChildItem -LiteralPath $source -Recurse -Force -Attributes ReparsePoint | ForEach-Object {
    $link = $_
    if ($link.LinkType -ne 'Junction' -or @($link.Target).Count -ne 1) {
        throw "Unsupported link: $($link.FullName)"
    }
    $target = [IO.Path]::GetFullPath($link.Target)
    if (-not $target.StartsWith($source + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "External junction: $($link.FullName) -> $target"
    }
    [pscustomobject]@{ Relative=$link.FullName.Substring($source.Length + 1); Target=$target.Substring($source.Length + 1) }
})
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
Move-Item -LiteralPath $source -Destination $destination
foreach ($link in $links) {
    $path = [IO.Path]::GetFullPath((Join-Path $destination $link.Relative))
    $target = [IO.Path]::GetFullPath((Join-Path $destination $link.Target))
    if (-not $path.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not $target.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Junction relocation escaped the SDK directory'
    }
    Remove-Item -LiteralPath $path -Force
    New-Item -ItemType Junction -Path $path -Target $target | Out-Null
}
& python (Join-Path $PSScriptRoot 'relocate-windows-sdk.py') $destination
if ($LASTEXITCODE -ne 0) { throw 'SDK metadata relocation needs repair; the files are in the destination' }
Write-Output "SDK moved to $destination; junctions relocated: $($links.Count)"
