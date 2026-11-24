# Contributing

Thanks for helping improve Visual Find.

1. Open an issue first for larger changes.
2. Build and test (see the README): `powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Test`
3. Keep changes small and focused; match the existing C++20 style (header-only helpers in `src/`).
4. Describe what you tested. Tests that need an interactive desktop (`tests\app_matrix.ps1`, `tests\desktop_workflow.ps1`) are run manually.
5. By contributing you agree your work is licensed under the [MIT license](LICENSE).
