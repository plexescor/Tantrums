# Tantrums Language Support

[![Marketplace](https://img.shields.io/visual-studio-marketplace/v/plexescor.tantrums-lang?style=flat-square&color=blue)](https://marketplace.visualstudio.com/items?itemName=plexescor.tantrums-lang)
[![Installs](https://img.shields.io/visual-studio-marketplace/i/plexescor.tantrums-lang?style=flat-square)](https://marketplace.visualstudio.com/items?itemName=plexescor.tantrums-lang)
[![License](https://img.shields.io/github/license/plexescor/tantrums?style=flat-square)](LICENSE)

**Tantrums** is a modern, high-performance programming language designed for developers who want the control of low-level memory management with the safety and ergonomics of higher-level syntax. This extension provides full IDE support to make your Tantrums development experience seamless and powerful.

## ✨ Features

*   **Syntax Highlighting**: Rich, color-coordinated highlighting for keywords, types, pointers, and directives.
*   **IntelliSense & Autocomplete**: Smart suggestions for built-in functions, keywords, and user-defined symbols.
*   **Cross-File Support**: Full support for `use` statements. Symbols from imported files are automatically added to your autocomplete and used for linting.
*   **Advanced Diagnostics**: Real-time static analysis detecting undefined variables, type mismatches, missing returns, and memory management errors.
*   **Memory Management Tooling**: Special support for `#autoFree`, `#allowMemoryLeaks`, and pointer-safe diagnostics.
*   **Comprehensive Hover Docs**: Instant documentation for every built-in function, directive, and keyword.
*   **Rich Snippets library**: Pre-built snippets for functions, control flow, memory headers, and time/memory APIs.

## 🚀 Quick Start

1.  **Install** the extension from the VS Code Marketplace.
2.  **Open** or create a file with the `.42AHH` extension.
3.  **Start Coding!** Use snippets like `tantrum`, `manualheader`, or `arenaheader` to get started quickly.

## 🧠 Memory Management

Tantrums features a unique two-layer memory management system. This extension helps you navigate it with specific diagnostics and hovers:

*   **#mode static;**: Enforces strict typing.
*   **#autoFree true;**: Enables the high-performance compiler-managed auto-free system.
*   **#allowMemoryLeaks true;**: Converts memory leak errors into warnings (perfect for arena-style allocation).

## 🛠️ Commands

*   `Tantrums: Run File` - Runs the current `.42AHH` file.
*   `Tantrums: Compile File` - Compiles the current file to bytecode.
*   `Tantrums: Execute Bytecode` - Executes a compiled `.42ass` file.

## 📄 License

This extension is licensed under the [MIT License](LICENSE).

---
Developed with ❤️ by **plexescor**
