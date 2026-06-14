# SENTINEL — Release & Installation



SENTINEL ships as **native desktop packages** for Windows and Linux. Version **1.0.0** includes the full analytics pipeline, sample datasets, and evaluation documentation.



---



## Download artifacts



After building (see below), artifacts appear under `dist/`:



| Platform | Artifact | Size (typical) | Description |

|----------|----------|----------------|-------------|

| Windows | `SENTINEL-1.0.0-win64-portable.zip` | ~80–120 MB | Self-contained folder: `bin/sentinel.exe`, Qt DLLs, sample `share/sentinel/data/`, docs |

| Windows | `SENTINEL-1.0.0-win64-setup.exe` | ~80–120 MB | NSIS installer — same payload as portable ZIP, adds Start Menu + desktop shortcuts |

| Linux | `SENTINEL-1.0.0-linux-x86_64.tar.gz` | varies | FHS layout under `usr/bin`, `usr/share/sentinel/data`, `.desktop` entry |

| Linux | `sentinel_1.0.0_amd64.deb` | varies | Debian package (system Qt 6.4+ dependencies) |



**Do not use** `SENTINEL-*-win64.exe` from CPack — it is a small stub without Qt DLLs. Always distribute `*-win64-setup.exe` (built by `deploy_portable.ps1` / `sentinel.nsi`) or the portable ZIP.



---



## Windows — end user guide



### Which package should I use?



| | Portable ZIP | NSIS setup.exe |

|---|--------------|----------------|

| **Install steps** | Extract to any folder | Run wizard, choose install dir |

| **Admin rights** | Not required | Optional (per-user install supported) |

| **Shortcuts** | None (run `bin\sentinel.exe` directly) | Start Menu + optional desktop shortcut |

| **Updates** | Replace folder manually | Re-run installer or switch to portable |

| **Best for** | USB / restricted networks / quick eval | Daily workstation use |



Both packages contain identical binaries and bundled sample data. User settings and the SQLite database are **not** inside the install folder — they live in `%APPDATA%\SENTINEL\`.



### Option A: Portable ZIP (recommended for first try)



1. Download `SENTINEL-1.0.0-win64-portable.zip`.

2. Extract to a writable folder, e.g. `C:\SENTINEL`.

3. Confirm layout:

   ```

   C:\SENTINEL\

     bin\sentinel.exe

     bin\Qt6Core.dll          ← must be present

     bin\platforms\qwindows.dll

     share\sentinel\data\     ← sample CSVs

   ```

4. Launch:

   ```powershell

   C:\SENTINEL\bin\sentinel.exe

   ```

5. On first run: **File → Import Sample Data** (`Ctrl+Shift+I`) to load bundled London fixture data.



No administrator rights required.



### Option B: NSIS installer



1. Download `SENTINEL-1.0.0-win64-setup.exe`.

2. Run the wizard (default install path is typically `C:\Program Files\SENTINEL` or similar).

3. Launch from **Start Menu → SENTINEL Crime Analytics** or the desktop shortcut.

4. Sample data is under `share\sentinel\data\` relative to the install root.



The installer registers an uninstall entry in Windows Settings → Apps.



### Smoke-test path (developers / CI)



The packaging script stages a runnable tree at **`packaging/pkg/`** before zipping:



```powershell

& packaging/windows/deploy_portable.ps1 -BuildDir build

packaging/pkg/bin/sentinel.exe

```



Verify `packaging/pkg/bin/` contains `Qt6Core.dll` and `platforms/qwindows.dll`. If the app fails to start with “missing DLL”, re-run `deploy_portable.ps1` — do not publish the CPack stub `.exe`.



---



## Linux — end user guide



### Option A: Tarball



```bash

tar -xzf SENTINEL-1.0.0-linux-x86_64.tar.gz -C /opt/sentinel --strip-components=0

# If layout is usr/ inside tarball:

sudo tar -xzf SENTINEL-1.0.0-linux-x86_64.tar.gz -C /

sentinel

```



Ensure Qt 6.4+ runtime libraries are installed (`libqt6widgets6`, `libqt6charts6`, `libqt6sql6-sqlite` on Debian/Ubuntu).



### Option B: Debian package



```bash

sudo dpkg -i sentinel_1.0.0_amd64.deb

sudo apt-get install -f   # resolve Qt dependencies if needed

sentinel

```



Desktop entry: **SENTINEL** appears under Office / Science categories.



Sample data: `/usr/share/sentinel/data/crimes/`



---



## Building releases from source



### Prerequisites



| Requirement | Windows | Linux |

|-------------|---------|-------|

| Qt 6.4+ | MinGW or MSVC 2022 kit with Charts, Sql, Network | `qt6-base-dev`, `qt6-charts-dev`, `libqt6sql6-sqlite` |

| CMake | 3.27+ (Qt Tools or standalone) | 3.27+ |

| Compiler | MinGW 13+ **or** MSVC 2022 + **Windows 11 SDK** | GCC 13+ or Clang 16+ |

| Extra (optional) | NSIS at `C:/Program Files (x86)/NSIS` for `*-setup.exe` | `dpkg-deb` for `.deb` |



### Windows — primary packaging path



**Use `deploy_portable.ps1` as the primary build-and-package command.** It works with MinGW or MSVC builds, stages to `packaging/pkg/`, runs `windeployqt`, and produces both `dist/` artifacts.



```powershell

cd sentinel

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `

  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"

cmake --build build --target sentinel

& packaging/windows/deploy_portable.ps1 -BuildDir build

```



**Output in `dist/`:**

- `SENTINEL-1.0.0-win64-portable.zip`

- `SENTINEL-1.0.0-win64-setup.exe` (when NSIS / `makensis` is installed)



**Staged smoke-test tree:** `packaging/pkg/bin/sentinel.exe` with `Qt6Core.dll` and `platforms/qwindows.dll`.



### Windows — MSVC release (optional, requires Windows SDK)



MSVC builds need the **Windows 11 SDK** (provides `stddef.h`, UCRT headers). Without it, use the MinGW + `deploy_portable.ps1` path above.



```powershell

cd sentinel

& packaging/windows/install_windows_sdk.ps1   # if SDK missing

& packaging/windows/build_release.ps1

# Or: & packaging/windows/build_msvc.bat

```



If MSVC configure fails, use MinGW instead: `& packaging/windows/build_mingw.ps1` or `& packaging/windows/deploy_portable.ps1 -BuildDir build`. The MSVC script auto-clears a stale MinGW CMake cache when reconfiguring with `cl`.



Auto-detects Qt at `C:/Qt/6.11.0/msvc2022_64`. Override with `-QtPath` if needed.



MSVC output in `dist/`:

- `SENTINEL-1.0.0-win64-portable/` (unpacked tree)

- `SENTINEL-1.0.0-win64-portable.zip`

- Re-run `deploy_portable.ps1` for `SENTINEL-1.0.0-win64-setup.exe` if NSIS setup was not produced by CPack



### Linux



```bash

cd sentinel

chmod +x packaging/linux/build_release.sh

QT_PREFIX=/usr ./packaging/linux/build_release.sh

# Or with a custom Qt install:

QT_PREFIX=/opt/Qt/6.11.0/gcc_64 ./packaging/linux/build_release.sh

```



Output: `dist/SENTINEL-1.0.0-linux-x86_64.tar.gz` (+ `.deb` when `dpkg-deb` is available).



### Manual install (developers)



```bash

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>

cmake --build build --target sentinel

cmake --install build --prefix /path/to/install

```



On Windows, `deploy_portable.ps1` runs `windeployqt --release --no-translations --compiler-runtime` automatically (MSVC redistributables included when built with MSVC).



---



## What is bundled



| Content | Location in package |

|---------|---------------------|

| Application binary | `bin/sentinel` (Linux) or `bin/sentinel.exe` (Windows) |

| Sample datasets | `share/sentinel/data/` (excludes large `bulk/` archives) |

| Co-offending sample | `share/sentinel/data/co_offending/chicago_co_offending.csv` |

| Documentation | `share/doc/Sentinel/README.md`, `ANALYST_GUIDE.md`, `REAL_DATA_EVALUATION.md` |

| Qt runtime (Windows) | Alongside executable via `windeployqt` |

| Desktop entry (Linux) | `share/applications/sentinel.desktop` |



User database, settings, and provenance logs are **not** bundled — they are created on first run in the platform app-data directory.



---



## Verification checklist



Before publishing a release:



1. **Build tests:** `ctest --output-on-failure -j4` (495 tests pass).

2. **Real-data smoke:** `& scripts/run_real_data_tests.ps1` (Windows) or `ctest -R test_real_data_evaluation`.

3. **Staged launch:** Run `packaging/pkg/bin/sentinel.exe` after `deploy_portable.ps1` (confirms Qt DLLs deployed).

4. **Fresh-machine launch:** Start from portable ZIP on a VM without Qt SDK installed.

5. **Import sample:** Load `share/sentinel/data/crimes/london_crimes_2024.csv` and confirm map + events table populate.

6. **Cases / Network pages:** Open sidebar **Cases** — filter a case, confirm Rossmo CGT heatmap, series merge/split overrides, lead history, and **Export Case Report** writes Markdown. Open **Network** — confirm zoom/pan, node click tooltip, **Load CSV**, and **Load from DB** (when events have `meta.person_id`).

7. **Analytics calibration:** Open **Analytics → Calibration** tab; **Run Analysis** produces reliability diagram when ≥2 months of dated events exist.

8. **Version string:** About / window title shows **1.0.0**.



---



## Versioning



Version is defined in root `CMakeLists.txt` (`project(Sentinel VERSION …)`). Packaging scripts read `1.0.0` by default; pass `-Version` (Windows) or `VERSION=` (Linux) to override.



Bump process:



1. Update `project(Sentinel VERSION x.y.z)` in `CMakeLists.txt`.

2. Rebuild packages with matching `-Version` / `VERSION=`.

3. Update this document and `README.md` release table.



---



## Long-term release goals



See [ROADMAP.md](ROADMAP.md) Phase 1 follow-ups (code signing, auto-update channel, CI-built artifacts for every tag). Phase 2 investigation-depth features (case workspace with CGT heatmap and series overrides, SQLite co-offending graph, calibration dashboard, `LocalApiServer`) are **shipped** in v1.0.0 — remaining work is Phase 3+ in [REMAINING.md](../REMAINING.md). Current packages are **unsigned** and suitable for research, demo, and internal analyst workstations.

