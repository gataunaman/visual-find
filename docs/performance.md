# Performance and validation

Measured on **3 October 2026**, on an Intel Core i5-10210U Windows 10 Home Single Language 22H2 PC (build 19045), capturing one **1920 x 1080** monitor. These are observations from this machine, with other desktop applications running, rather than latency guarantees.

## Release measurements

| Operation | Samples | Median | 95th percentile |
| --- | ---: | ---: | ---: |
| Screen capture | 12 | 46.08 ms | 66.28 ms |
| Windows OCR, en-US | 12 | 72.17 ms | 145.78 ms |
| Construct cached search index | 12 | 0.038 ms | 0.075 ms |
| Search cached OCR text | 2,000 | 0.0011 ms | 0.0012 ms |
| Draw and submit highlights | 120 | 0.045 ms | 0.097 ms |
| Process creation to input-ready message loop | 5 | 34.97 ms | 67.28 ms |

The first capture and OCR call in the benchmark process took 135.72 ms and 95.19 ms respectively. This does not represent a cold operating-system cache: OCR had been exercised earlier during development. The fixture contained 39 indexed OCR words; cached searches alternated between a single word and a phrase. Dense screens, more matches, different languages, and larger displays will cost more.

An application smoke run recorded the search UI becoming available in **5.57 ms**, a refreshed capture/OCR/index pipeline completing in **143.62 ms**, and 15 query updates across two OCR sessions. UI timing measures the native show/focus calls; rendering timing measures clearing, painting, and `UpdateLayeredWindow` submission. Neither measurement includes the monitor's final presentation time.

| Resource measurement | Observed value |
| --- | ---: |
| Idle CPU over a 6.03-second sample | 0.00% of one CPU core |
| Working set before any search | 7.76 MiB |
| Private committed memory before any search | 1.78 MiB |
| Working set after completed/closed sessions | 23.44 MiB |
| Private committed memory after those sessions | 3.94 MiB |
| Release executable | 1,585,152 bytes |

The idle CPU result means no additional process CPU time was recorded during that sample; it is not a claim of permanently zero CPU usage. Memory values are process snapshots, not peak memory or total system/GPU memory. The OCR engine remains available after a session, while screenshot, index, and overlay resources are released.

## Changes justified by measurements

The initial overlay allocated and submitted a full-monitor bitmap for every query and navigation update. Its measured median submission time was **15.65 ms**, compared with **0.056 ms** after restricting the bitmap to the padded union of the matches. Post-session working set fell from 31.15 MiB to 23.28 MiB. Broadly scattered matches can still require a large surface. These are separate runs under changing desktop load, not a controlled hardware-wide speedup claim.

The UI now takes foreground focus during the global-hotkey handler, while capture and OCR remain on the worker. Refresh keeps the search box in place on the same monitor. Both changes were checked against real hotkey delivery, focus restoration, and text placed behind the capture-excluded search box. Typing searches the cached index and never queues recognition.

Capture and OCR now dominate session latency. A GPU capture pipeline or another OCR engine would add dependencies and complexity, so this version retains GDI capture and the installed Windows OCR engine. No cross-engine accuracy or speed comparison was performed. The English fixtures establish practical functionality, not general OCR accuracy.

## What passed

The search tests also passed exact desktop-label merging, suppression of incorrect/duplicate OCR readings inside shortcut regions, and preservation of other screen text. The search tests passed case-insensitive matching, ordered phrases, multiple/zero results, Unicode and whitespace normalization, substring behavior, rectangle unions, wrapped-line regions, resized-image mapping, and negative monitor origins. The DPI arithmetic checks verify physical coordinate invariance; physical mixed-DPI monitor configurations were not exercised.

The desktop benchmark passed local capture/OCR, word and phrase boxes, wrapped phrases, real Ctrl+Shift+F delivery while its fixture was focused, input-through overlay behavior, Enter/previous navigation, cached typing, refresh, capture exclusion, Escape/focus restoration, focus-loss dismissal, display-change dismissal, stale-result rejection, and normal process exit. The display-change check injects the notification; it does not physically disconnect a monitor.

The application checks passed on visible fixtures in Chrome, Edge, Edge's PDF viewer, Notepad, VS Code, a raster image, and a paused video frame. Each recognized three case-varied occurrences of `algorithm`, one `binary search` phrase, and one `Hello World` phrase. Windows Settings passed a separate check for visible Settings-related terms; it does not contain the fixed fixture phrases. Application-specific outcomes and their capture/OCR timings are recorded in [application-test-results.json](application-test-results.json). These checks capture visible test windows and exercise OCR; the separate desktop benchmark exercises the app's hotkey, interface, and lifecycle. A partial recognition or unavailable fixture is recorded rather than treated as a pass. Small editor text can be misrecognized even when other words and phrases match.

## Reproduce

Exit Visual Find before running the benchmark from the source project root on an unlocked interactive desktop:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test
.\build\VisualFind.Bench.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\app_matrix.ps1
```

The compatibility script opens local fixtures and isolated browser/editor profiles under `build`, temporarily brings its fixture windows forward, and closes windows it creates. Previously open fixture windows are preserved. It saves only fixed-query counts, outcomes, and timings. Normal application use saves no screenshots or OCR text.

Raw measurements: [release benchmark](benchmark-results.json), [application smoke run](app-smoke.json), [overlay baseline](benchmark-baseline.json), and [baseline smoke run](app-smoke-baseline.json).

## Screenshot and desktop-shortcut fix

The real desktop's small, shadowed **MinGW Installer** label was not recognized by the Windows OCR engine in the diagnostic at native size or at 1.5x, 2x, or 3x enlargement. Those diagnostic attempts saved no screenshots. The release instead reads the names and physical bounds of visible Explorer desktop shortcuts once when a session starts from the desktop, replacing OCR readings in those regions. Other application sessions continue to use OCR alone. This adds one local accessibility read on the worker and no work per keystroke or while idle.

The end-to-end desktop regression passed an actual mingw query with one match, physical shortcut alignment, the real global hotkey, F5 refresh, screenshot inclusion of the bar and highlights, and the embedded window icon. Reading the visible desktop labels took **48.61 ms** in that test, before launching the app; this is a single diagnostic sample, not a desktop-session median. Desktop matches highlight the full icon-and-label region and use the full accessible shortcut name, including names that Explorer may visually abbreviate. See [desktop-validation.json](desktop-validation.json).

The search bar is now excluded only during Visual Find's pixel capture and is restored to normal screenshot visibility before recognition completes. The workflow benchmark also passed a white search-bar pixel check at its actual DPI, normal display affinity for both windows, and recognition of a marker behind the search bar across capture and refresh. A screenshot taken during the brief capture step can still omit the bar.

The current benchmark includes a 12-pixel, white, shadowed MinGW label on a simple dark patch. It was recognized; the real wallpaper diagnostic demonstrates why this fixture alone cannot establish desktop-label accuracy. The updated benchmark ran under lighter desktop load than the earlier overlay comparison, so its lower capture/OCR timings should not be interpreted as a new OCR-engine optimization. The overlay optimization comparison above remains a comparison of the original full-monitor and bounded-surface implementations.