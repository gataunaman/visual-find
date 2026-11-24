# Visual Find

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg) ![Platform: Windows 10/11 x64](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey.svg)

Visual Find is a native Windows utility for finding visible text anywhere on the monitor under your mouse cursor. Press **Ctrl+Shift+F**, type a word or phrase, and see matching text highlighted over the desktop. It reads a single screen capture with the installed Windows OCR engine, then searches the recognized text as you type.

The app runs in the notification area and uses Win32 controls, a worker thread, and a transparent highlight overlay. There is no browser shell or .NET application runtime.

When search starts from the Windows desktop, it also reads the names and physical bounds of visible desktop shortcuts through Windows accessibility. This makes names such as **MinGW Installer** searchable even when OCR misses their shadowed lettering on wallpaper. Desktop shortcut matches highlight the full icon-and-label region; their full names can be searched even when Windows abbreviates the displayed label.

## Run

Extract `VisualFind-1.0.0-win-x64.zip` and run `VisualFind\VisualFind.exe`. Keep the accompanying documentation and license notices with the executable when redistributing it.

Practical supported configuration: **64-bit Windows 10 version 2004 or later, including Windows 11**, with at least one installed Windows OCR language. The portable executable statically links its GCC/C++ runtime components; no separately installed Visual C++ redistributable or .NET runtime is required. Windows system libraries and the Windows OCR component remain required. Other architectures and older Windows releases have not been established as supported.

1. Move the mouse cursor to the monitor you want to search.
2. Press **Ctrl+Shift+F**. The search box opens on that monitor while capture and OCR run on a worker thread.
3. Type while OCR finishes. Once recognition is ready, your current query is applied automatically; further edits search the cached text without another capture or OCR pass.
4. Press **Enter** for the next match or **Shift+Enter** for the previous one. Navigation wraps around. The selected match is blue; other matches are yellow.
5. **Scroll** the content under the search box with the mouse wheel. Highlights hide while you scroll, then the monitor is re-read about 0.2 seconds after you stop (or once per second during a long scroll) and the current query is re-applied. Nothing runs while you are not scrolling.
6. Press **F5** to capture the monitor under the cursor again. Refresh preserves the query.
7. Press **Esc** or the close button to dismiss the search. Switching focus to another application also dismisses it.

Drag the three-dot grip or an empty part of the search window to move it. The text field and buttons remain interactive. Each new search opens at the default upper-right position on the monitor under the cursor.

The app keeps running after a search closes. Right-click its notification-area icon and choose **Exit Visual Find** to stop it. The tray menu also offers **Search screen**; double-clicking the tray icon starts a search. Launching another copy activates the existing instance.

## Options

Use command-line arguments, or put them in a Windows shortcut's target:

```powershell
.\VisualFind.exe --hotkey Ctrl+Alt+G --language en-US --quiet
```

| Option | Behavior |
| --- | --- |
| `--hotkey Ctrl+Alt+G` | Changes the global shortcut. Use one or more of `Ctrl`/`Control`, `Shift`, `Alt`, and `Win`, joined with `+`, followed by exactly one letter or digit. At least one modifier is required. |
| `--language en-US` | Selects an installed Windows OCR language by its language tag. An unavailable language produces an error. |
| `--quiet` | Suppresses the startup tray notification. The tray icon and shortcut remain active. |

Without `--language`, Visual Find first tries the user's profile languages, then the first available installed OCR recognizer. These options are read when the first instance starts. Exit the existing instance before relaunching with different options.

If the shortcut is already registered by another app, Visual Find reports the conflict at startup. Choose another combination with `--hotkey`. To resolve a missing OCR language, add a language with its OCR feature in Windows Settings, then restart Visual Find.

## Search behavior and limits

- Searches are case-insensitive substrings. Unicode compatibility normalization and whitespace normalization apply to both OCR words and queries. There is no regex, fuzzy matching, or automatic correction of OCR errors.
- Phrases can span adjacent OCR words and line breaks. Recognition order determines phrase order; columns and complex layouts can produce unexpected joins.
- Substring matches highlight the whole recognized word rectangle. Phrases use one rectangle per recognized line. Overlapping substring occurrences are not counted separately.
- Each session searches a snapshot of one monitor. Mouse-wheel scrolling refreshes automatically; moving windows, scrollbar dragging, keyboard scrolling, playing video, or other page changes can still leave highlights out of date, so use **F5**. The utility does not scroll applications or find off-screen text. Scroll detection uses a low-level mouse hook that is installed only while a search is open and sees wheel events only as a trigger; it does not record or store input.
- Small, low-contrast, stylized, rotated, or unsupported-language text may be missed or read incorrectly. Captures larger than the OCR engine's image-size limit are reduced before recognition, which can further affect small text.
- Protected video, capture-excluded windows, the secure desktop, and some exclusive full-screen content may not be captured. Compatibility depends on what Windows screen capture can read.
- The search box occupies the upper-right work area of the selected monitor and can cover some content visually. The app excludes the bar only while taking its own OCR screenshot, allowing it to read text behind the bar. Exclusion ends as soon as pixels are copied, so ordinary screenshots include the search bar and highlights. A screenshot taken during the brief capture step can still omit the bar. Highlights pass mouse input through; clicking the underlying app closes the session because focus changes.
- Display configuration changes close the active session. Coordinate tests cover scaling and negative monitor origins, but do not establish testing on every physical multi-monitor or DPI configuration.

The app code makes no network requests during normal operation. Captured pixels and recognized text stay in process memory; normal use does not save screenshots or OCR text to disk. Closing a session releases its index and overlay. An OCR operation already running may finish on the worker thread before its temporary memory is released; stale results are discarded. Diagnostic tools write timings, counts, and test outcomes.

## Build from source

Install MSYS2's **MinGW-w64 x64 GCC toolchain** (`mingw-w64-x86_64-gcc` and the `windres` tool provided by MinGW binutils), then make `g++.exe` and `windres.exe` available on the Windows `PATH`, normally from `C:\msys64\mingw64\bin`. Run this from the project root in Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test -Package
```

The script downloads the pinned build dependency **Microsoft.Windows.CppWinRT 2.0.250303.1** from NuGet on first use. Its `cppwinrt.exe` generates the C++ projections from the machine's `%WINDIR%\System32\WinMetadata`. The tool and generated headers are build inputs and are not shipped as a separate runtime. An internet connection is needed for that initial download; subsequent builds use the cached tool and projections.

The build produces `dist\VisualFind\VisualFind.exe`. `-Test` runs the search and coordinate tests; `-Package` copies the README, project license, documentation, and third-party notices into the portable folder and writes `dist\VisualFind-1.0.0-win-x64.zip`.

## Validation and measurements

`tests\search_tests.cpp` checks case-insensitive and substring matching, phrases, Unicode normalization, empty queries, highlight geometry, image scaling, and negative monitor coordinates. It does not test OCR accuracy.

The build also creates a benchmark tool and an application compatibility probe. Run these separately on an interactive desktop, from the project root:

```powershell
.\build\VisualFind.Bench.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\app_matrix.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\desktop_workflow.ps1
```

The benchmark shows its own text fixture and exercises capture, OCR, cached searches, overlay rendering, and app lifecycle behavior. `tests\app_matrix.ps1` opens the fixtures in installed applications: Chrome, Edge, an Edge PDF viewer, Notepad, VS Code, a raster image, a paused video frame, and Windows Settings. It writes per-application outcomes to `docs\application-test-results.json`. Missing applications, unavailable windows, and focus failures are reported rather than counted as passes. The matrix changes foreground windows while it runs and requires an unlocked interactive desktop.

The desktop workflow regression test requires a visible **MinGW Installer** desktop shortcut on the cursor monitor. It temporarily minimizes windows and checks the real global hotkey, exact shortcut match and physical bounds, refresh, screenshot inclusion, and custom icon. Results are in `docs\desktop-validation.json`.

See [measured performance and test scope](docs/performance.md) for recorded results and limitations. The existence of a test or fixture does not imply that every application, language, display, or Windows version has passed it.

The implementation is described in [architecture](docs/architecture.md). Project code is under the [MIT license](LICENSE); bundled third-party notices are indexed in [licenses/README.md](licenses/README.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md), and the [changelog](CHANGELOG.md).
