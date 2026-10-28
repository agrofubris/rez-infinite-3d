# Builds everything and packages a release zip in dist\.
#
#   powershell -ExecutionPolicy Bypass -File .\build.ps1 [-Version 0.1.0]
#
# Needs Visual Studio 2022 or its Build Tools (C++ workload, Windows SDK) and
# CMake. The launcher and test tool use the C# compiler that ships with Windows.
param(
    [string]$Version = "dev"
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
Set-Location $root

function Run([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE)" }
}

Run cmake @('-S', '.', '-B', 'build', '-A', 'x64')
Run cmake @('--build', 'build', '--config', 'Release')
Run cmake @('-S', 'xinput-proxy', '-B', 'xinput-proxy\build', '-A', 'x64')
Run cmake @('--build', 'xinput-proxy\build', '--config', 'Release')

$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
New-Item -ItemType Directory -Force 'build\launcher', 'build\tools' | Out-Null
Run $csc @('-nologo', '-target:winexe', '-r:System.Windows.Forms.dll', '-r:System.Drawing.dll',
           '-out:build\launcher\Rez3D.exe', 'launcher\Rez3DLauncher.cs')
Run $csc @('-nologo', '-out:build\tools\RunHidden.exe', 'tools\RunHidden.cs')

# Release layout: binaries in bin\, scripts and docs at the top
$name = "rez-infinite-3d-$Version"
$stage = Join-Path $root "dist\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force (Join-Path $stage 'bin') | Out-Null
Copy-Item 'build\Release\openvr_api.dll', 'xinput-proxy\build\Release\xinput1_3.dll', 'build\launcher\Rez3D.exe' (Join-Path $stage 'bin')
Copy-Item 'install.ps1', 'uninstall.ps1', 'game-path.ps1', 'rez3d.ini', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md' $stage

$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
Write-Host "Release package: $zip"
