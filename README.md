<!-- ========================================================================================= -->
<!-- TANTRUMS LANGUAGE - MASTER README -->
<!-- ========================================================================================= -->

<div align="center">

<!-- <img src="https://raw.githubusercontent.com/gist/Plexescor/12345/tantrums_logo.png" width="300" alt="Tantrums Logo"> -->

# 🔥 TANTRUMS
**A Vibe-Coded, Blazing-Fast, Natively Compiled Programming Language.**

[![C++23](https://img.shields.io/badge/C++-23-blue.svg?style=for-the-badge&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/23) 
[![LLVM Backend](https://img.shields.io/badge/LLVM-Native_Compiler-magenta.svg?style=for-the-badge&logo=llvm)](https://llvm.org/)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL_v3-red.svg?style=for-the-badge)](https://www.gnu.org/licenses/gpl-3.0)
[![Status: Aggressively Fast](https://img.shields.io/badge/Status-Aggressively_Fast-orange.svg?style=for-the-badge)](#)

*Because writing a systems-level native compiler from scratch makes you want to throw one.*

<br>

[What is Tantrums?](#-what-is-tantrums) •
[Quick Start](#-quick-start) •
[The Architecture](#-the-architecture) •
[Memory Model](#-memory-model--true-safety) •
[Language Tour](#-deep-language-tour) •
[VS Code Tooling](#-vs-code-extension)

</div>

<br>
<br>

---

<br>

## ⚠️ A Brutally Honest Disclosure
This entire language—the lexer, the recursive descent parser, the Abstract Syntax Tree (AST), the embedded LLVM code generator, the C-level runtime architecture, and the integrated VS Code extension—was entirely **vibe-coded with heavy AI assistance**. 

No formal programming language theory textbooks were harmed (or even opened) in the making of this project. We did not study Dragon Books. We did not optimize for theoretical elegance. We optimized for **speed, fun, and breaking rules.**

It works, it is aggressively fast, and it is entirely held together by duct tape, raw pointers, intrinsic native hardware calls, and good vibes. Proceed at your own risk. It will bite you if you don't read the docs, but if you treat it right, it will outrun standard interpreters without breaking a sweat.

<br>

---

<br>

## ⚡ What is Tantrums?

**Tantrums** is a statically *or* dynamically typed language featuring a clean, C-like syntax and brace-delimited `{}` block scopes. 

As of version `0.2.x`, Tantrums went through a massive paradigm shift. It completely ripped out its old stack-based bytecode virtual machine in favor of **compiling directly to native hardware executables via an embedded LLVM backend.** 

There are no heavy garbage collectors hanging out in the background destroying your game-loop framerates. There are no borrow checkers fighting you on every compilation cycle. There are no hidden virtual machines interpreting bytecodes dynamically.

There is just you, the hardware array, and a highly efficient 64-bit `NaN`-boxed native value architecture interacting directly with your CPU registers.

It maps beautifully to systems, scales seamlessly for scripting hacks, and executes as standalone lightweight executables.

<br>

---

<br>

## 🚀 Quick Start

Build the native compiler and start emitting executable raw machine code. 
*(Note: We strictly enforce the `.42AHH` file extension. Yes, that is the real extension. No, we are not changing it.)*

### 1️⃣ Build the Compiler Structure
Tantrums requires an active **C++23** compiler (Clang/GCC heavily recommended) and **CMake 3.15+**. You must also have your LLVM `-16` developer libraries accessible within your PATH for the codegen phase.

```bash
# Clone the repository natively
git clone https://github.com/Plexescor/tantrums.git
cd tantrums

# Generate the build configuration
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build the LLVM-powered Tantrums compiler executable
cmake --build build --config Release
```

### 2️⃣ Write Your First Script
Create a new file named `hello.42AHH`:

```cpp
#mode static; // Enforce type strictness for this file

tantrum void main() {
    print("Hello, World! Welcome to the new native era of Tantrums.");
    
    int memoryMB = getProcessMemory() / 1024 / 1024;
    print("Process RSS: " + memoryMB + " MB");
}
```

### 3️⃣ Execute to Bare Metal
```bash
# Compiles to a native executable and runs immediately
tantrums run hello.42AHH

# Or explicitly build a standalone binary (.exe / ELF)
tantrums build hello.42AHH

# Run the standalone executable natively
./hello.exe
```

<br>

---

<br>

## ⚙️ The Dual-Type Philosophy

Why choose between static safety and dynamic prototyping when you can have both instantly? 

Tantrums allows you to swap typing philosophies **per file** globally using `#mode` directives. This lets you prototype quickly in messy sandbox scripts, while locking down your core library architectures natively with strict C-level enforcements.

### 🛡️ `#mode static;` (Full Enforcement)
In `#mode static`, every single function block must declare an explicit return type. All code paths must safely return to the caller, or throw an exception. Variables are locked to their explicit initializations. Return mismatches trigger immediate compile-time fatalities.

```cpp
#mode static; // The hammer drops here.

tantrum int multiply(int a, int b) {
    if (a == 0 || b == 0) {
        return (0);
    }
    return (a * b);
}

tantrum void main() {
    int result = multiply(10, 5);
    print(result);  // Prints out: 50
    
    // result = "string"; -> COMPILE ERROR: Type Mismatch Detected!
}
```

### 🛹 `#mode dynamic;` (Scripting Mode)
Zero type-checking annotations. Perfect for fast, unhinged prototyping where you just want logic to run immediately. Types become highly flexible decorations.

```cpp
#mode dynamic; // Complete freedom.

tantrum main() {
    x = "hello, world"; // Native Strings!
    x = 3.14159;        // Floats instantly assigned over previous pointer arrays!
    print(x);
}
```

### ⚖️ `#mode both;` (The Harmonized Default)
If no directive is specified, `#mode both` is safely assumed. Variables tagged with explicit types (`int count`) are checked statically at compile-time. Untyped variables (`count = 5`) remain entirely free and dynamically allocated in the native registers.

<br>

---

<br>

## 🧠 Memory Architecture & True Safety

Let's talk about memory.

Tantrums has transitioned entirely into an **LLVM Native Executable** layout. Variables are tracked natively inside the hardware via `alloca` instruction generation. Everything passed across functions runs in pure registers utilizing a highly optimized **64-bit NaN-boxed format (`uint64_t`)**, completely bypassing legacy C++ ABI `sret` performance overhead. 

Tantrums natively allows massive manual heap allocation controls via `alloc` and `free` while holding down an overarching **Safety Net Architecture** behind the scenes.

### Explicit Heap Rules

```cpp
#autoFree false; // We explicitly take control off the compiler's hands.

tantrum void main() {
    int* p = alloc int(42);
    
    print(*p);  // 42
    
    *p = 99;    // Safely dereferenced array
    print(*p);  // 99
    
    free p;     // Explicitly hand back the memory block

    // Caught natively at runtime preventing host-crashes!
    // print(*p); -> [Tantrums Runtime Error] Null pointer dereference @line 13!
}
```

### 🚧 The 7-Layer Escape Model (Under Direct LLVM Maintenance)
Tantrums famously features a 7-layer static memory safety system that previously ran within its Bytecode virtual machine space. Currently, as the LLVM codegen is being freshly minted, the **Compile-Time Escape Analysis** phase (which automatically injects `free()` states into pointers that do not escape their localized AST blocks) is **Temporarily Disabled**. 

Currently, all `use-after-free`, `double-free`, and `null dereferences` act correctly to stop segment faults, but automatic lifecycle tracking optimization branches are skipped into LLVM.

You are instructed to comfortably continue managing heap pipelines manually via `alloc`/`free` or letting basic variables simply live natively unboxed on the standard Stack!

<br>

---

<br>

## 🧪 Deep Language Tour

### ➗ Native Mathematical Overrides
Tantrums is ridiculously efficient with standard mathematics. The codebase structurally intercepts mathematical module calls during AST parsing generation to directly fuse them into lightning-fast native LLVM `MathLib` intrinsics. It completely skips map or function-call dictionary lookups.

```cpp
float angle = math.cos(math.random_float(0, 3.14));
int diceRoll = math.random_int(1, 6);
int rounded = math.floor(3.99); 
```

### 💥 Full Exception Control (Natively mapping setjmp/longjmp)
Exceptions compile fully into the executable using low-level C `setjmp` structural instruction bounds, avoiding host segmentation faults silently while breaking through nested function scopes successfully.

```cpp
tantrum void faulting_logic() {
    throw "Exception Hit: critical memory boundary severed!";
}

tantrum void main() {
    try {
        faulting_logic();
    } catch (e) {
        // We natively caught the throw, saving the program block!
        print("Caught Exception Data Block: " + e); 
    }
}
```

### 🛤️ Advanced Control Flow (Switch & Loops)
```cpp
// Fallthrough / Auto-break capable Switch Statement Blocks!
switch (val) {
   case 1: print("Phase 1 Init"); break;
   case 2: print("Phase 2 Execution"); break;
   default: print("Fallback State");
}

// Iterator loops natively handle unboxing internal pointer dynamics behind the scenes
for i in range(10) { print(i); }
for ch in "Hello Strings" { print(ch); } // Iterate native chars

// Core mathematical compound structure mutations map 1-to-1 to Native Ops
int count = 10;
count--;
count += 5;
```

### 🔬 Deep Profiling Native Internal API
Track true process limits, structural delays, live heap allocation bounds, and OS-level memory configurations instantly without *ever* needing heavy library installations.

```cpp
// Instant microsecond UNIX timestamps for profiling cycles
int start = getCurrentTime();

// Heavy algorithmic logic here...
for x in range(5000) {
    int* payload = alloc int(x);
    free payload;
}

int end = getCurrentTime();

print("Elapsed Time Output: " + toSeconds(end - start) + "s");

// Direct hardware monitoring binds
print("Active Heap Memory: " + bytesToKB(getHeapMemory()) + " KB");
print("Heap Ceiling Hit: " + bytesToKB(getHeapPeakMemory()) + " KB");
print("Process Master RSS: " + bytesToMB(getProcessMemory()) + " MB");
```

<br>

---

<br>

## 🎨 VS Code Extension
Tantrums provides an officially bundled VS Code Extension built specifically for `.42AHH` pipeline integrations right out-of-the-box in the `tantrums-vscode/` directory.

### Key Visual & Intelligence Features:
- **Vibrant Syntax Highlighting:** Keywords, Type Directives, Control flows, Mathematics, Strings, built-ins, and Comments.
- **IntelliSense & Snippets:** Over 30+ native snippet definitions mapped perfectly across loop scopes, modes, pointers, switch boundaries, and file IO structures.
- **Hover Documentation:** Instantly hover any keyword, built-in, or memory command to receive strict return signatures and help-file definitions on the fly.
- **Live Extension Diagnostics:** On-save active parsing that traps memory leaks, strict type-checking violations, dead-branch logic loops, and unregistered identifier tracking natively inside VS code!

> **How To Install:** Simply copy the bundled `tantrums-vscode/` directory directly into your local machine's `%USERPROFILE%\.vscode\extensions\` pathing directory, restart VS Code quickly, and select any native `.42AHH` file to see the language leap to life!

<br>

---

<br>

## 📁 Core Architectural Layout

```text
├── src/                          [C++ Source Compiler Structure]
│   ├── main.cpp                  Entry point (CLI mappings: build / run)
│   ├── lexer.cpp                 Raw String Text Data → AST Tokens
│   ├── parser.cpp                AST Tokens → Formatted Syntax Trees
│   ├── compiler.cpp              AST Validation, Semantic Type Scopes, Memory Safety Checks
│   ├── LLVMCodegen.cpp           AST Structs → LLVM IR Logic Mapping → Target Machine Binaries
│   ├── runtime.cpp               C-Based Native Executable Headers (Exception handling, Profiling)
│   └── value.cpp                 NaN-Boxed Struct Array Objects & Interning Bounds
│
├── include/                      [System Headers & Runtime Maps]
├── external/                     [Bundled LLVM / Custom Dependency Bridges]
│   └── llvm-backend/             LLVM v16 static library maps for native compiling bounds
│
├── tantrums-vscode/              [Official Built-In IDE plugin directory]
│
├── REFERENCE.txt                 [Exhaustive Language Syntax Standard / Developer Documentation]
├── TANTRUMS_PLAN.txt             [Master Roadmapping & Architectural Internal Tracking]
├── CMakeLists.txt                [Standalone Build Construction Rules]
└── .gitignore                    [Untracked Files]
```

<br>

---

<br>

## 🗺️ Engineering Master Roadmap

The language is aggressively adapting, having just conquered the monumental leap from Bytecode runtime executions to pure LLVM Native Compilation boundaries. 

Here is a raw look at the rollout tracking roadmap standard:

- **v1.0 (Current Rollout State)** — Full LLVM Native codegen replacement, Try-Catch Exception handling loops bridging to C exceptions, mathematical library AST pipeline fusions, strict enforcing of modes on the CLI binaries.
- **v2.0 (The Standard Integration)** — Deep slab allocation integration optimizations to beat basic malloc calls, the Full Modular Restoration of the `Auto-Free` 7-Layer Escape Analysis engine mapped purely to LLVM IR blocks, FileSystem `fs` / `io` modular abstractions.
- **v3.0 (The Graphical Standard)** — Win32 Windowing module wrappers and native OpenGL hardware API pipelines natively accessible within purely typed Tantrums logic!
- **v4.0 (Raw Optimizing Bounds)** — Deep Link-Time Optimization (LTO) implementations, strict branch optimizations natively applied via static bounds, and fully automated SIMD vectorization loops injected manually by the core compiler architecture.

<br>

---

<br>

## 🤝 Contributing
Tantrums is a highly customized passion project aggressively structured around raw execution performance, unhindered developer flexibility, and shattering traditional Computer Science boundaries purely for the sheer enjoyment of building crazy technological systems.

**We unequivocally optimized for mathematical execution speed and raw fun over elegant architecture definitions.**

If something crashes, open an issue. If something works perfectly and is terrifyingly fast—also open an issue to let us know. Pull Requests (PRs) are accepted with wide open arms! 

*Ensure you read `REFERENCE.txt` tightly to match standard syntactical boundaries before shooting off any crazy PRs!*

<br>

---

<br>

## 📜 Why is it called "Tantrums"?
Because sitting there late at night writing a completely bespoke compiler syntax processor, AST tree logic array, and native LLVM code-generator from absolute scratch inside basic text bounds will legitimately make you want to throw one.

<br>

---

<br>

<div align="center">

<h3>⚖️ Open Source Licensing Standards</h3>

<h4>GNU General Public License v3.0 (GPL-3.0)</h4>

You may copy, distribute, modify, and shatter the bounds of this compiled software as long as you track modifications directly inside the source architectures. Any distributions, enhancements, or software branching including (or hooking onto) this GPL-licensed code framework must also be fully accessible and entirely open under the GPL license banner naturally, along with its specific compiling & installation bounds. 

<br>

> *The absolute gold rule:* **If you break the compiler engine, you get to keep both the pieces.**

<br>
<br>

**Built heavily on chaotic vibes, LLVM compiler pipelines, and extremely questionable life choices.**

<p align="center">
  <img src="https://img.shields.io/github/stars/Plexescor/tantrums?style=social" alt="Github Stars">
  <img src="https://img.shields.io/github/forks/Plexescor/tantrums?style=social" alt="Github Forks">
</p>

</div>