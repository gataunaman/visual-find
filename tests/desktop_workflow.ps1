$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DesktopWorkflowFocus {
 [DllImport("user32.dll")] public static extern IntPtr GetShellWindow();
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@
$desktopShell = New-Object -ComObject Shell.Application
try {
    $desktopShell.MinimizeAll()
    Start-Sleep -Milliseconds 500
    [void][DesktopWorkflowFocus]::SetForegroundWindow([DesktopWorkflowFocus]::GetShellWindow())
    & (Join-Path (Split-Path $PSScriptRoot) 'build\desktop-workflow.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Desktop workflow validation failed.' }
} finally { $desktopShell.UndoMinimizeALL() }
