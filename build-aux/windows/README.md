# Windows packaging

This directory builds a per-user `.msi` for Monday Night Net. It is laid out
so that an MSIX package can follow from the same files.

The real product here is the **staged folder**, `build-msvc\stage\`. It is a
self-contained, relocatable `bin\ lib\ share\` tree. The `.msi` is a thin
WiX wrapper around it, and an MSIX should be one too.

| File | What it does |
|---|---|
| `stage.ps1` | Builds `build-msvc\stage\` from scratch and self-checks it |
| `make-msi.ps1` | Runs `stage.ps1`, then `wix build` and ICE validation |
| `mnn.wxs` | The WiX package: harvests the stage, adds the Start menu shortcut |
| `mnn-test.wsb` | A Windows Sandbox config with `build-msvc` mapped read-only |

## Prerequisites (on the build box)

Run both scripts from an **unelevated** PowerShell on the desktop, not over
ssh. They are in `~/AsusMiku` and are idempotent.

1. `setup-gtk-deps.ps1`: the MSVC GTK 4 stack in `C:\gtk-build`
   (gvsbuild 2026.8.0 plus libshumate), `C:\dev\gtkenv.cmd`, and the
   clang-cl native file. It also fixes gvsbuild's gettext recipe for
   VS 2026. Without that fix, glib links proxy-libintl and nothing in the
   stack can translate.
2. `setup-installer-tools.ps1`: the .NET SDK and WiX 7.0.0 as a .NET global
   tool. **Running it accepts the WiX OSMF EULA** (`wix eula accept wix7`).
   It also tells you whether Windows Sandbox is enabled. If it isn't, run
   this from an elevated PowerShell and reboot:

   ```
   Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All
   ```

The build dir must use meson's default prefix (`C:\`), which DESTDIR turns
into the stage root:

    C:\dev\gtkenv.cmd meson setup --native-file C:\dev\clang-cl-c.ini build-msvc

## Building

    C:\dev\gtkenv.cmd powershell -ExecutionPolicy Bypass -File build-aux\windows\make-msi.ps1

This writes `build-msvc\monday-night-net-<version>-x64.msi`. The version comes
from `meson.build` and must be numeric `x.y.z`. To build only the stage, run
`stage.ps1` the same way.

## Testing

**Clean environment, on the build box.** This copies the stage somewhere
unrelated and runs it with nothing from the build environment:

    robocopy C:\dev\mnn\build-msvc\stage C:\tmp\mnn-stage /MIR
    cmd /c "set PATH=C:\Windows\System32;C:\Windows&& set XDG_DATA_DIRS=&& set GSETTINGS_SCHEMA_DIR=&& set GIO_MODULE_DIR=&& set GDK_PIXBUF_MODULE_FILE=&& set GOM_MODULES_DIR=&& set GTK_PATH=&& C:\tmp\mnn-stage\bin\monday-night-net.exe"

Clear `XDG_DATA_DIRS` in particular. When it is set, GLib drops the
exe-relative `share\`, and the schemas and icons seem to vanish.

**Windows Sandbox, a genuinely clean machine.** Run
`start build-aux\windows\mnn-test.wsb`. Inside the sandbox:

1. Run `C:\mnn\stage\bin\monday-night-net.exe` straight from the read-only
   mapping. This checks that the app never writes next to itself.
2. Install `C:\mnn\monday-night-net-<version>-x64.msi`. There should be no
   UAC prompt. Launch it from the Start menu, then check:
   - map tiles load (HTTPS);
   - icons render;
   - a net saves (Ctrl+S) and loads again after a restart.
3. Uninstall it from Settings → Apps. `%LOCALAPPDATA%\Programs\Monday Night Net`
   and the Start menu shortcut should be gone. User data remains, as below.

## What is bundled, and why

- **`bin\`:**
  - `monday-night-net.exe`;
  - every DLL in the import closure of every staged binary, found by
    walking `dumpbin /dependents`;
  - the MSVC runtime, app-local (`vcruntime140.dll`, `vcruntime140_1.dll`,
    `msvcp140.dll` from `VC\Redist`), never through the VC++ redist
    installer;
  - GLib's `gspawn-win64-helper*.exe`.

  Everything shares one directory because GLib, GTK and gdk-pixbuf each
  find their data relative to *their own* DLL. Plugins under `lib\` also
  resolve their imports from the exe's directory.
- **Plugins**, which no import table names:
  - `lib\gio\modules\gioopenssl.dll` is the TLS backend. It reads trust
    roots from the Windows certificate store, so no CA bundle is shipped.
  - `lib\gdk-pixbuf-2.0\2.10.0\loaders\pixbufloader_svg.dll` is librsvg. PNG
    and JPEG are built into gdk-pixbuf.
  - `lib\libgom-2\modules\gom-sqlite-module.dll` is gom's database driver.
- **`share\glib-2.0\schemas`:** GLib's, GTK's and gsettings-desktop-schemas'
  schemas plus mnn's, compiled. The two wallpaper schemas are left out:
  their defaults are `file://` URIs into the build prefix.
- **`share\icons`:** hicolor, for mnn's own icon, and only the
  *symbolic* part of Adwaita, which is what GTK 4 and libadwaita draw with.
  The full theme is mostly cursors, which Windows draws itself.
- **`share\locale`:** mnn's catalogs, plus GTK's, libadwaita's, GLib's,
  gdk-pixbuf's, glib-networking's, libsoup's and libshumate's, but **only
  for the languages in `po/LINGUAS`** and their regional variants. GLib's
  `g_dgettext()` translates no library string in a language the app has no
  catalog for, except `en_*` and `C`, so the other catalogs would never be
  read. A new language in `po/LINGUAS` needs no change here.
- **Not shipped:**
  - the desktop and metainfo files;
  - headers, import libraries, pkg-config files, GIRs and typelibs;
  - `.pdb` files;
  - `giognomeproxy`, which reads GNOME's proxy settings;
  - GLib's `gdbus.exe`.

Without `gdbus.exe`, GApplication logs "win32 session dbus binary not found"
and runs without single-instance handling. Shipping it would start a
session bus process that outlives the app.

## How the app finds its files

GLib, GTK, GIO and gdk-pixbuf relocate themselves on Windows. Every cache is
generated with no absolute paths:
- `gschemas.compiled` and `giomodule.cache` hold none.
- `loaders.cache` is written by a copy of `gdk-pixbuf-query-loaders` run from
  `stage\bin`, so its paths are relative. Its `# LoaderDir` comment is
  stripped.
- The icon caches come from `gtk4-update-icon-cache`.

`src/main.cpp` covers the rest, under `G_OS_WIN32`, relative to
`g_win32_get_package_installation_directory_of_module(NULL)`:
- **`GOM_MODULES_DIR`:** gom's default is a compiled-in absolute path.
- **`bindtextdomain`:** mnn's `share\locale`, in the system codepage.
- **`OPENSSL_CONF`, `OPENSSL_MODULES`, `OPENSSL_ENGINES` and
  `XML_CATALOG_FILES`:** these point into the install root, where none of
  the files exist. Otherwise OpenSSL and libxml2 would read files under
  `C:\gtk-build\…`. Any user may create that path on another machine, and an
  `openssl.cnf` there can load an engine DLL.

Each one is set with `g_setenv(…, FALSE)`, so `meson devenv` and the tests
still win.

The self-check fails the stage when any file contains `C:\gtk-build` or
`C:\dev`, unless one of these applies:
- **Debug info that is never opened:** source paths and `.pdb` paths, from
  `__FILE__`, CodeView records, or Rust panic locations.
- **A per-file allowlist** in `stage.ps1` of the GTK stack's compiled-in
  prefix directories, each with the reason it is not used on Windows.
  mnn's own binaries have no allowlist entries.

It also fails when a staged binary imports a DLL that is neither staged nor
in System32, or when `MNN.mo` is missing for a language in `po/LINGUAS`.

## Rules for the MSIX that follows

These hold for the stage today. Keep them:

1. **Nothing runs at install time.** No custom actions and no post-install
   scripts. Every cache is built by `stage.ps1`.
2. **The install directory is read-only.** The database lives in
   `g_get_user_data_dir()` (`%LOCALAPPDATA%\monday-night-net\mnn.db`),
   GSettings uses the registry, and nets are saved wherever the user picks.
3. **Fully relocatable.** No absolute build or prefix paths are used at run
   time (see above).
4. **Self-contained.** Everything but Windows system DLLs is bundled, the
   MSVC runtime included.
5. **Per-user state stays per-user.** No HKLM writes. The only registry
   value is the shortcut's HKCU key path,
   `HKCU\Software\KI6KVZ\Monday Night Net`.

### Identity, decided and to be reused

| | |
|---|---|
| Product name | Monday Night Net |
| Publisher display name | KI6KVZ |
| AppUserModelID | `radio.ki6kvz.MondayNightNet` (`APP_ID`) |
| MSI UpgradeCode | `6F1742D9-2199-4EC3-A0BB-B6A775582728` (never change it) |
| MSIX `Publisher` (assumed) | `CN=KI6KVZ`; it must match the signing certificate's subject |
| Install folder (MSI) | `%LOCALAPPDATA%\Programs\Monday Night Net` |

The MSI's Start menu shortcut carries the AUMID, and an unpackaged process
sets the same one on itself, so they group in the taskbar. **Under MSIX the
OS assigns the AUMID** (`<PackageFamilyName>!<AppId>`). `main.cpp` sets its
own only when `GetCurrentPackageFullName` says the process is unpackaged.

### Where MSI and MSIX differ

- **Uninstall.** The MSI removes the install folder and shortcut but leaves
  user data (`%LOCALAPPDATA%\monday-night-net`, the registry settings under
  `HKCU\Software\GSettings`). An MSIX's virtualised AppData goes with the
  package.
- **ICE38, ICE64 and ICE91** are suppressed in `make-msi.ps1`. They assume a
  per-machine package: harvested files are their own key paths, and a
  per-user package installs for only one user.
