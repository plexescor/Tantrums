# 🔥 Tantrums

**A vibe-coded programming language built from scratch in C++.**

> ⚠️ **Full disclosure:** This entire language — the compiler, VM, bytecode format, and VS Code extension — was vibe-coded with AI assistance. No formal language theory textbooks were harmed (or opened) in the making of this project. It works, it's fun, and it's probably held together by duct tape and good vibes.

Tantrums compiles `.42AHH` source files into `.42ass` bytecode, which runs on a custom stack-based virtual machine. Yes, those are the real file extensions. No, we're not changing them.

---

## 🚀 Quick Start

```bash
# Build from source (CMake + C++23 compiler)
cmake -B build -S .
cmake --build build --config Release

# Run a program
tantrums run hello.42AHH

# Or compile and execute separately
tantrums compile hello.42AHH   # → hello.42ass
tantrums exec hello.42ass
```

### Hello World

```c
tantrum main()
{
    print("Hello, World!");
}
```

Save as `hello.42AHH`, run with `tantrums run hello.42AHH`. That's it.

---

## 📖 What Is This?

Tantrums is a dynamically-typed language with **optional static types**, a C-like syntax, and a bytecode compiler + VM architecture inspired by languages like Lua and Python — except we built it without really knowing what we were doing, and it somehow works.

### The Pipeline

```
.42AHH source → Lexer → Parser → AST → Compiler → .42ass bytecode → VM → Output
```

The `.42ass` bytecode is portable — compile on Windows, run on Linux (as long as you have the VM binary for that platform).

---

## ✨ Features

| Feature | Status | Notes |
|---|---|---|
| Variables | ✅ | Dynamic (`x = 5`) or typed (`int x = 5`) |
| Types | ✅ | `int`, `float`, `string`, `bool`, `list`, `map` |
| Functions | ✅ | `tantrum` keyword, optional return type |
| Control flow | ✅ | `if`/`else`, `while`, `for-in` |
| Operators | ✅ | Arithmetic, comparison, logical, `++`, `--`, `+=`, etc. |
| Strings | ✅ | Escape sequences, auto-concat with other types |
| Lists & Maps | ✅ | `[1, 2, 3]`, `{"key": "value"}`, indexing |
| Imports | ✅ | `use helper.42AHH;` — same directory |
| Type checking | ✅ | Compile-time errors for typed params/vars |
| Mode directives | ✅ | `#mode static;` / `#mode dynamic;` / `#mode both;` |
| Pointers | ✅ | `alloc`/`free`, `&`/`*` operators |
| Error handling | ✅ | `throw`, `try`/`catch` |
| Bytecode | ✅ | Binary `.42ass` format, cross-platform |
| VS Code extension | ✅ | Syntax highlighting, IntelliSense, hover docs, commands |

---

## ⚙️ Typing Modes

Control how strict the compiler is about types:

```c
#mode static;    // ALL variables must have type annotations
#mode dynamic;   // NO type checking (types ignored)
#mode both;      // Default: typed vars checked, untyped are dynamic
```

**Static mode** — forces you to declare every variable with a type:
```c
#mode static;

tantrum main()
{
    int x = 42;       // ✅ OK
    x = 10;           // ✅ OK (already declared)
    y = "hello";      // ❌ COMPILE ERROR: must use string y = "hello";
}
```

**Dynamic mode** — types are purely decorative, no checking at all:
```c
#mode dynamic;

tantrum main()
{
    int x = "hello";  // ✅ OK (no type checking)
    x = 3.14;         // ✅ OK
}
```

**Both mode** (default) — typed variables are checked, untyped are free:
```c
tantrum main()
{
    int x = 42;       // ✅ checked
    x = "nope";       // ❌ ERROR: can't assign string to int
    y = "anything";   // ✅ OK (untyped = dynamic)
}
```

---

## 🧪 Language Tour

### Variables & Types

```c
// Dynamic — no type annotation needed
x = 42;
name = "Tantrums";

// Typed — compiler enforces at compile time
int count = 10;
float pi = 3.14;
string greeting = "Hello";
bool alive = true;
list items = [1, 2, 3];
map data = {"name": "john", "age": "21"};
```

### Functions

Functions are declared with the `tantrum` keyword (yes, really).

```c
tantrum int add(float a, float b)
{
    return a + b;
}

tantrum greet(string name)
{
    print("Hello, " + name + "!");
}

tantrum main()
{
    print(add(3.14, 2.86));   // 6
    greet("World");            // Hello, World!
}
```

### Control Flow

```c
// If / else
if (x > 10)
{
    print("big");
}
else
{
    print("small");
}

// For-in loop — works with ranges, lists, and strings
for i in range(10)
{
    print(i);
}

for ch in "Hello"
{
    print(ch);    // H, e, l, l, o
}

// While loop
while (count > 0)
{
    print(count);
    count--;
}
```

### Operators

All the usual suspects, plus some extras:

```c
// Arithmetic
x = 10 + 5;     x = 10 - 5;
x = 10 * 5;     x = 10 / 5;     x = 10 % 3;

// Compound assignment
i += 1;   i -= 2;   i *= 3;   i /= 4;   i %= 5;

// Increment / Decrement
i++;   ++i;   i--;   --i;

// Comparison (including reversed operators because why not)
x == y    x != y
x < y     x > y     x <= y    x >= y
x => y    x =< y    // same as >= and <=

// Logical
x and y   x or y    !x
x && y    x || y

// String concat with auto-conversion
"Score: " + 42    // → "Score: 42"
```

### Imports

Split your code across files:

```c
// helper.42AHH
tantrum int square(int n)
{
    return n * n;
}
```

```c
// main.42AHH
use helper.42AHH;

tantrum main()
{
    print(square(7));    // 49
}
```

### Type Checking

When you use type annotations, the compiler enforces them:

```c
tantrum int add(float a, float b)
{
    return a + b;
}

tantrum main()
{
    print(add(5, 10));          // ✅ OK — int promotes to float
    print(add("hello", "no"));  // ❌ Compile error: param 1 expects 'float' got 'string'

    int x = 42;
    x = "nope";                 // ❌ Compile error: can't assign 'string' to 'int'
}
```

Untyped variables remain fully dynamic — no type errors.

### Input

```c
x = input("Enter something: ");          // always returns string

int n = input("Enter a number: ");       // auto-cast to int
float f = input("Enter a decimal: ");    // auto-cast to float
bool b = input("Yes or no? ");           // "true"→true, "false"→false,
                                          // empty/whitespace→false, else→true
```

### Manual Memory

```c
x = alloc int(42);     // heap-allocate
print(*x);             // dereference → 42
free x;                // deallocate
```

### Error Handling

```c
// Unhandled throw — halts the program
throw "Something went wrong!";

// Try/catch — catch and handle errors
try
{
    throw "oops";
}
catch (e)
{
    print("Caught: " + e);   // "Caught: oops"
}

// Without error variable
try { throw "fail"; }
catch { print("Something failed"); }
```

---

## 🎨 VS Code Extension

The extension lives in `tantrums-vscode/` and provides:

- **Syntax highlighting** — keywords, types, functions, operators, strings, comments, imports
- **IntelliSense** — snippets, keyword/type/builtin completions, user-defined function discovery
- **Hover docs** — hover any keyword, type, or builtin for signature + description
- **Diagnostics** — 18+ checks: type errors, undefined vars/functions, dead code, division by zero, and more
- **Commands** — Run, Compile, Execute from the command palette or right-click menu
- **File icons** — custom icons for `.42AHH` and `.42ass` files

See [EXTENSION.txt](EXTENSION.txt) for the full feature list and installation guide.

---

## 📁 Project Structure

```
├── src/
│   ├── main.cpp              Entry point (CLI: run/compile/exec)
│   ├── lexer.cpp             Tokenizer
│   ├── parser.cpp            Recursive descent parser → AST
│   ├── compiler.cpp          AST → bytecode (with type checking)
│   ├── vm.cpp                Stack-based virtual machine
│   ├── builtins.cpp          Built-in functions (print, input, etc.)
│   ├── value.cpp             Value types and string interning
│   ├── memory.cpp            Memory management
│   ├── ast.cpp               AST node allocation/deallocation
│   └── bytecode_file.cpp     .42ass serialization/deserialization
├── include/                  Header files
├── tantrums-vscode/          VS Code extension
├── REFERENCE.txt             Language syntax reference
├── EXTENSION.txt             VS Code extension guide
├── CMakeLists.txt            Build configuration
└── .gitignore
```

---

## 🏗️ Building

**Requirements:** CMake 3.15+, a C++23 compiler (MSVC, GCC, or Clang)

```bash
cmake -B build -S .
cmake --build build --config Release
```

The binary will be at:
- Windows: `build\Release\tantrums.exe`
- Linux/Mac: `build/tantrums`

Add it to your PATH and you're good to go.

---

## 🗺️ Roadmap

Things we'd like to add eventually (no promises, this is a vibe project):

- [ ] Standard library modules (`use math;`, `use fs;`)
- [ ] C/C++ FFI (`foreign "C" { ... }`)
- [ ] Native compilation (`.42AHH` → `.exe`)
- [ ] Debugger integration
- [ ] Graphics via OpenGL/Vulkan modules
- [ ] Package manager (lol)

---

## 🤝 Contributing

This is a vibe-coded passion project. If you want to contribute, go for it — just know that the codebase is "AI-assisted artisanal C++" and may not follow conventional best practices. We optimized for fun, not correctness.

That said, PRs are welcome. If something is broken, it's probably a feature we haven't documented yet.

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
