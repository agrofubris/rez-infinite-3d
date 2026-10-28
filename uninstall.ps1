# Removes Rez Infinite 3D and restores the game's original files.
#
#   powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 [-GameRoot "D:\Games\Rez Infinite"] [-KeepSettings]
param(
    [string]$GameRoot = "",
    [switch]$KeepSettings
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'game-path.ps1')
$GameRoot = Resolve-GameRoot $GameRoot

$manifest = Join-Path $GameRoot 'rez3d-installed.txt'
if (-not (Test-Path $manifest)) { throw "Rez Infinite 3D is not installed in: $GameRoot" }

$restored = 0
$removed = 0
foreach ($line in Get-Content $manifest) {
    $kind, $rel = $line -split '\|', 2
    if (-not $rel) { continue }
    $p = Join-Path $GameRoot $rel
    if ($kind -eq 'replaced' -and (Test-Path "$p.orig")) {
        Move-Item -LiteralPath "$p.orig" -Destination $p -Force
        $restored++
    } elseif ($kind -eq 'added' -and (Test-Path $p)) {
        if ($KeepSettings -and $rel -like '*rez3d.ini') { continue }
        Remove-Item -LiteralPath $p -Force
        $removed++
    }
}
Remove-Item -LiteralPath $manifest -Force

# Logs and debug output written while playing
foreach ($pattern in 'rez3d.log', 'rezstereo.log', 'xinput_proxy.log', 'rez3d_frame_*.bmp', 'rezstereo_frame_*.bmp', 'rezpad.txt') {
    Get-ChildItem -LiteralPath $GameRoot -Recurse -Filter $pattern -File | Remove-Item -Force
}

Write-Host "Restored $restored original file(s) and removed $removed added file(s) in: $GameRoot"
