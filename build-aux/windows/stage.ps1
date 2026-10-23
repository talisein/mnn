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
    Builds build-msvc\stage\: mnn and everything it loads, laid out as
    bin\ lib\ share\ so that GLib, GTK and gdk-pixbuf find their data
    relative to their DLLs. The .msi (make-msi.ps1) and a later MSIX are thin
    wrappers around this folder; see README.md for the rules it keeps.

    Run through gtkenv.cmd, which puts dumpbin, meson and the GTK prefix on
    PATH:

        C:\dev\gtkenv.cmd powershell -ExecutionPolicy Bypass -File build-aux\windows\stage.ps1

    It starts from an empty stage every time and ends with a self-check.
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$Stage,
    [string]$GtkPrefix = $env:GTKPREFIX
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Good { param([string]$m) Write-Host "  $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "  ! $m" -ForegroundColor Yellow }
function Write-Bad  { param([string]$m) Write-Host "  X $m" -ForegroundColor Red }

# Native stderr would throw under 'Stop'. Exit codes are checked through
# $LASTEXITCODE.
function Invoke-Native {
    param([scriptblock]$Command)
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $prevEap }
}

function Copy-Into {
    param([string]$Source, [string]$DestDir)
    if (-not (Test-Path $DestDir)) { New-Item -ItemType Directory -Path $DestDir | Out-Null }
    Copy-Item -Path $Source -Destination $DestDir -Recurse -Force
}

# Windows PowerShell 5.1 leaves $PSScriptRoot empty in param defaults.
if (-not $BuildDir) { $BuildDir = Join-Path $PSScriptRoot '..\..\build-msvc' }
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $Stage) { $Stage = Join-Path $BuildDir 'stage' }
if (-not $GtkPrefix -or -not (Test-Path (Join-Path $GtkPrefix 'bin'))) {
    throw "no GTK prefix ('$GtkPrefix'); run this through C:\dev\gtkenv.cmd"
}
if (-not (Get-Command dumpbin.exe -ErrorAction SilentlyContinue)) {
    throw 'no dumpbin on PATH; run this through C:\dev\gtkenv.cmd'
}
$gtkBin = Join-Path $GtkPrefix 'bin'
$system32 = Join-Path $env:SystemRoot 'System32'
$stageBin = Join-Path $Stage 'bin'

# --------------------------------------------------------------- install
Write-Step "meson install into $Stage"

if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
# runtime and i18n leave out headers, import libraries, pkg-config files and
# GIRs, and the desktop and metainfo files, which mean nothing on Windows.
$out = @(Invoke-Native { meson install -C $BuildDir --tags runtime,i18n --destdir $Stage })
if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host "    $_" }; throw 'meson install failed' }

# meson's Windows default prefix is C:\, which DESTDIR turns into the stage
# root. Any other prefix would nest the tree a level down.
if (-not (Test-Path (Join-Path $stageBin 'monday-night-net.exe'))) {
    throw "meson install left no bin\monday-night-net.exe in $Stage; configure $BuildDir with the default prefix"
}

# What the tags still let through: debug symbols, the sqlite module's import
# library, and typelibs that nothing loads at run time.
Get-ChildItem -Path $Stage -Recurse -Include '*.pdb', '*.lib' | Remove-Item -Force
foreach ($d in @('include', 'lib\pkgconfig', 'lib\cmake', 'lib\girepository-1.0', 'libexec',
                 'share\gir-1.0', 'share\applications', 'share\metainfo')) {
    $p = Join-Path $Stage $d
    if (Test-Path $p) { Remove-Item -Recurse -Force $p }
}
Write-Good 'installed'

# --------------------------------------------------------------- plugins
Write-Step 'Plugins'

# Loaded with LoadLibrary at run time, so no import table names them.
# - gioopenssl: the TLS backend. libsoup has no https without it. It takes
#   its trust roots from the Windows certificate store, so no CA file.
# - pixbufloader_svg: librsvg, for the SVG icons. gvsbuild builds PNG and
#   JPEG into gdk-pixbuf itself.
# - gspawn helpers: GLib runs them for g_spawn_*() on Windows.
# giognomeproxy is left out: it reads GNOME's proxy settings, which nothing
# sets on Windows.
$plugins = [ordered]@{
    'lib\gio\modules\gioopenssl.dll'                           = 'lib\gio\modules'
    'lib\gdk-pixbuf-2.0\2.10.0\loaders\pixbufloader_svg.dll'   = 'lib\gdk-pixbuf-2.0\2.10.0\loaders'
    'bin\gspawn-win64-helper.exe'                              = 'bin'
    'bin\gspawn-win64-helper-console.exe'                      = 'bin'
}
foreach ($p in $plugins.Keys) {
    $src = Join-Path $GtkPrefix $p
    if (-not (Test-Path $src)) { throw "no $src" }
    Copy-Into $src (Join-Path $Stage $plugins[$p])
    Write-Good $p
}

# ------------------------------------------------------------ DLL closure
Write-Step 'DLL closure'

# The MSVC runtime ships app-local: never the VC++ redist installer, which
# would be an install-time action. vcvars does not always set
# VCToolsRedistDir, so fall back to the newest redist under VCINSTALLDIR.
$crtDir = $null
$redistRoots = @()
if ($env:VCToolsRedistDir) { $redistRoots += $env:VCToolsRedistDir }
if ($env:VCINSTALLDIR) {
    $redistRoots += @(Get-ChildItem -Path (Join-Path $env:VCINSTALLDIR 'Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
                      Sort-Object { try { [version]$_.Name } catch { [version]'0.0' } } -Descending |
                      ForEach-Object FullName)
}
foreach ($r in $redistRoots) {
    $c = @(Get-ChildItem -Path (Join-Path $r 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue)
    if ($c.Count -gt 0) { $crtDir = $c[0].FullName; break }
}
if (-not $crtDir) { throw "no x64\Microsoft.VC*.CRT under $($redistRoots -join ', ')" }
Write-Good "MSVC runtime from $crtDir"

function Get-PeImports {
    param([string]$Path)
    $lines = @(Invoke-Native { dumpbin.exe /nologo /dependents $Path })
    if ($LASTEXITCODE -ne 0) { throw "dumpbin /dependents $Path failed: $($lines -join ' ')" }
    # Both the import and delay-load lists: a delay-loaded DLL is still
    # loaded the first time it is used.
    return @($lines | ForEach-Object {
        $m = [regex]::Match($_, '^\s{4}(\S+\.dll)\s*$', 'IgnoreCase')
        if ($m.Success) { $m.Groups[1].Value }
    })
}

function Test-SystemDll {
    param([string]$Name)
    if ($Name -match '^(api-ms-win-|ext-ms-)') { return $true }
    return (Test-Path (Join-Path $system32 $Name))
}

function Get-StagedPe {
    return @(Get-ChildItem -Path $Stage -Recurse -File -Include '*.exe', '*.dll')
}

# The runtime the closure will reach anyway, named so that it is always
# there even if a future build stops importing one of them directly.
foreach ($n in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
    Copy-Into (Join-Path $crtDir $n) $stageBin
}

# Every DLL lands in bin\. Plugins under lib\ resolve their imports from
# the exe's directory, not their own.
$queue = New-Object System.Collections.Generic.Queue[string]
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$missing = New-Object System.Collections.Generic.List[string]
foreach ($f in Get-StagedPe) { $queue.Enqueue($f.FullName); [void]$seen.Add($f.Name) }
while ($queue.Count -gt 0) {
    $pe = $queue.Dequeue()
    foreach ($dll in Get-PeImports $pe) {
        if (-not $seen.Add($dll)) { continue }
        if (Test-Path (Join-Path $stageBin $dll)) { continue }
        # GTK's prefix and the CRT come before System32: the build box has
        # the VC++ runtime installed system-wide, a clean machine does not.
        # libshumate, built by meson rather than gvsbuild, installs its DLL
        # into the prefix's lib\.
        $src = $null
        foreach ($dir in @($gtkBin, (Join-Path $GtkPrefix 'lib'), $crtDir)) {
            if (Test-Path (Join-Path $dir $dll)) { $src = Join-Path $dir $dll; break }
        }
        if ($src) {
            Copy-Into $src $stageBin
            $queue.Enqueue((Join-Path $stageBin $dll))
        } elseif (-not (Test-SystemDll $dll)) {
            $missing.Add("$dll (needed by $pe)")
        }
    }
}
if ($missing.Count -gt 0) { $missing | ForEach-Object { Write-Bad $_ }; throw 'DLLs found neither in the GTK prefix nor in System32' }
Write-Good "$(@(Get-ChildItem -Path $stageBin -Filter '*.dll').Count) DLLs in bin\"

# ------------------------------------------------------------------ data
Write-Step 'Data'

# GSettings schemas: GLib's, GTK's and gsettings-desktop-schemas', next to
# mnn's from meson install. Compiled below.
# The wallpaper schemas are left out: their defaults are file:// URIs into
# the GTK prefix, and nothing on Windows reads them.
$schemaDir = Join-Path $Stage 'share\glib-2.0\schemas'
Copy-Into (Join-Path $GtkPrefix 'share\glib-2.0\schemas\*.xml') $schemaDir
foreach ($s in @('org.gnome.desktop.background.gschema.xml', 'org.gnome.desktop.screensaver.gschema.xml')) {
    Remove-Item -Force (Join-Path $schemaDir $s)
}

# Icons. hicolor is the fallback theme that holds mnn's own icon. Of
# Adwaita, only the symbolic icons: GTK 4 and libadwaita use symbolic icons
# for their own UI, and the full theme is mostly cursors, which Windows
# draws itself.
$iconsSrc = Join-Path $GtkPrefix 'share\icons'
$iconsDst = Join-Path $Stage 'share\icons'
Copy-Into (Join-Path $iconsSrc 'hicolor\index.theme') (Join-Path $iconsDst 'hicolor')
Copy-Into (Join-Path $iconsSrc 'Adwaita\index.theme') (Join-Path $iconsDst 'Adwaita')
Copy-Into (Join-Path $iconsSrc 'Adwaita\symbolic') (Join-Path $iconsDst 'Adwaita')

# Translations of the libraries whose strings reach the UI or its error
# messages. mnn's own come from meson install.
$domains = @('gtk40', 'libadwaita', 'glib20', 'gdk-pixbuf', 'glib-networking', 'libsoup-3.0', 'shumate1')
$localeSrc = Join-Path $GtkPrefix 'share\locale'
$nMo = 0
foreach ($lang in Get-ChildItem -Path $localeSrc -Directory) {
    foreach ($d in $domains) {
        $mo = Join-Path $lang.FullName "LC_MESSAGES\$d.mo"
        if (Test-Path $mo) {
            Copy-Into $mo (Join-Path $Stage "share\locale\$($lang.Name)\LC_MESSAGES")
            $nMo++
        }
    }
}
Write-Good "$nMo .mo files"

# ---------------------------------------------------------------- caches
Write-Step 'Caches'

# Nothing runs at install time, so every cache is built here. None of them
# may hold an absolute path.
$null = Invoke-Native { & (Join-Path $gtkBin 'glib-compile-schemas.exe') $schemaDir }
if ($LASTEXITCODE -ne 0) { throw 'glib-compile-schemas failed' }
Write-Good 'gschemas.compiled'

# giomodule.cache holds only file names.
$null = Invoke-Native { & (Join-Path $gtkBin 'gio-querymodules.exe') (Join-Path $Stage 'lib\gio\modules') }
if ($LASTEXITCODE -ne 0) { throw 'gio-querymodules failed' }
Write-Good 'giomodule.cache'

# GDK-PIXBUF-QUERY-LOADERS WRITES PATHS RELATIVE TO ITS OWN TOPLEVEL, not to
# the loaders it is given. Run from the GTK prefix it would write absolute
# stage paths, so it runs from the stage's bin\, against the stage's DLLs,
# and is removed afterwards.
$gpql = Join-Path $stageBin 'gdk-pixbuf-query-loaders.exe'
Copy-Item (Join-Path $gtkBin 'gdk-pixbuf-query-loaders.exe') $gpql
$savedEnv = @{}
foreach ($v in @('GDK_PIXBUF_MODULEDIR', 'GDK_PIXBUF_MODULE_FILE')) {
    $savedEnv[$v] = [Environment]::GetEnvironmentVariable($v)
    [Environment]::SetEnvironmentVariable($v, $null)
}
$savedPath = $env:PATH
$env:PATH = "$stageBin;$system32;$env:SystemRoot"
try {
    $null = Invoke-Native { & $gpql --update-cache }
    $rc = $LASTEXITCODE
} finally {
    $env:PATH = $savedPath
    foreach ($v in $savedEnv.Keys) { [Environment]::SetEnvironmentVariable($v, $savedEnv[$v]) }
    Remove-Item -Force $gpql
}
if ($rc -ne 0) { throw 'gdk-pixbuf-query-loaders failed' }
$loadersCache = Join-Path $Stage 'lib\gdk-pixbuf-2.0\2.10.0\loaders.cache'
if (-not (Test-Path $loadersCache)) { throw "gdk-pixbuf-query-loaders wrote no $loadersCache" }
# Its header comment names the absolute loader directory; nothing reads it.
$text = [IO.File]::ReadAllText($loadersCache)
$text = [regex]::Replace($text, '(?m)^# LoaderDir = .*\r?\n', '')
[IO.File]::WriteAllText($loadersCache, $text)
if ($text -notmatch '(?m)^"lib\\\\gdk-pixbuf-2\.0\\\\2\.10\.0\\\\loaders\\\\pixbufloader_svg\.dll"') {
    throw "loaders.cache does not list the svg loader by a relative path:`n$text"
}
Write-Good 'loaders.cache'

foreach ($theme in @('hicolor', 'Adwaita')) {
    $null = Invoke-Native { & (Join-Path $gtkBin 'gtk4-update-icon-cache.exe') -q -t -f (Join-Path $iconsDst $theme) }
    if ($LASTEXITCODE -ne 0) { throw "gtk4-update-icon-cache $theme failed" }
}
Write-Good 'icon-theme.cache (hicolor, Adwaita)'

# ------------------------------------------------------------ self-check
Write-Step 'Self-check'

$failed = 0

# 1. No build or prefix paths that anything opens at run time.
#    - Source files and .pdb paths are debug info and assertion messages:
#      __FILE__, the CodeView record, Rust's panic locations. They are never
#      opened, and are allowed everywhere.
#    - Any other path is a lookup. The GTK stack's compiled-in prefix
#      directories are allowed per file, each with the reason it is never
#      used on Windows. mnn's own files allow none.
$needles = @('c:\gtk-build', 'c:/gtk-build', 'c:\dev', 'c:/dev')
# gvsbuild's build\ and tools\ (cargo's registry) hold only sources; Rust's
# strings are not NUL-terminated, so a panic location can run straight into
# the next string and hide its extension.
$debugPath = '\.(c|cc|cpp|h|hpp|inc|rs|pdb)$|^c:[\\/]gtk-build[\\/](build|tools)[\\/]'
$allowed = [ordered]@{
    'bin\glib-2.0-0.dll'      = 'GLIB_LOCALE_DIR: only its share\locale tail is used, appended to the DLL''s root (ggettext.c)'
    'bin\gtk-4-1.dll'         = 'GTK_LIBDIR and GTK_LOCALEDIR: only their tails are used, appended to the DLL''s root (gtkwin32.c)'
    'bin\adwaita-1-0.dll'     = 'LOCALEDIR: Windows builds use adw_get_localedir(), relative to the DLL (adw-main.c)'
    'bin\pango-1.0-0.dll'     = 'sysconfdir and libdir: Pango no longer reads pangorc or modules, and relocates on Windows'
    'bin\fontconfig-1.dll'    = 'conf.avail in the built-in config: GTK on Windows uses the win32 font map, so fontconfig is never initialised'
    'bin\libcrypto-3-x64.dll' = 'OPENSSLDIR, ENGINESDIR, MODULESDIR: main.cpp overrides them with OPENSSL_CONF, OPENSSL_ENGINES, OPENSSL_MODULES; ct_log_list.cnf is read only for Certificate Transparency, which glib-networking does not enable'
    'bin\xml2-16.dll'         = 'the default XML catalog: main.cpp overrides it with XML_CATALOG_FILES'
}
$latin1 = [Text.Encoding]::GetEncoding(28591)
foreach ($f in Get-ChildItem -Path $Stage -Recurse -File) {
    $rel = $f.FullName.Substring($Stage.Length + 1)
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    $hits = New-Object System.Collections.Generic.List[string]
    foreach ($text in @($latin1.GetString($bytes).ToLowerInvariant(),
                        [Text.Encoding]::Unicode.GetString($bytes).ToLowerInvariant())) {
        foreach ($n in $needles) {
            $i = $text.IndexOf($n)
            while ($i -ge 0) {
                # The path runs from the hit to the end of the printable
                # string, or to a quote.
                $e = $i
                while ($e -lt $text.Length -and $text[$e] -gt ' ' -and $text[$e] -le '~' -and
                       $text[$e] -ne '"' -and $text[$e] -ne "'") { $e++ }
                $path = $text.Substring($i, $e - $i)
                if ($path -notmatch $debugPath) { $hits.Add($path) }
                $i = $text.IndexOf($n, $e)
            }
        }
    }
    if ($hits.Count -eq 0) { continue }
    $uniq = @($hits | Sort-Object -Unique)
    if ($allowed.Contains($rel)) {
        Write-Good "allowed: $rel ($($uniq.Count) path(s)): $($allowed[$rel])"
    } else {
        Write-Bad "$rel contains a build or prefix path:"
        $uniq | ForEach-Object { Write-Host "      $_" }
        $failed++
    }
}

# 2. Every import of every staged binary is staged in bin\ or is a system
#    DLL.
foreach ($pe in Get-StagedPe) {
    foreach ($dll in Get-PeImports $pe.FullName) {
        if (-not (Test-Path (Join-Path $stageBin $dll)) -and -not (Test-SystemDll $dll)) {
            Write-Bad "$($pe.FullName.Substring($Stage.Length + 1)) imports $dll, which is neither staged nor a system DLL"
            $failed++
        }
    }
}
# 3. The CRT must be app-local even though System32 has it on this box.
foreach ($n in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')) {
    if (-not (Test-Path (Join-Path $stageBin $n))) { Write-Bad "no bin\$n"; $failed++ }
}

$size = (Get-ChildItem -Path $Stage -Recurse -File | Measure-Object -Property Length -Sum).Sum
$count = @(Get-ChildItem -Path $Stage -Recurse -File).Count
if ($failed -gt 0) {
    Write-Bad "$failed self-check failure(s)"
    exit 1
}
Write-Step ("Done: {0} files, {1:N1} MB in {2}" -f $count, ($size / 1MB), $Stage)
