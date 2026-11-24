param([switch]$RemainingOnly, [switch]$SettingsOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Push-Location (Split-Path $PSScriptRoot)
try {
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class FixtureDesktop {
    private delegate bool EnumCallback(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] private static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
    public static bool IsTopmost(IntPtr hwnd) { return (GetWindowLongPtr(hwnd, -20).ToInt64() & 8) != 0; }
    public static void RestoreTopmost(IntPtr hwnd, bool wasTopmost) {
        SetWindowPos(hwnd, new IntPtr(wasTopmost ? -1 : -2), 0, 0, 0, 0, 0x13);
    }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int x, y; }
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    public static bool FocusFixture(IntPtr hwnd) {
        ShowWindow(hwnd, 3);
        SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x53);
        return SetForegroundWindow(hwnd);
    }
    public static IntPtr Find(string contains) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, data) => {
            var text = new StringBuilder(1024); GetWindowText(hwnd, text, text.Capacity);
            if (text.ToString().Contains(contains)) { found = hwnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@
    $fixtureRoot = (Resolve-Path 'tests\fixtures').Path
    $original = [FixtureDesktop]::GetForegroundWindow()
    # Generate known assets only; no screenshot is written.
    $image = New-Object Drawing.Bitmap(1000,400)
    $graphics = [Drawing.Graphics]::FromImage($image)
    $graphics.Clear([Drawing.Color]::White)
    $font = New-Object Drawing.Font('Segoe UI',24)
    $lines = @('algorithm algorithm ALGORITHM','binary search tree','Hello World')
    for ($i=0; $i -lt $lines.Count; $i++) { $graphics.DrawString($lines[$i],$font,[Drawing.Brushes]::Black,40,40+$i*85) }
    $image.Save((Join-Path $fixtureRoot 'image.png'),[Drawing.Imaging.ImageFormat]::Png)
    $font.Dispose(); $graphics.Dispose(); $image.Dispose()
    $pdfObjects = @(
        '<< /Type /Catalog /Pages 2 0 R >>',
        '<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
        '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 792 612] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>',
        '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>'
    )
    $stream = "BT /F1 24 Tf 50 540 Td (algorithm algorithm ALGORITHM) Tj 0 -70 Td (binary search tree) Tj 0 -70 Td (Hello World) Tj ET`n"
    $pdfObjects += "<< /Length $([Text.Encoding]::ASCII.GetByteCount($stream)) >>`nstream`n${stream}endstream"
    $pdf = "%PDF-1.4`n"; $offsets = @(0)
    for ($i=0; $i -lt $pdfObjects.Count; $i++) {
        $offsets += [Text.Encoding]::ASCII.GetByteCount($pdf)
        $pdf += "$($i+1) 0 obj`n$($pdfObjects[$i])`nendobj`n"
    }
    $xref = [Text.Encoding]::ASCII.GetByteCount($pdf)
    $pdf += "xref`n0 6`n0000000000 65535 f `n"
    foreach ($offset in $offsets[1..5]) { $pdf += ('{0:D10} 00000 n ' -f $offset) + "`n" }
    $pdf += "trailer`n<< /Size 6 /Root 1 0 R >>`nstartxref`n$xref`n%%EOF`n"
    [IO.File]::WriteAllBytes((Join-Path $fixtureRoot 'fixture.pdf'),[Text.Encoding]::ASCII.GetBytes($pdf))
    $results = New-Object 'Collections.Generic.List[object]'
    if (($RemainingOnly -or $SettingsOnly) -and (Test-Path -LiteralPath 'docs\application-test-results.json')) {
        foreach ($previous in (Get-Content -LiteralPath 'docs\application-test-results.json' -Raw | ConvertFrom-Json)) {
            if ($previous.status -eq 'passed' -and $previous.application -ne 'Windows Settings' -and
                -not ($results | Where-Object { $_.application -eq $previous.application })) { $results.Add($previous) }
        }
    }
    function Test-Window([string]$Name,[string]$Executable,[string[]]$Arguments,[string]$Title,[int]$Wait=1800) {
        if ($RemainingOnly -and ($results | Where-Object { $_.application -eq $Name -and $_.status -eq 'passed' })) { return }
        if (-not (Test-Path -LiteralPath $Executable)) {
            $results.Add([pscustomobject]@{application=$Name;status='not installed'}); return
        }
        $window = [FixtureDesktop]::Find($Title)
        $createdWindow = $window -eq [IntPtr]::Zero
        $child = $null
        $wasTopmost = $false
        if ($createdWindow) { $child = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru }
        try {
            $deadline = [DateTime]::UtcNow.AddSeconds(15)
            while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
                Start-Sleep -Milliseconds 100; $window = [FixtureDesktop]::Find($Title)
            }
            if ($window -eq [IntPtr]::Zero) {
                $results.Add([pscustomobject]@{application=$Name;status='fixture window unavailable'}); return
            }
            $wasTopmost = [FixtureDesktop]::IsTopmost($window)
            [void][FixtureDesktop]::FocusFixture($window)
            Start-Sleep -Milliseconds $Wait
            # The native probe verifies that this fixture is actually visible and uncovered.
            # OCR capture does not require foreground keyboard focus.
            $output = & '.\build\app-probe.exe' --window ($window.ToInt64())
            if ($output) {
                $result = $output | ConvertFrom-Json
                $result | Add-Member -NotePropertyName application -NotePropertyValue $Name
                $result | Add-Member -NotePropertyName status -NotePropertyValue $(if($result.passed){'passed'}else{'failed'})
                $results.Add($result)
            } else { $results.Add([pscustomobject]@{application=$Name;status='probe failed'}) }
        } finally {
            if ($createdWindow -and $window -ne [IntPtr]::Zero) { [void][FixtureDesktop]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
            elseif ($window -ne [IntPtr]::Zero) { [FixtureDesktop]::RestoreTopmost($window,$wasTopmost) }
            if ($child) { $child.Dispose() }; Start-Sleep -Milliseconds 500
            $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath 'docs\application-test-results.json' -Encoding UTF8
        }
    }
    $chrome = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
    $edge = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
    $code = Join-Path $env:LOCALAPPDATA 'Programs\Microsoft VS Code\Code.exe'
    $chromeProfile = Join-Path (Resolve-Path 'build').Path 'chrome-test-profile'
    $edgeProfile = Join-Path (Resolve-Path 'build').Path 'edge-test-profile'
    $chromeArgs = @("--user-data-dir=`"$chromeProfile`"",'--no-first-run','--no-default-browser-check','--disable-background-networking','--new-window')
    $edgeArgs = @("--user-data-dir=`"$edgeProfile`"",'--no-first-run','--no-default-browser-check','--disable-background-networking','--new-window')
    if (-not $RemainingOnly -and -not $SettingsOnly) {
        Test-Window 'Chrome browser text' $chrome ($chromeArgs + "`"$(Join-Path $fixtureRoot 'browser.html')`"") 'Visual Find fixture - browser text'
        Test-Window 'Edge browser text' $edge ($edgeArgs + "`"$(Join-Path $fixtureRoot 'browser.html')`"") 'Visual Find fixture - browser text'
        Test-Window 'Edge PDF viewer' $edge ($edgeArgs + "`"$(Join-Path $fixtureRoot 'fixture.pdf')`"") 'fixture.pdf' 2500
        Test-Window 'Notepad' "$env:WINDIR\System32\notepad.exe" @("`"$(Join-Path $fixtureRoot 'VisualFind-fixture.txt')`"") 'VisualFind-fixture.txt'
    }
    if (-not $SettingsOnly) {
    $codeProfile = Join-Path (Resolve-Path 'build').Path 'code-test-profile'
    $codeExtensions = Join-Path (Resolve-Path 'build').Path 'code-test-extensions'
    $electronNode = [Environment]::GetEnvironmentVariable('ELECTRON_RUN_AS_NODE','Process')
    $codeIpc = [Environment]::GetEnvironmentVariable('VSCODE_IPC_HOOK_CLI','Process')
    try {
        [Environment]::SetEnvironmentVariable('ELECTRON_RUN_AS_NODE',$null,'Process')
        [Environment]::SetEnvironmentVariable('VSCODE_IPC_HOOK_CLI',$null,'Process')
        Test-Window 'VS Code' $code @("--user-data-dir=`"$codeProfile`"","--extensions-dir=`"$codeExtensions`"",'--disable-extensions','--skip-welcome','--skip-release-notes','--new-window',"`"$(Join-Path $fixtureRoot 'VisualFind-fixture.txt')`"") 'VisualFind-fixture.txt' 2500
    } finally {
        [Environment]::SetEnvironmentVariable('ELECTRON_RUN_AS_NODE',$electronNode,'Process')
        [Environment]::SetEnvironmentVariable('VSCODE_IPC_HOOK_CLI',$codeIpc,'Process')
    }
    Test-Window 'Raster image in Chrome' $chrome ($chromeArgs + "`"$(Join-Path $fixtureRoot 'image.html')`"") 'Visual Find fixture - raster image'
    Test-Window 'Paused video frame in Chrome' $chrome ($chromeArgs + "`"$(Join-Path $fixtureRoot 'video.html')`"") 'Visual Find fixture - paused video ready' 1000
    }
    $settings = [FixtureDesktop]::Find('Settings')
    $createdSettings = $settings -eq [IntPtr]::Zero
    if ($createdSettings) {
        Start-Process 'ms-settings:display' -WindowStyle Hidden
        Start-Sleep -Milliseconds 2000
        $settings = [FixtureDesktop]::Find('Settings')
    }
    if ($settings -ne [IntPtr]::Zero) {
        $settingsWasTopmost = [FixtureDesktop]::IsTopmost($settings)
        try {
            [void][FixtureDesktop]::FocusFixture($settings)
            Start-Sleep -Milliseconds 1000
            $probeOutput = & '.\build\app-probe.exe' --settings --window ($settings.ToInt64())
            if ($LASTEXITCODE -eq 0 -and $probeOutput) {
                $result = $probeOutput | ConvertFrom-Json
                $result | Add-Member -NotePropertyName application -NotePropertyValue 'Windows Settings'
                $result | Add-Member -NotePropertyName status -NotePropertyValue $(if($result.passed){'passed'}else{'failed'})
                $results.Add($result)
            } else { $results.Add([pscustomobject]@{application='Windows Settings';status='probe failed or target not visible'}) }
        } finally {
            if ($createdSettings) { [void][FixtureDesktop]::PostMessage($settings,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
            else { [FixtureDesktop]::RestoreTopmost($settings,$settingsWasTopmost) }
        }
    } else { $results.Add([pscustomobject]@{application='Windows Settings';status='fixture window unavailable'}) }
    $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath 'docs\application-test-results.json' -Encoding UTF8
    $results | Format-Table application,status,algorithm_matches,phrase_matches,capture_ms,ocr_ms -AutoSize
    [void][FixtureDesktop]::SetForegroundWindow($original)
} finally { Pop-Location }
