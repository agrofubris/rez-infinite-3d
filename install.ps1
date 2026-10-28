# Installs Rez Infinite 3D into an existing Rez Infinite installation.
#
#   powershell -ExecutionPolicy Bypass -File .\install.ps1 [-GameRoot "D:\Games\Rez Infinite"]
#
# Without -GameRoot it looks in your Steam libraries and GOG installs.
# Replaced game files are kept as *.orig; uninstall.ps1 puts them back.
param(
    [string]$GameRoot = ""
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
. (Join-Path $src 'game-path.ps1')
$GameRoot = Resolve-GameRoot $GameRoot

function Find-Binary([string]$name, [string]$buildPath) {
    foreach ($p in (Join-Path $src "bin\$name"), (Join-Path $src $buildPath)) {
        if (Test-Path $p) { return $p }
    }
    throw "$name not found. Use a release zip, or build first with build.ps1."
}

$shim = Find-Binary 'openvr_api.dll' 'build\Release\openvr_api.dll'
$pad = Find-Binary 'xinput1_3.dll' 'xinput-proxy\build\Release\xinput1_3.dll'
$launcher = Find-Binary 'Rez3D.exe' 'build\launcher\Rez3D.exe'
# Optional: the Leia SR weave (Format=LeiaSR), absent from builds without the SDK
$leia = $null
try { $leia = Find-Binary 'rez3d_leia_sr.dll' 'leia\build\Release\rez3d_leia_sr.dll' } catch {}
$ini = Join-Path $src 'rez3d.ini'

# What this installer touched, for uninstall.ps1: "added|<path>" or "replaced|<path>"
$manifest = Join-Path $GameRoot 'rez3d-installed.txt'
$entries = @()
if (Test-Path $manifest) { $entries = @(Get-Content $manifest | Where-Object { $_ }) }

function Rel([string]$path) { return $path.Substring($GameRoot.Length).TrimStart('\') }
function Note([string]$kind, [string]$path) {
    $rel = Rel $path
    if (-not ($script:entries | Where-Object { $_ -like "*|$rel" })) { $script:entries += "$kind|$rel" }
}

function Install-File([string]$from, [string]$to) {
    $rel = Rel $to
    $known = $script:entries | Where-Object { $_ -like "*|$rel" }
    if ((Test-Path $to) -and -not $known) {
        # An existing .orig is older than the file now in place (an earlier mod
        # or manual install), so it is the one to restore: never overwrite it
        if (-not (Test-Path "$to.orig")) { Copy-Item -LiteralPath $to -Destination "$to.orig" }
        Note 'replaced' $to
    } elseif (-not $known) {
        Note 'added' $to
    }
    Copy-Item -LiteralPath $from -Destination $to -Force
}

# Settings: a reinstall or update keeps the player's own ini
function Install-Ini([string]$dir) {
    $to = Join-Path $dir 'rez3d.ini'
    if (-not (Test-Path $to)) {
        $legacy = Join-Path $dir 'rezstereo.ini'
        if (Test-Path $legacy) {
            Copy-Item -LiteralPath $legacy -Destination $to
        } else {
            Copy-Item -LiteralPath $ini -Destination $to
        }
        Note 'added' $to
    }
}

# Classic Rez (REZ.exe)
$rez = Join-Path $GameRoot 'REZ'
Install-File $shim (Join-Path $rez 'openvr_api.dll')
Install-File $pad (Join-Path $rez 'xinput1_3.dll')
if ($leia) { Install-File $leia (Join-Path $rez 'rez3d_leia_sr.dll') }
Install-Ini $rez

# Area X (UE4)
$axdir = Join-Path $GameRoot 'AX\Engine\Binaries\ThirdParty\OpenVR\OpenVRv1_0_2\Win64'
if (Test-Path $axdir) {
    Install-File $shim (Join-Path $axdir 'openvr_api.dll')
    if ($leia) { Install-File $leia (Join-Path $axdir 'rez3d_leia_sr.dll') }
    Install-Ini $axdir
}
$axbin = Join-Path $GameRoot 'AX\REZAX\Binaries\Win64'
if (Test-Path $axbin) { Install-File $pad (Join-Path $axbin 'xinput1_3.dll') }

# Settings launcher in the game root
Install-File $launcher (Join-Path $GameRoot 'Rez3D.exe')

# Steam copies: started outside Steam, the game restarts itself through Steam and
# loses the vr=1 argument. steam_appid.txt (Valve's documented way to run a Steam
# game straight from its folder) keeps it in this process. Steam must still be
# running and own the game.
$appid = Join-Path $GameRoot 'steam_appid.txt'
if ((Test-Path (Join-Path $GameRoot 'steam_api64.dll')) -and -not (Test-Path $appid)) {
    Set-Content -LiteralPath $appid -Value '636450' -Encoding ASCII -NoNewline
    Note 'added' $appid
}

$entries | Set-Content -LiteralPath $manifest -Encoding UTF8

Write-Host "Installed Rez Infinite 3D into: $GameRoot"
Write-Host ""
Write-Host "Start the game with:  `"$GameRoot\Rez3D.exe`"   (settings + play)"
Write-Host "Pick your display's format there first (passive monitors: interlaced)."
Write-Host "Replaced files are kept as *.orig. To remove everything: uninstall.ps1"
