# Changelog

All notable changes to the **Tantrums Language Support** extension will be documented in this file.

## [0.2.0] - 2026-03-01

### Added
- **Cross-File Symbol Support**: The extension now resolves symbols across files using `use` statements.
- **Improved Syntax Highlighting**: Added variable usage coloring, pointer type support (`*`), and `void` return types.
- **Preprocessor Support**: Highlighting and validation for `#mode`, `#autoFree`, and `#allowMemoryLeaks`.
- **Advanced Diagnostics**:
  - Pointer-sensitive "undefined variable" checks.
  - Semicolon requirement checks for pointer assignments.
  - Directive logic validation (e.g., `#allowMemoryLeaks` requires `#autoFree false`).
- **Expanded Hover Information**: Added documentation for memory management directives and time/memory APIs.
- **Rich Snippet Library**: Added snippets for headers, memory management, and advanced language constructs.

### Changed
- Refined regex patterns for more robust symbol collection regardless of spacing in declarations.
- Updated `package.json` with the official publisher ID.

### Fixed
- Fixed issue where duplicate variables were not correctly colored.
- Fixed false "missing return" warnings in `void` functions.
- Fixed false "undefined" errors for `null` and `void`.

## [0.1.0] - Initial Release
- Basic syntax highlighting for Tantrums (.42AHH).
- Simple IntelliSense for core keywords.
- Basic diagnostic checks.
