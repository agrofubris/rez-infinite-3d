# Shared by install.ps1 and uninstall.ps1: locate a Rez Infinite installation.

function Test-GameRoot([string]$dir) {
    return $dir -and (Test-Path (Join-Path $dir 'REZ\REZ.exe')) -and (Test-Path (Join-Path $dir 'Rez-infinite.exe'))
}

# Steam libraries (libraryfolders.vdf) and GOG installs (registry)
function Find-GameRoot {
    $candidates = @()
    $steam = $null
    foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
        try {
            $item = Get-ItemProperty $key -ErrorAction Stop
            if ($item.SteamPath) { $steam = $item.SteamPath } elseif ($item.InstallPath) { $steam = $item.InstallPath }
        } catch {}
        if ($steam) { break }
    }
    if ($steam) {
        $libs = @($steam)
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $libs += $m.Groups[1].Value -replace '\\\\', '\'
            }
        }
        foreach ($lib in $libs) { $candidates += Join-Path $lib 'steamapps\common\Rez Infinite' }
    }
    foreach ($key in 'HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games') {
        if (Test-Path $key) {
            foreach ($g in Get-ChildItem $key) {
                $p = Get-ItemProperty $g.PSPath
                if ($p.gameName -like 'Rez Infinite*' -and $p.path) { $candidates += $p.path }
            }
        }
    }
    foreach ($c in $candidates) { if (Test-GameRoot $c) { return (Resolve-Path $c).Path } }
    return $null
}

function Resolve-GameRoot([string]$GameRoot) {
    if (-not $GameRoot) { $GameRoot = Find-GameRoot }
    if (-not (Test-GameRoot $GameRoot)) {
        throw "Rez Infinite not found. Run again with -GameRoot `"<folder that contains Rez-infinite.exe>`"."
    }
    return (Resolve-Path $GameRoot).Path
}
