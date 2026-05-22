# Virtual Smart Card Architecture

Virtual Smart Card Architecture is an umbrella project for various
projects concerned with the emulation of different types of smart card readers
or smart cards themselves.

Currently the following projects are part of Virtual Smart Card Architecture:

- [Virtual Smart Card](http://frankmorgner.github.io/vsmartcard/virtualsmartcard/README.html)
- [Remote Smart Card Reader](http://frankmorgner.github.io/vsmartcard/remote-reader/README.html)
- [Android Smart Card Emulator](http://frankmorgner.github.io/vsmartcard/ACardEmulator/README.html)
- [PC/SC Relay](http://frankmorgner.github.io/vsmartcard/pcsc-relay/README.html)
- [USB CCID Emulator](http://frankmorgner.github.io/vsmartcard/ccid/README.html)

Please refer to [our project's website](http://frankmorgner.github.io/vsmartcard) for more information.

[![GitHub CI status](https://img.shields.io/github/actions/workflow/status/frankmorgner/vsmartcard/ci.yml?branch=master&label=Ubuntu%2FmacOS&logo=github)](https://github.com/frankmorgner/vsmartcard/actions/workflows/ci.yml?branch=master) [![AppVeyor CI status](https://img.shields.io/appveyor/ci/frankmorgner/vsmartcard/master.svg?label=Windows&logo=appveyor)](https://ci.appveyor.com/project/frankmorgner/vsmartcard) [![Coverity Scan status](https://img.shields.io/coverity/scan/3987.svg?label=Coverity%20Scan)](https://scan.coverity.com/projects/3987)

---

## Building `BixVReader` (the Windows UMDF driver) on Windows 11

This fork (`Vingelar/vsmartcard`) contains a set of build-system fixes that
make `virtualsmartcard/win32/BixVReader.sln` build out-of-the-box on a current
Windows 11 machine with Visual Studio 2022 / 2026. The sections below explain
*why* those fixes are necessary, *what* was changed, and *how* a third party
can reproduce the build from a fresh clone.

### Why the upstream solution does not build on a modern Windows 11 box

A clean checkout of `frankmorgner/vsmartcard` fails to build `BixVReader.sln`
on a current developer machine for several independent reasons:

1. **Missing `wudfddi.idl` / `wudfddi.h` (UMDF 1.x headers).**
   `BixVReader` is a UMDF 1.9 user-mode driver. Its `VirtualSCReader.idl`
   imports `wudfddi.idl`, which ships only with the **Windows Driver Kit
   (WDK)**, not with the Windows SDK. Visual Studio 2022/2026 selects the
   `Component.Microsoft.Windows.DriverKit` component in the installer UI, but
   that component on its own only puts a stub WDK package onto disk - the
   actual headers, libs, build targets, and tools (`stampinf.exe`,
   `inf2cat.exe`, ...) live in a separate WDK MSI that has to be installed
   in addition to the SDK.
   Result of the unfixed build:
   `midl : command line error MIDL1001: cannot open input file wudfddi.idl`.

2. **`DevMsi.vcxproj` is hard-coded to a Windows 10 SDK / VS toolset that is
   not available on modern machines.**
   Upstream pins `<WindowsTargetPlatformVersion>10.0.19041.0</WindowsTargetPlatformVersion>`
   (Windows 10 2004, May 2020) and `<PlatformToolset>v142</PlatformToolset>`
   (VS 2019). On a Windows 11 box with VS 2022/2026 and only the matching SDK
   (e.g. `10.0.26100.0`) installed, this throws:
   `error MSB8036: The Windows SDK version 10.0.19041.0 was not found.`

3. **The `DevMsi` project uses an outdated WiX 3.x SDK layout.**
   `DevMsi/src/Props/WIX.props` references `$(WIX)SDK\VS2015\inc`, a path
   that existed inside the old WiX 3.x VS-extension MSI but is no longer
   produced by the modern `wix314-binaries.zip` portable archive (headers
   now live in `$(WIX)SDK\inc`). WiX 4/5/7 is not API-compatible with
   WiX 3.x C custom actions either - so we genuinely need a WiX 3.14
   toolchain.

4. **`BixVReaderInstaller.wixproj` cannot find `Wix.targets`.**
   The `.wixproj` looks for `Wix.targets` under
   `$(MSBuildExtensionsPath32)\Microsoft\WiX\v3.x\`, which only exists when
   the legacy WiX 3.14 *MSI* installer has run. With WiX 4/5/7 (current
   default from the Visual Studio Marketplace and `winget`) the file is
   missing and the build aborts before the WiX projects are even compiled.

5. **No test-signing certificate.**
   The post-build target invokes `signtool.exe sign /fd SHA256 /v
   bixvreader.cat` without any cert reference. On a fresh machine there is no
   matching code-signing certificate in the user / machine store, so
   `signtool` errors out with `SignTool error: No certificates were found
   that met all the given criteria.` and the build fails at the very last
   step even though the DLL and CAT were produced successfully.

### What this fork changes

The patches are intentionally minimal and live entirely under
`virtualsmartcard/win32/`:

| File                                                      | Change                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| --------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Directory.Build.props` (new)                             | Auto-imported by MSBuild. Points `WIX`, `WixToolPath`, `WixExtDir`, `WixTasksPath` and `WixTargetsPath` at the portable WiX 3.14 toolset under `.tools/wix314/`, so the build no longer depends on a system-wide WiX 3.x MSI install.                                                                                                                                                                                                                                                                                                                                                                                            |
| `scripts/Bootstrap-WiX314.ps1` (new)                       | One-shot, idempotent download + extract of `wix314-binaries.zip` from the official WiX 3 GitHub release into `.tools/wix314/`. Removes the need for an interactive MSI install.                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| `EnsureTestCert.ps1` (new)                                 | Pre-build script that creates a self-signed code-signing certificate (`CN=BixVReader Test Code Signing`, RSA-2048, SHA-256, 10-year validity) and exports it as both a PFX (consumed by `signtool` at build time) and a DER-encoded CER (next to `install_vpcd.bat`, where the installer script imports it into `Root` and `TrustedPublisher`). Idempotent.                                                                                                                                                                                                                                                                       |
| `BixVReader/BixVReader.vcxproj` (modified)                 | (1) Added an `EnsureTestCert` pre-build target that runs `EnsureTestCert.ps1`. (2) Extended the existing `AutoTestSignUMDF` post-build target so that both the DLL and the catalog are signed with the auto-generated PFX (`signtool sign /fd SHA256 /f ... /p ...`).                                                                                                                                                                                                                                                                                                                                                            |
| `BixVReaderInstaller/BixVReaderInstaller.wixproj` (modified) | Added an explicit `<Import Project="..\Directory.Build.props" />` near the top because `.wixproj` (ToolsVersion `4.0`) does not auto-import `Directory.Build.props`.                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `DevMsi/` (now a regular directory, was a git submodule)   | The upstream submodule pointed at `frankmorgner/DevMsi` and contained a hard-coded VS 2019 / SDK 10.0.19041.0 configuration. Carrying it as a submodule made the build fixes impossible to commit in this single fork, so it was vendored into the main tree. Inside `DevMsi/src/DevMsi/DevMsi.vcxproj` the `<PlatformToolset>` is now `$(DefaultPlatformToolset)` (auto-selects whatever toolset the installed VS provides), and `<WindowsTargetPlatformVersion>` was relaxed to `10.0` (MSBuild then picks the latest installed 10.x SDK). `DevMsi/src/Props/WIX.props` was fixed to use `$(WIX)SDK\inc` (the modern WiX layout). |
| `.gitignore` (new, under `virtualsmartcard/win32/`)        | Excludes the portable WiX directory (`.tools/`), the test-signing assets (`TestCert/` and `BixVReader.cer`), MIDL by-products in the source tree, and the usual `bin/`, `obj/`, `x64/` MSBuild outputs.                                                                                                                                                                                                                                                                                                                                                                                                                          |

The fork **does not** modify any C/C++ source code. All driver behaviour
is identical to upstream `frankmorgner/vsmartcard`.

### Prerequisites

1. **Windows 11** (any recent build; verified on 22H2 / 24H2).
2. **Git for Windows** with `git lfs` not required.
3. **Visual Studio 2022 (17.x) or 2026 (18.x)**, *Community / Professional /
   Enterprise* - all editions work. The following individual components must
   be ticked in the VS Installer:
   - *Desktop development with C++* (workload)
   - *C++ ATL for the latest build tools* (ATL is required because BixVReader
     sets `<UseOfAtl>Static</UseOfAtl>`)
   - *MSVC v143 / v144 / v145 - VS C++ x64/x86 build tools* (whatever is
     current for your VS version)
   - *Windows 11 SDK (e.g. 10.0.26100.0)*
   - *Windows Driver Kit* (the VS-Installer entry called
     `Component.Microsoft.Windows.DriverKit`). Selecting this does **not**
     install the actual WDK MSI on its own - see step 5 below.
4. **PowerShell 5.1 or later** (ships with Windows 11).
5. **The standalone Windows Driver Kit MSI**, matching the Windows SDK
   version installed in step 3. The easiest way to install it is via
   `winget`:

   ```powershell
   winget install --id Microsoft.WindowsWDK.10.0.26100 `
                  --silent --accept-source-agreements --accept-package-agreements
   ```

   This requires elevation (UAC prompt). After it has finished, you should
   find `wudfddi.idl` at
   `C:\Program Files (x86)\Windows Kits\10\Include\wdf\umdf\1.11\wudfddi.idl`
   and `stampinf.exe` / `Inf2Cat.exe` under
   `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\<arch>\`.

   If your Windows SDK version is different (for example `10.0.22621.x`),
   install the matching `Microsoft.WindowsWDK.10.0.22621` instead.

6. **No need to install WiX manually.** The portable WiX 3.14 toolchain is
   fetched by `scripts/Bootstrap-WiX314.ps1`. WiX 4/5/7 are *not* supported
   by `DevMsi` and `BixVReaderInstaller` and must not be relied on - their
   C custom-action API is fundamentally different from WiX 3.x.

### Build instructions

After cloning *this* fork:

```powershell
git clone https://github.com/Vingelar/vsmartcard.git
cd vsmartcard\virtualsmartcard\win32

# 1. Download and extract the portable WiX 3.14 toolset into .tools\wix314.
#    Idempotent: re-runs are no-ops once the toolset is present.
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\Bootstrap-WiX314.ps1

# 2. Build the whole solution. The pre-build step generates TestCert\BixVReader.pfx
#    and BixVReader.cer on the first run.
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    .\BixVReader.sln `
    /p:Configuration=Release /p:Platform=x64 /m
```

(If you use VS 2022, the MSBuild path is
`C:\Program Files\Microsoft Visual Studio\2022\<Edition>\MSBuild\Current\Bin\MSBuild.exe`.)

You can also open `BixVReader.sln` in Visual Studio and use *Build > Build
Solution* (Ctrl+Shift+B). The first build will trigger
`EnsureTestCert.ps1` automatically; subsequent builds reuse the generated
PFX.

### What gets produced

| Artifact                                                     | Purpose                                                                                                                                                                                              |
| ------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `BixVReader\x64\Release\BixVReader.dll`                       | The UMDF driver itself, embedded-signed with the test certificate.                                                                                                                                   |
| `BixVReader\x64\Release\BixVReader.inf`                       | The driver INF, stamped with `stampinf` (sets `DriverVer = ...`).                                                                                                                                    |
| `BixVReader\x64\Release\bixvreader.cat`                       | Catalog file generated by `Inf2Cat`, also signed with the test certificate.                                                                                                                          |
| `DevMsi\src\bin\x64\Release\DevMsi.dll`                       | WiX 3.x custom-action DLL (creates / removes the virtual device node during install / uninstall).                                                                                                    |
| `BixVReaderInstaller\bin\x64\Release\BixVReaderInstaller.msi` | The end-user installer.                                                                                                                                                                              |
| `BixVReader.cer` (next to `install_vpcd.bat`)                 | DER-encoded public certificate. `install_vpcd.bat` imports it into `Root` and `TrustedPublisher` before running `msiexec`.                                                                          |
| `TestCert\BixVReader.pfx`                                     | Private code-signing key + cert. **Not committed** (gitignored). Re-generated automatically if missing. The PFX password is `BixVReader!` and is defined inline in `BixVReader\BixVReader.vcxproj`. |

### Installing the freshly-built driver

The test-certificate signed driver will only load if Windows has been put
into *test-signing* mode:

```cmd
bcdedit /set testsigning on
```

A reboot is required for this change to take effect.

After rebooting, copy the following three files into one folder on the
target machine (or use them from the build output directly):

- `BixVReader.cer`
- `BixVReaderInstaller.msi`
- `install_vpcd.bat`

Then right-click `install_vpcd.bat` and choose *Run as administrator*. It
will:

1. Import `BixVReader.cer` into `Cert:\LocalMachine\Root` and
   `Cert:\LocalMachine\TrustedPublisher`.
2. Run `msiexec /i BixVReaderInstaller.msi` (silent, no reboot).
3. The MSI calls into `DevMsi.dll` to create the virtual device node
   `root\BixVirtualReader`. Windows then loads `BixVReader.dll` via WUDFRd.

To remove the driver, run `uninstall_vpcd.bat` (also as administrator).

### Security warning

`TestCert\BixVReader.pfx` and the hard-coded password (`BixVReader!`) in
`BixVReader\BixVReader.vcxproj` are **development-time test-signing
material**. They give anybody who possesses the PFX the ability to sign
code that Windows will trust *after* `install_vpcd.bat` has been executed
on that machine.

- Do **not** ship the PFX to end users.
- Do **not** import the certificate into `Root` / `TrustedPublisher` on
  production machines.
- For a production driver, replace the test-signing flow with a real EV
  code-signing certificate and submit the driver package to the Microsoft
  Partner Center for WHQL/HLK attestation signing.

### Known runtime issues (unrelated to the build)

- On Windows 11 22H2 and newer the smart-card resource manager
  (`SCardSvr`) calls `IoSmartCardGetAttribute(SCARD_ATTR_CURRENT_PROTOCOL_TYPE = 0x00020110)`
  immediately after device discovery. The current `Reader::IoSmartCardGetAttribute`
  implementation falls through to `default:` and returns `ERROR_NOT_SUPPORTED`,
  which causes Windows 11 to abort the reader registration (see upstream
  issue [#324](https://github.com/frankmorgner/vsmartcard/issues/324)).
  This is a *source-level* bug, not a build-system problem, and is left
  unchanged in this fork.
