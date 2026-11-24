# Third-party notices

Visual Find's original source and documentation use the [project MIT license](../LICENSE). This directory preserves notices for the C++/WinRT projection and the MinGW/GCC components used by the build. The individual notice files retain their respective copyright statements and license terms.

| Notice | Component or purpose |
| --- | --- |
| [CppWinRT-MIT.txt](CppWinRT-MIT.txt) | Microsoft's C++/WinRT MIT license. The build downloads Microsoft.Windows.CppWinRT 2.0.250303.1 and generates projection headers from the host Windows metadata. |
| [MinGW-runtime.txt](MinGW-runtime.txt) | MinGW-w64 runtime's overall notice. |
| [MinGW-w64-runtime.txt](MinGW-w64-runtime.txt) | Runtime redistribution notices and notices for constituent code. |
| [MinGW-w64.txt](MinGW-w64.txt) | MinGW-w64 notice inventory and component information. |
| [Winpthreads.txt](Winpthreads.txt) | Notices for the MinGW-w64 Windows pthread implementation used by the toolchain's threading support. |
| [GCC-GPL-3.txt](GCC-GPL-3.txt) | GNU General Public License version 3 text accompanying the GCC runtime notices. |
| [GCC-Runtime-Exception.txt](GCC-Runtime-Exception.txt) | GCC Runtime Library Exception text accompanying the GCC runtime notices. |
| [GCC-LGPL-2.1.txt](GCC-LGPL-2.1.txt) | GNU Lesser General Public License version 2.1 text referenced by constituent toolchain/runtime notices. |

The portable package includes this directory and the project license. GCC runtime components are linked statically into the executable. The compiler, resource compiler, C++/WinRT generator, generated-header tree, and NuGet archive are build inputs and are not included in the portable package.

Windows system libraries, Windows OCR, and installed OCR language components are supplied by Windows and are not redistributed here. The package does not bundle a separate OCR model or OCR engine.
