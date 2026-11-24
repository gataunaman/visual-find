param([switch]$Test, [switch]$Package)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Push-Location $PSScriptRoot
try {
    $compiler = Get-Command g++.exe -ErrorAction SilentlyContinue
    if (-not $compiler) { throw 'Install MSYS2 mingw-w64-x86_64-gcc and add C:\msys64\mingw64\bin to PATH. See README.md.' }
    $resourceCompiler = Get-Command windres.exe -ErrorAction Stop
    $version = '2.0.250303.1'
    New-Item -ItemType Directory -Force -Path '.tools','build','dist\VisualFind' | Out-Null
    $projectionTool = '.tools\cppwinrt\bin\cppwinrt.exe'
    if (-not (Test-Path -LiteralPath $projectionTool)) {
        $url = "https://api.nuget.org/v3-flatcontainer/microsoft.windows.cppwinrt/$version/microsoft.windows.cppwinrt.$version.nupkg"
        & curl.exe --fail --location --retry 2 --connect-timeout 15 --max-time 90 $url --output '.tools\cppwinrt.zip'
        if ($LASTEXITCODE -ne 0) { throw 'Could not download Microsoft C++/WinRT. Internet access is needed only for this build dependency.' }
        Expand-Archive -LiteralPath '.tools\cppwinrt.zip' -DestinationPath '.tools\cppwinrt' -Force
    }
    if (-not (Test-Path -LiteralPath 'build\generated\winrt\Windows.Media.Ocr.h')) {
        & $projectionTool -input "$env:WINDIR\System32\WinMetadata" -output 'build\generated' -include Windows.Media.Ocr Windows.Graphics.Imaging Windows.Globalization Windows.Foundation
        if ($LASTEXITCODE -ne 0) { throw 'C++/WinRT projection generation failed.' }
    }
    $common = @('-std=c++20','-O2','-fno-strict-aliasing','-Wall','-Wextra','-Wno-missing-field-initializers',
        '-D_WIN32_WINNT=0x0A00','-DWINVER=0x0A00','-DNOMINMAX','-DUNICODE','-D_UNICODE',
        '-Ibuild\generated','-static','-static-libgcc','-static-libstdc++')
    $libraries = @('-lwindowsapp','-lole32','-loleacc','-loleaut32','-luuid','-lgdi32','-luser32','-lnormaliz','-ldwmapi','-lcomctl32','-lshell32','-lpsapi')
    & $resourceCompiler.Source -I src src\app.rc -o build\resources.o
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & $compiler.Source @common src\app.cpp build\resources.o -o dist\VisualFind\VisualFind.exe -municode -mwindows -s @libraries
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    & $compiler.Source @common tests\search_tests.cpp -o build\search-tests.exe -lnormaliz -loleacc -loleaut32 -lole32 -luuid -luser32 -lgdi32
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
    if (Test-Path -LiteralPath 'tests\benchmark.cpp') {
        & $compiler.Source @common tests\benchmark.cpp build\resources.o -o build\VisualFind.Bench.exe -s @libraries
        if ($LASTEXITCODE -ne 0) { throw 'Benchmark compilation failed.' }
    }
    if (Test-Path -LiteralPath 'tests\app_probe.cpp') {
        & $compiler.Source @common tests\app_probe.cpp -o build\app-probe.exe -s @libraries
        if ($LASTEXITCODE -ne 0) { throw 'Application compatibility probe compilation failed.' }
    }
    if (Test-Path -LiteralPath 'tests\desktop_workflow.cpp') {
        & $compiler.Source @common tests\desktop_workflow.cpp -o build\desktop-workflow.exe -s @libraries
        if ($LASTEXITCODE -ne 0) { throw 'Desktop workflow probe compilation failed.' }
    }
    if ($Test) {
        & '.\build\search-tests.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Search tests failed.' }
    }
    if ($Package) {
        Copy-Item -LiteralPath 'README.md','LICENSE' -Destination 'dist\VisualFind'
        Copy-Item -LiteralPath 'docs' -Destination 'dist\VisualFind' -Recurse -Force
        Copy-Item -LiteralPath 'licenses' -Destination 'dist\VisualFind' -Recurse -Force
        Compress-Archive -Path 'dist\VisualFind' -DestinationPath 'dist\VisualFind-1.0.0-win-x64.zip' -Force
    }
    Write-Output 'Built dist\VisualFind\VisualFind.exe'
} finally { Pop-Location }
