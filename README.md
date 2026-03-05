<div align="center">

# 🔥 TANTRUMS
**A Vibe-Coded, Blazing-Fast, Natively Compiled Programming Language.**

[![C++23](https://img.shields.io/badge/C++-23-blue.svg?style=for-the-badge&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/23) 
[![LLVM](https://img.shields.io/badge/LLVM-Backend-magenta.svg?style=for-the-badge&logo=llvm)](https://llvm.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](https://opensource.org/licenses/MIT)

*Because writing a compiler from scratch makes you want to throw one.*

</div>

> ⚠️ **Full disclosure:** This entire language—the parser, the AST, the LLVM codegen, and the VS Code extension—was vibe-coded with AI assistance. No formal language theory textbooks were harmed (or opened) in the making of this project. It works, it's blazing fast, and it's probably held together by duct tape and good vibes.

---

## ⚡ What is Tantrums?

**Tantrums** is a statically / dynamically typed language with a clean C-like syntax that compiles *directly to native hardware instructions* using an embedded **LLVM** backend. It completely bypasses virtual machines, interpreters, and heavy garbage collectors.

The language is designed for speed, flexibility, and absolute developer control, scaling seamlessly from dynamic scripting to system-level statically typed architectures.

<br>

---

## 🚀 Quick Start

Build the native compiler and start running `.42AHH` source files on bare metal.

### 1️⃣ Build the Compiler
Tantrums requires an active **C++23** compiler (Clang/GCC recommended) and **CMake 3.15+**.

```bash
# Generate the build configuration
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build the LLVM-powered Tantrums executable
cmake --build build --config Release
```

### 2️⃣ Write Your First Script
Create a new file named `hello.42AHH`:

```cpp
#mode static;

tantrum void main() {
    print("Hello, World! Welcome to Tantrums.");
}
```

### 3️⃣ Run It Natively
```bash
# Compiles to a native executable and runs immediately
tantrums run hello.42AHH

# Or explicitly build a standalone binary (.exe / ELF)
tantrums build hello.42AHH
```

<br>

---

## ✨ Features at a Glance

Tantrums isn't just a toy; it is packed with serious system-level controls and dynamic data structures.

| Capability | Supported | Description |
| :--- | :---: | :--- |
| **LLVM Codegen** | ✅ | Emits native binaries using a `NaN`-boxing 64-bit value architecture. |
| **Typing System** | ✅ | Native `int`, `float`, `string`, `bool`, `list`, `map`, and pointers `int*`. |
| **Function Paradigms** | ✅ | Uses the `tantrum` keyword. Supports `void` and strict returns. |
| **Control Flow** | ✅ | `if`/`else`, `for-in`, `while`, `break`, `continue`, and `switch`. |
| **Operators** | ✅ | Native math, logical operations, and compound math (`++`, `--`, `+=`). |
| **Exception Handling** | ✅ | Fast, native `try`/`catch`/`throw` blocks powered by `setjmp`/`longjmp`. |
| **Native Match** | ✅ | Blazing fast `math.sin`, `math.floor`, `math.random_int()`, etc. |
| **Dynamic Structures** | ✅ | Immutable `string`s, mutable arrays `[]`, and hash maps `{}`. |
| **File Directives** | ✅ | `#mode static`, `#mode dynamic`, `#mode both`. |
| **VS Code Tooling** | ✅ | Dedicated extension with Intelligence, formatting, and syntax highlighting. |

<br>

---

## ⚙️ The Dual-Type Philosophy

Why choose between static safety and dynamic prototyping when you can have both? Tantrums allows you to swap typing philosophies **per file** globally using `#mode` directives.

### 🛡️ `#mode static;` (Full Enforcement)
Every function must declare a return type. All code paths must safely return. Types are strictly enforced everywhere.

```cpp
#mode static;

tantrum int multiply(int a, int b) {
    return (a * b);
}

tantrum void main() {
    int result = multiply(10, 5);
    print(result);  // 50
}
```

### 🛹 `#mode dynamic;` (Scripting Mode)
Zero type-checking annotations. Perfect for fast prototyping.

```cpp
#mode dynamic;

tantrum main() {
    x = "hello"; // Strings!
    x = 3.14;    // Floats!
    print(x);
}
```

---

## 🧠 Memory Architecture & Safety

Tantrums operates natively using a highly optimized **NaN-boxed uint64_t representation** for its variables. 

Tantrums allows manual heap allocation via `alloc` and `free` while maintaining an overarching **Safety Net Architecture** natively.

```cpp
#autoFree false;

tantrum void main() {
    int* p = alloc int(42);
    
    print(*p);  // 42
    *p = 99;
    print(*p);  // 99
    
    free p;

    // Caught natively at runtime preventing host-crashes!
    // print(*p); -> [Tantrums Runtime Error] Null pointer dereference!
}
```

> **Note on Auto-Free**: The experimental 7-layer memory safety system (compile-time escape analysis) is currently undergoing modifications for optimal LLVM compatibility. Explicit heap management is advised in the meantime.

---

## 🔬 Deep Profiling Native API

Track performance, heap allocations, and process loads instantly. No external tools and no standard library imports required.

```cpp
int start = getCurrentTime();

// Heavy logic
for i in range(100000) { /* ... */ }

int end = getCurrentTime();

print("Elapsed Time: " + toSeconds(end - start) + "s");
print("Heap Peak: " + bytesToMB(getHeapPeakMemory()) + " MB");
print("Process RSS: " + bytesToMB(getProcessMemory()) + " MB");
```

---

## 📦 The Modular Standard Library

A highly optimized modular library system is bundled directly into the compiler, wrapping calls at the AST level directly into optimized LLVM IR instructions!

### Standard Mathematics

```cpp
/* Executes instantaneously without map lookups */
float angle = math.cos(math.random_float(0, 3.14));
int diceRoll = math.random_int(1, 6);
int rounded = math.floor(3.99); 
```

### Modular Imports
```cpp
// math_helper.42AHH
tantrum int square(int n) { return (n * n); }

// main.42AHH
use math_helper.42AHH;

tantrum void main() {
    print(square(8)); // 64
}
```

---

## 🎨 VS Code Extension
Tantrums provides an officially bundled VS Code Extension out-of-the-box (`tantrums-vscode/`).

**Features:**
- **Vibrant Syntax Highlighting** (Directives, Keywords, Operators)
- **IntelliSense & Snippets** (30+ built-in completions)
- **Hover Documentation** (Instant built-in signature reading)
- **Diagnostics** (Caught-on-save checks for semantics and memory risks)

> **To Install:** Simply copy the `tantrums-vscode/` directory directly into `%USERPROFILE%\.vscode\extensions\`.

---

## 🗺️ Engineering Roadmap

The language is rapidly evolving. Here is a snapshot of the native rollout:

- **v1.0 (Current)** — Full LLVM codegen completed, try-catch handlers, native switch systems, core memory protections, mathematical frameworks.
- **v2.0** — Slab allocation integration, full Auto-Free 7-Layer Escape Analysis optimizations, FS/IO abstractions.
- **v3.0** — Win32 Windowing module, OpenGL graphical manipulation pipelines.
- **v4.0** — Deep Link-Time Optimization (LTO), and auto SIMD vectorizations.

---

## 🤝 Contributing
This is a passion project heavily focused on raw speed and enjoyable functionality, deliberately breaking traditional CS theory boundaries. 

If it's broken, open an issue. If it works perfectly and is terrifyingly fast—also open an issue to let us know. PRs are accepted with open arms!

---

<div align="center">

<h3>📄 License</h3>
Do whatever you want with it under the MIT License. <br>
<i>If it breaks, you get to keep both pieces.</i>

<br>

---
**Built with vibes, LLVM architectures, and highly questionable life choices.**

</div>