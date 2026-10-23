<#
    monday-night-net: An amateur radio net monitoring utility in gtk4
    Copyright (C) 2026  Andrew Potter

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
#>
<#
    Stages mnn (stage.ps1) and wraps the stage in a per-user .msi with WiX
    (mnn.wxs): build-msvc\monday-night-net-<version>-x64.msi. Run through
    gtkenv.cmd:

        C:\dev\gtkenv.cmd powershell -ExecutionPolicy Bypass -File build-aux\windows\make-msi.ps1

    wix comes from ~\AsusMiku\setup-installer-tools.ps1.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\..\build-msvc')
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }

function Invoke-Native {
    param([scriptblock]$Command)
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $prevEap }
}

$BuildDir = (Resolve-Path $BuildDir).Path
$stage = Join-Path $BuildDir 'stage'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

# A global tool lands in ~\.dotnet\tools, which an ssh session that started
# before setup-installer-tools.ps1 ran does not have on PATH. Its apphost
# finds the SDK through DOTNET_ROOT.
$wix = Join-Path $env:USERPROFILE '.dotnet\tools\wix.exe'
if (-not (Test-Path $wix)) { throw "no $wix; run setup-installer-tools.ps1 from the desktop" }
if (-not $env:DOTNET_ROOT) { $env:DOTNET_ROOT = [Environment]::GetEnvironmentVariable('DOTNET_ROOT', 'User') }

& (Join-Path $PSScriptRoot 'stage.ps1') -BuildDir $BuildDir -Stage $stage
if ($LASTEXITCODE -ne 0) { throw 'stage.ps1 failed' }

# MSI versions are numeric: major.minor.build.
$info = @(Invoke-Native { meson introspect --projectinfo $BuildDir }) -join "`n" | ConvertFrom-Json
$version = $info.version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "project version '$version' is not numeric x.y.z, which an MSI needs" }

Write-Step "wix build $version"
$msi = Join-Path $BuildDir "monday-night-net-$version-x64.msi"
# ICE38, ICE64 and ICE91 assume a per-machine package: they want every
# component under the user profile keyed by an HKCU value and every
# directory there listed for removal. Harvested files are keyed by
# themselves, and a per-user package only ever installs for one user.
$out = @(Invoke-Native { & $wix build -arch x64 -pdbtype none `
            -d "Version=$version" -d "StageDir=$stage" `
            -d "IconFile=$(Join-Path $repo 'res\radio.ki6kvz.MondayNightNet.ico')" `
            -sice ICE38 -sice ICE64 -sice ICE91 `
            -o $msi (Join-Path $PSScriptRoot 'mnn.wxs') })
$out | ForEach-Object { Write-Host "    $_" }
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $msi)) { throw 'wix build failed' }

Write-Step ("{0} ({1:N1} MB)" -f $msi, ((Get-Item $msi).Length / 1MB))
