# 🔥 Tantrums

**A vibe-coded programming language built from scratch in C++.**

> ⚠️ **Full disclosure:** This entire language — the compiler, VM, bytecode format, and VS Code extension — was vibe-coded with AI assistance. No formal language theory textbooks were harmed (or opened) in the making of this project. It works, it's fast, and it's probably held together by duct tape and good vibes.

Tantrums compiles `.42AHH` source files into `.42ass` bytecode, which runs on a custom stack-based virtual machine. Yes, those are the real file extensions. No, we're not changing them.

---

## 🚀 Quick Start

```bash
# Build from source (CMake + C++23 compiler)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run a program
tantrums run hello.42AHH

# Or compile and execute separately
tantrums compile hello.42AHH   # → hello.42ass
tantrums exec hello.42ass
```

### Hello World

```
tantrum void main()
{
    print("Hello, World!");
}
```

Save as `hello.42AHH`, run with `tantrums run hello.42AHH`. That's it.

---

## 📖 What Is This?

Tantrums is a statically or dynamically typed language with a C-like syntax, a bytecode compiler + VM architecture, and a multi-layer memory safety system — built without a garbage collector or a borrow checker.

### The Pipeline

```
.42AHH source → Lexer → Parser → AST → Compiler → .42ass bytecode → VM → Output
```

The `.42ass` bytecode is portable — compile on Windows, run on Linux with the same file.

---

## ✨ Features

| Feature | Status | Notes |
|---|---|---|
| Variables | ✅ | Dynamic (`x = 5`) or typed (`int x = 5`) |
| Types | ✅ | `int`, `float`, `string`, `bool`, `list`, `map` |
| Functions | ✅ | `tantrum` keyword, optional return type |
| Void functions | ✅ | `tantrum void foo()` |
| Pointer return type | ✅ | `tantrum int* foo()` |
| Control flow | ✅ | `if`/`else`, `while`, `for-in`, `break`, `continue` |
| Operators | ✅ | Arithmetic, comparison, logical, `++`, `--`, `+=`, etc. |
| Strings | ✅ | Escape sequences, auto-concat with other types |
| Lists & Maps | ✅ | `[1, 2, 3]`, `{"key": "value"}`, indexing |
| Imports | ✅ | `use helper.42AHH;` — same directory |
| Type checking | ✅ | Compile-time errors for typed params/vars |
| Mode directives | ✅ | `#mode static;` / `#mode dynamic;` / `#mode both;` |
| Return enforcement | ✅ | `#mode static` enforces return types, void, all paths |
| Manual memory | ✅ | `alloc`/`free` with full safety checks |
| Auto memory | ✅ | `#autoFree true` (default) — compiler + runtime auto-free |
| Memory directives | ✅ | `#autoFree true/false`, `#allowMemoryLeaks true/false` |
| Escape analysis | ✅ | Smart compile-time pointer escape detection |
| Use-after-free | ✅ | Runtime error + line number |
| Double-free | ✅ | Runtime error + line number |
| Null dereference | ✅ | Runtime error + line number |
| Leak detection | ✅ | Exit report with line numbers, types, sizes, log file |
| Error handling | ✅ | `throw`, `try`/`catch`, nested rethrow |
| Bytecode | ✅ | Binary `.42ass` format, cross-platform |
| VS Code extension | ✅ | Syntax highlighting, IntelliSense, hover docs, commands |
| Built-in profiling | ✅ | `getCurrentTime()`, `getProcessMemory()`, `bytesToMB()` etc. |

---

## ⚙️ Typing Modes

Control how strict the compiler is about types with a file-level directive:

```
#mode static;    // ALL variables must have type annotations
#mode dynamic;   // NO type checking (types ignored)
#mode both;      // Default: typed vars checked, untyped are dynamic
```

### Static mode — full enforcement

In `#mode static`, every function must declare a return type. All code paths must return. Types are strictly enforced everywhere.

```
#mode static;

tantrum int add(int a, int b)
{
    return (a + b);
}

tantrum void main()
{
    int result = add(10, 20);
    print(result);
}
```

### Dynamic mode — no checking

```
#mode dynamic;

tantrum main()
{
    int x = "hello";  // OK — no type checking
    x = 3.14;         // OK
}
```

### Both mode (default)

```
tantrum main()
{
    int x = 42;       // checked
    x = "nope";       // ERROR: can't assign string to int
    y = "anything";   // OK — untyped = dynamic
}
```

---

## 🧠 Memory Model

Tantrums has a **three-directive memory system** that lets you choose your memory philosophy per file.

### Directive 1 — `#autoFree`

```
#autoFree true;     // default — compiler + runtime auto-free pointers
#autoFree false;    // full manual control — you free everything
```

With `#autoFree true` (default), the compiler runs escape analysis on every pointer. If a pointer is provably local (not returned, not passed anywhere, not aliased), it inserts a free automatically and tells you:

```
[Tantrums] note: auto-freed 'p' at line 3 (provably local)
```

If the compiler can't prove it's safe, the runtime catches it at scope exit instead.

With `#autoFree false`, you manage everything manually. The leak detector still runs and tells you what you forgot.

### Directive 2 — `#allowMemoryLeaks`

Requires `#autoFree false` first. Allows intentional leaks (arena/region pattern):

```
#autoFree false;
#allowMemoryLeaks true;
```

Compile-time leak errors become warnings instead of aborting. The exit leak report still runs so you know exactly what leaked.

### Memory Safety Stack

```
Layer 1: Compile-time escape analysis    — no false positives on valid code
Layer 2: Compile-time auto-free          — provably local pointers freed automatically
Layer 3: Runtime auto-free               — safety net for ambiguous cases
Layer 4: Runtime use-after-free          — error + line number
Layer 5: Runtime double-free             — error + line number
Layer 6: Runtime null dereference        — error + line number
Layer 7: Exit leak detector              — full report with memleaklog.txt
```

### The Four Memory Modes

| `#autoFree` | `#allowMemoryLeaks` | Behavior |
|---|---|---|
| `true` | `false` | Auto-free on, leaks abort — **default Tantrums** |
| `false` | `false` | Manual memory, leaks abort — like C with valgrind |
| `false` | `true` | Manual memory, leaks warn — arena/region style |
| `true` | `true` | ❌ Invalid — contradictory |

---

## 🧪 Language Tour

### Variables & Types

```
// Dynamic
x = 42;
name = "Tantrums";

// Typed
int count = 10;
float pi = 3.14;
string greeting = "Hello";
bool alive = true;
list items = [1, 2, 3];
map data = {"name": "john", "age": "21"};
```

### Functions

```
tantrum int add(int a, int b)
{
    return (a + b);
}

tantrum void greet(string name)
{
    print("Hello, " + name + "!");
}

tantrum void main()
{
    print(add(10, 20));
    greet("World");
}
```

### Pointer Return Type

```
tantrum int* makeValue()
{
    int* p = alloc int(42);
    return (p);
}

tantrum void main()
{
    int* v = makeValue();
    print(*v);
    free v;
}
```

### Control Flow

```
// If / else
if (x > 10) { print("big"); }
else { print("small"); }

// For-in loop
for i in range(10) { print(i); }
for ch in "Hello" { print(ch); }
for item in [1, 2, 3] { print(item); }

// While loop
while (count > 0) { count--; }

// Break and continue
for i in range(10)
{
    if (i == 5) { break; }
    if (i % 2 == 0) { continue; }
    print(i);
}
```

### Manual Memory

```
#autoFree false;

tantrum void main()
{
    int* p = alloc int(42);
    print(*p);       // 42
    *p = 99;
    print(*p);       // 99
    free p;

    // use-after-free caught at runtime:
    // print(*p);    // [Tantrums Runtime Error] Null pointer dereference!
}
```

### Error Handling

```
try
{
    throw "something broke";
}
catch (e)
{
    print("Caught: " + e);
}

// Nested rethrow
try
{
    try { throw "inner"; }
    catch (e)
    {
        print("Inner: " + e);
        throw "re-thrown";
    }
}
catch (e)
{
    print("Outer: " + e);
}
```

### Built-in Profiling

Zero imports needed:

```
int start = getCurrentTime();
// your code here
int end = getCurrentTime();
print("Elapsed: " + toSeconds(end - start) + "s");
print("RAM: " + bytesToMB(getProcessMemory()) + " MB");
```

### Imports

```
// helper.42AHH
tantrum int square(int n)
{
    return (n * n);
}

// main.42AHH
use helper.42AHH;

tantrum void main()
{
    print(square(7));    // 49
}
```

---

## 🎨 VS Code Extension

Lives in `tantrums-vscode/`. Install by copying to `%USERPROFILE%\.vscode\extensions\`.

Features:
- **Syntax highlighting** — keywords, types, functions, operators, strings, directives
- **IntelliSense** — 30+ snippets, keyword/type/builtin completions, user-defined function discovery
- **Hover docs** — hover any keyword, type, or builtin for signature + description
- **Diagnostics** — 20+ checks including type errors, undefined vars, dead code, memory issues
- **Commands** — Run, Compile, Execute from command palette or right-click
- **File icons** — custom icons for `.42AHH` and `.42ass`

See [EXTENSION.txt](EXTENSION.txt) for the full feature list.

---

## 📁 Project Structure

```
├── src/
│   ├── main.cpp              Entry point (CLI: run/compile/exec)
│   ├── lexer.cpp             Tokenizer
│   ├── parser.cpp            Recursive descent parser → AST
│   ├── compiler.cpp          AST → bytecode + escape analysis + type checking
│   ├── vm.cpp                Stack-based virtual machine
│   ├── builtins.cpp          Built-in functions
│   ├── value.cpp             Value types and string interning
│   ├── memory.cpp            Memory management + leak detection
│   ├── ast.cpp               AST node allocation/deallocation
│   └── bytecode_file.cpp     .42ass serialization/deserialization
├── include/                  Header files
├── tantrums-vscode/          VS Code extension
├── REFERENCE.txt             Language syntax reference
├── EXTENSION.txt             VS Code extension guide
├── TANTRUMS_PLAN.txt         Master planning document
├── CMakeLists.txt            Build configuration
└── .gitignore
```

---

## 🏗️ Building

**Requirements:** CMake 3.15+, C++23 compiler (MSVC, GCC, or Clang)

```bash
# Release build (recommended — debug is ~2x slower)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Binary locations:
- Windows: `build\Release\tantrums.exe`
- Linux/Mac: `build/tantrums`

Add to PATH and you're good.

---

## 🗺️ Roadmap

```
v1.0 (current)  — core language, memory safety, type system, VS Code extension
v2.0            — slab allocator, math/fs/io modules, stdlib
v3.0            — Win32 window module, OpenGL graphics module
v4.0            — JIT compiler (LLVM backend)
v5.0            — native compilation, full LLVM optimization
```

---

## 📊 Performance

Tantrums v0.1.0 bytecode VM vs CPython on the same hardware:

| Benchmark | Tantrums | Python | Winner |
|---|---|---|---|
| Raw loop 100M | 4.26s | 46.86s | Tantrums 11x |
| List 200k append | 0.018s | 0.075s | Tantrums 4x |
| List 200k iterate | 0.013s | 0.106s | Tantrums 8x |
| Map 50k string insert | 0.004s | 0.035s | Tantrums 8.75x |
| Baseline RAM | 3.96 MB | 30.18 MB | Tantrums 7.6x less |

Overall: **8.75x faster than Python** on the test suite. Without a JIT. Without native compilation.

---

## 🤝 Contributing

Vibe-coded passion project. PRs welcome. The codebase is AI-assisted C++ and may not follow conventional best practices. We optimized for fun and correctness, not elegance.

If something is broken, open an issue. If something is unexpectedly working, same.

---

## 📜 Why "Tantrums"?

Because writing a programming language from scratch makes you want to throw one.

---

## 📄 License

Do whatever you want with it. If it breaks, you get to keep both pieces.

---

<p align="center">
  <i>Built with vibes, AI, and questionable life choices.</i>
</p>