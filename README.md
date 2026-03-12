<div align="center">

<br>

<div align="center">
  <img src="assets/logo_bomb.svg" width="200" alt="Tantrums"/>
</div>

<br>

```
████████╗ █████╗ ███╗   ██╗████████╗██████╗ ██╗   ██╗███╗   ███╗███████╗
╚══██╔══╝██╔══██╗████╗  ██║╚══██╔══╝██╔══██╗██║   ██║████╗ ████║██╔════╝
   ██║   ███████║██╔██╗ ██║   ██║   ██████╔╝██║   ██║██╔████╔██║███████╗
   ██║   ██╔══██║██║╚██╗██║   ██║   ██╔══██╗██║   ██║██║╚██╔╝██║╚════██║
   ██║   ██║  ██║██║ ╚████║   ██║   ██║  ██║╚██████╔╝██║ ╚═╝ ██║███████║
   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝╚══════╝
```

<br>

**A vibe-coded, natively compiled, aggressively fast programming language.**
*Built from scratch. Powered by LLVM. Held together by raw ambition and questionable life choices.*

<br>

[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![LLVM](https://img.shields.io/badge/LLVM-Native_Backend-262D3A?style=flat-square&logo=llvm&logoColor=white)](https://llvm.org/)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL_v3-red?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0)
[![Status](https://img.shields.io/badge/Status-Aggressively_Alive-brightgreen?style=flat-square)](#)
[![Extension](https://img.shields.io/badge/Source_Extension-.42AHH-orange?style=flat-square)](#)
[![Secret](https://img.shields.io/badge/Secret_Extension-.trinitrotoluene-black?style=flat-square)](#)

<br>

[Philosophy](#-philosophy) · [Benchmarks](#-benchmarks) · [Architecture](#%EF%B8%8F-architecture) · [Type System](#-type-system) · [Memory Model](#-memory-model) · [Language Design](#%EF%B8%8F-language-design) · [Standard Libraries](#-standard-libraries) · [Profiling API](#-profiling--memory-apis) · [Tooling](#-vs-code-tooling) · [Building](#%EF%B8%8F-building-from-source) · [Roadmap](#%EF%B8%8F-roadmap)

<br>
<br>

</div>

---

## ⚠️ Brutally Honest Disclosure

This entire language — the lexer, the recursive-descent parser, the Abstract Syntax Tree, the embedded LLVM code generator, the C-level runtime, the 7-layer memory safety system, and the VS Code extension — was **entirely vibe-coded with heavy AI assistance.**

No Dragon Books were consulted. No formal language theory was harmed. No Computer Science PhD was involved. What *was* involved: relentless iteration, raw curiosity, zero respect for "the right way to do things," and an unreasonable amount of late-night determination from a 10th grader who just wanted to see if it was possible.

It is possible. It compiles. It's fast. It has a secret file extension. And it will absolutely bite you if you don't read this document.

---

<br>

## 💡 Philosophy

Most programming languages are designed by committees, shaped by years of academic consensus, and sanded down into something palatable for the widest possible audience. Tantrums is none of those things.

Tantrums exists because the question *"what if I just... built one?"* demanded an answer. It is a language designed around **execution speed**, **developer flexibility**, and the deeply personal satisfaction of watching a program you wrote from absolute zero emit real, native machine code that runs directly on hardware.

There are no garbage collectors silently taxing your game loops. No borrow checkers arguing with you about lifetime annotations. No hidden virtual machines interpreting byte streams at runtime. When Tantrums runs your program, it runs. Directly. On the CPU. Exactly as fast as the machine allows.

> *"The name? Because sitting at midnight writing a bespoke recursive-descent parser, an AST code generator, and an LLVM IR emitter simultaneously — from scratch — will make you want to throw one."*

<br>

---

## ⚡ Benchmarks

Numbers don't lie. Here's what a tight 100-million iteration loop looks like across languages:

```
  Language          Time        vs Python
  ─────────────────────────────────────────
  C++               120ms       383x faster
  Tantrums (LLVM)   399ms       115x faster   ← you are here
  Tantrums (VM)     4,260ms     10x faster    ← old path, removed
  Python            46,000ms    baseline
```

Tantrums is **115x faster than Python** on tight loops. The LLVM backend replaced the old bytecode VM in a single commit with the message *"fuck it, llvm is coming in v0.2"* — and never looked back.

The gap between Tantrums and raw C++ narrows as v4.0 LTO and inlining optimizations land. The gap between Tantrums and Python is already not a gap — it's a canyon.

> **Why not C++ speeds yet?** Every value in Tantrums is NaN-boxed — type identity travels with the value at the cost of unbox/rebox on each arithmetic operation. When escape analysis and full inlining land in v4.0, this overhead shrinks dramatically on provably-typed code paths.

<br>

---

## ⚙️ Architecture

### The Compilation Pipeline

Tantrums transforms your source file through a tightly integrated multi-stage pipeline. Every stage is purpose-built. Nothing is borrowed from a generator.

```
  ┌──────────────────────┐
  │   Source (.42AHH)    │
  └──────────┬───────────┘
             │  Raw text
             ▼
  ┌──────────────────────┐
  │        Lexer         │  Characters → token stream
  │     (lexer.cpp)      │  Keywords, operators, string literals,
  └──────────┬───────────┘  escape sequences, file-level directives
             │  Token stream
             ▼
  ┌──────────────────────┐
  │        Parser        │  Tokens → Abstract Syntax Tree
  │     (parser.cpp)     │  Recursive descent. 9 precedence levels.
  └──────────┬───────────┘  Full forward-reference pre-scan.
             │  AST
             ▼
  ┌──────────────────────┐
  │       Compiler       │  AST → validated, scope-annotated IR
  │    (compiler.cpp)    │  Type inference. Escape analysis.
  └──────────┬───────────┘  Semantic checks. Arity validation.
             │  Validated AST
             ▼
  ┌──────────────────────┐
  │     LLVM Codegen     │  AST → LLVM IR → native binary
  │  (LLVMCodegen.cpp)   │  NaN-boxed 64-bit values. Math intrinsics
  └──────────┬───────────┘  fused directly. Embedded LTO runtime.
             │  Optimized IR
             ▼
  ┌──────────────────────┐
  │   Native Executable  │  Standard platform binary (ELF / PE / Mach-O)
  │    (.exe / ELF)      │  Standalone. No runtime dependency. No VM.
  └──────────────────────┘  Runs directly on hardware.
```

### Value Representation — NaN-Boxing

Every single value in Tantrums — integers, floats, booleans, strings, pointers, lists, maps, null — is represented as a single **64-bit NaN-boxed `uint64_t`**. No structs. No heap allocation for primitives. No type tag on the side.

The encoding exploits unused IEEE 754 NaN bit patterns:

```
  Floats:   stored as raw IEEE 754 bits directly
  All else: NaN pattern with type tag in bits [51:48]
            and payload (pointer or integer) in bits [47:0]

  Tag values:
    FLOAT = 0   (not NaN, just a regular double)
    NULL  = 1
    INT   = 2
    BOOL  = 3
    OBJ   = 4   (strings, lists, maps, pointers)
```

**What this means in practice:**

- Function arguments pass in CPU registers, not on the heap
- No `sret` (struct return) ABI overhead — every return fits in `rax`
- Type checking is a bitmask operation — sub-nanosecond
- Value size is constant and predictable at every pipeline layer
- Cache-friendly — 8 bytes per value, always

This is the exact technique used by V8, SpiderMonkey, and LuaJIT. The same trick that makes JavaScript fast enough to run games is what makes Tantrums fast enough to use seriously.

### Runtime Layer

The runtime (`runtime.cpp`) is compiled to LLVM bitcode at cmake time, then **embedded directly into the compiler binary** as a `const unsigned char[]` array. At compilation time, the runtime bitcode is linked into the user's module via `llvm::Linker::linkModules(LinkOnlyNeeded)` — before the optimizer runs. This means the LLVM optimizer sees both your code and the runtime simultaneously, enabling full cross-boundary inlining and LTO.

**No external runtime library is required.** The compiler is self-contained.

The runtime handles:
- Exception dispatch via `setjmp`/`longjmp`
- Scope tracking for the auto-free system
- Pointer lifecycle validation (null checks, double-free, use-after-free)
- Memory profiling API (process RSS, heap usage, peak heap)
- Auto-free and leak reporting with file output for large reports

<br>

---

## 🎛️ Type System

Tantrums makes a choice most languages refuse to make: **it lets you choose your own philosophy, per file, with a single directive at the top.**

### `#mode static;` — Full Enforcement

Every variable has a type. Every function declares a return type. Every code path must provably return or throw. Type mismatches are hard compile errors. Arity mismatches are hard compile errors. Undeclared variables are hard compile errors.

This is the mode you use when you're building something real and you want the compiler catching your mistakes before they become 3am debugging sessions.

The compiler performs full type inference — it knows `a + b` where both sides are `int` yields `int`, that comparisons produce `bool`, that dereferencing `int*` yields `int`. When it can prove the type, it enforces it. When it can't, it defers to runtime.

### `#mode dynamic;` — Zero Restrictions

Type annotations are accepted but ignored at runtime. Any variable can hold any value at any point. `int x = 5` followed by `x = "now a string"` is completely valid. This is for rapid prototyping, quick scripts, and the moments when you need logic to *run* and you'll clean it up later.

### `#mode both;` — The Harmonized Default

When no directive is specified, `#mode both` is assumed. Typed variables are type-checked. Untyped bare assignments are fully dynamic. You get safety where you explicitly declared it, and freedom everywhere else.

This is Tantrums at its most characteristic: **structured where structure was asked for, free where freedom was implied.**

### Type Coercion

When a typed variable is initialized with a compatible but distinct type, Tantrums coerces automatically and predictably:

| Source | Target | Result | Notes |
|--------|--------|--------|-------|
| `3.9` | `int` | `3` | Truncation, not rounding |
| `5` | `float` | `5.0` | Exact promotion |
| `42` | `string` | `"42"` | Numeric stringification |
| `0` | `bool` | `false` | Zero is false |
| `"hello"` | `bool` | `true` | Non-empty string is true |
| `""` | `bool` | `false` | Empty string is false |

Coercion is never silent. The compiler emits an explicit cast node in the AST. You can always see exactly what's happening.

### Primitive Types

| Type | Description | Default | Notes |
|------|-------------|---------|-------|
| `int` | 64-bit signed integer | `0` | Full 64-bit range |
| `float` | 64-bit IEEE 754 double | `0.0` | Standard double precision |
| `bool` | Boolean value | `false` | `true` or `false` only |
| `string` | UTF-8 string | `""` | Immutable |
| `list` | Dynamic typed array | `[]` | Mixed types allowed |
| `map` | Hash map | `{}` | Any key/value types |
| `null` | Absence of value | — | Explicit null literal |
| `int*` | Heap pointer to int | `null` | Manual or auto-managed |
| `float*` | Heap pointer to float | `null` | All primitives have pointer forms |
| `string*` | Heap pointer to string | `null` | |
| `list*` | Heap pointer to list | `null` | |
| `bool*` | Heap pointer to bool | `null` | |

<br>

---

## 🧠 Memory Model

Memory in Tantrums is an **explicit choice you make**, not something that happens to you. There are no hidden garbage collectors. No background threads doing collection passes. No surprise pauses.

The system is built on two philosophies you select at the top of your file:

### `#autoFree true;` (Default) — Collaborative Management

The compiler and runtime collaborate to automatically manage memory for provably-local allocations. This is not a garbage collector — it is **static escape analysis backed by a runtime safety net**.

**Layer 1 — Compile-Time Escape Analysis**

After each `alloc` declaration, the compiler walks the remaining statements in the enclosing block. It runs a full recursive AST analysis checking every possible escape path. A pointer is considered escaped if it:

1. Appears in a `return` statement
2. Is passed as a function argument
3. Is assigned to an outer-scope variable
4. Is stored in a map value
5. Is stored in a list
6. Is aliased via local-to-local assignment
7. Appears inside a conditional branch (conservative)
8. Appears inside a loop body (conservative)
9. Has more than one reference (conservative)

If the compiler can prove the pointer **does not escape**, it emits an automatic free instruction before scope exit and notes it:

```
[Tantrums] note: auto-freed 'p' at line 12 (provably local)
```

When in doubt, the compiler defers to Layer 2.

**Layer 2 — Runtime Scope Safety Net**

`rt_enter_scope` and `rt_exit_scope` calls bookend every block in the generated IR. On scope exit, the runtime sweeps all allocations registered since the scope opened and frees any pointer or collection whose escape flag was never set. This catches everything compile-time analysis couldn't prove statically — pointers inside conditionals, pointers in loop bodies, conservatively-flagged escapes that turned out not to escape.

The escape analysis covers not just raw pointers — it extends to `list` and `map` locals as well. A local list that never leaves its declaring function will have its internal buffer freed at function return, not at program exit. Verified with a 300-million element list: 5GB heap during function execution, ~6MB after function returns.

**Layer 3 — Use-After-Free Detection**

Accessing a pointer that has already been freed throws a descriptive runtime error with allocation info. Catchable inside `try` blocks.

**Layer 4 — Double-Free Detection**

Freeing an already-freed pointer throws before any heap corruption can occur. Catchable inside `try` blocks.

**Layer 5 — Null Dereference Detection**

Dereferencing a null pointer throws a descriptive runtime error. Catchable inside `try` blocks.

**Layer 6 — Leak Detection**

At program exit, every allocation that was never freed is reported:

- `<= 5 leaks` → printed directly to stderr
- `> 5 leaks` → written to `memoryLeakLog.txt` next to the executable, with full grouping by source location, byte counts, and a `[xN]` multiplier for identical leaks from the same line

**Layer 7 — Auto-Free Reporting**

At program exit, every allocation that was automatically freed is reported:

- `<= 20 auto-frees` → printed to stdout
- `> 20 auto-frees` → written to `autoFree.txt` next to the executable with a one-line terminal summary

### `#autoFree false;` — Full Manual Control

The compiler and runtime step back from lifecycle management entirely. `alloc` and `free` are entirely your responsibility. Leak detection still runs at exit — you'll know exactly what you forgot.

### `#allowMemoryLeaks true;` — Arena Allocation Pattern

Requires `#autoFree false` declared first. Designed for the legitimate pattern of allocating a large pool of objects and letting the OS reclaim everything at process exit rather than individually freeing each allocation. Detected leaks become warnings instead of errors. The exit leak report still fires.

Combining `#allowMemoryLeaks true` with `#autoFree true` is a compile error — those two philosophies are logically incompatible and the compiler will tell you exactly why.

### The Directive Compatibility Matrix

| `#autoFree` | `#allowMemoryLeaks` | Result |
|-------------|---------------------|--------|
| `true` (default) | not set | Full auto-management + leak detection |
| `true` | `true` | **Compile error** — incompatible |
| `false` | not set | Manual management + leak detection |
| `false` | `true` | Manual management, leaks are warnings only |

### Pointer Syntax

```
// Allocate
int* p = alloc int(42);

// Read
print(*p);

// Write
*p = 100;

// Free manually (when #autoFree false)
free(p);

// Typed pointer return
tantrum int* makeInt(int val) {
    int* p = alloc int(val);
    return (p);
}
```

All pointer types are fully typed: `int*`, `float*`, `string*`, `bool*`, `list*`, `map*`. In `#mode static`, function return types and parameter types enforce pointer typing.

<br>

---

## 🏗️ Language Design

### Functions

Functions are declared with the `tantrum` keyword. All functions live at global scope — no nested declarations, no closures, no anonymous lambdas.

```
tantrum <return_type> <name>(<params>) {
    return (value);
}
```

The compiler performs a **full pre-scan of all function signatures** before compiling a single line. You can call any function defined anywhere in the file, or in any imported file, without forward declarations. Everything is available everywhere, always.

Return values are wrapped in parentheses — `return (value)`. This is parser-enforced. It is intentional. After five minutes it feels completely natural.

```
tantrum int add(int a, int b) {
    return (a + b);
}

tantrum string greet(string name) {
    return ("Hello, " + name + "!");
}

tantrum void main() {
    int result = add(3, 4);
    print(result);
    print(greet("world"));
}
```

### Variables

```
// Typed (enforced in #mode static and #mode both)
int x = 10;
float pi = 3.14159;
string name = "Tantrums";
bool alive = true;

// Untyped (dynamic in all modes)
x = 42;
x = "now a string";  // valid in dynamic and both modes

// Compound assignment
x += 5;
x -= 2;
x *= 3;
x /= 4;
x %= 7;

// Increment / decrement
x++;
++x;
x--;
--x;
```

### Control Flow

**Conditionals:**

```
if x > 10 {
    print("big");
} else if x > 5 {
    print("medium");
} else {
    print("small");
}
```

Conditions must evaluate to `bool` at runtime. A non-boolean in a condition is a runtime error, not a silent implicit cast.

**While loops:**

```
int i = 0;
while i < 10 {
    print(i);
    i++;
}
```

**For-in loops:**

```
// Range iteration (lazy — no intermediate list allocated)
for i in range(10) {
    print(i);
}

// Range with start and end
for i in range(2, 8) {
    print(i);
}

// List iteration
list items = [10, 20, 30];
for item in items {
    print(item);
}

// String iteration (yields individual characters)
for ch in "hello" {
    print(ch);
}

// Map iteration (yields keys)
map scores = {"alice": 95, "bob": 87};
for key in scores {
    print(key + ": " + scores[key]);
}
```

`break` and `continue` are supported in all loop types. Both correctly unwind nested scope tracking before jumping — no memory leaks from early exits.

**Switch:**

```
// Default: auto-break mode (no fallthrough, like Python match)
switch (x) {
    case (1) { print("one"); }
    case (2) { print("two"); }
    default  { print("other"); }
}

// Opt-in to C-style fallthrough
#switchBreakMode true;
switch (x) {
    case (1) { print("one or two"); }
    case (2) { print("two"); }
    default  { print("other"); }
}
```

The `default` case always executes last regardless of where it appears in the source.

### Error Handling

```
try {
    int* p = alloc int(42);
    free(p);
    print(*p);  // use-after-free — throws!
} catch (err) {
    print("caught: " + err);
}

// Throw anything
throw "something went wrong";
throw 404;
throw someVariable;

// Nested try blocks work correctly
try {
    try {
        throw "inner error";
    } catch (inner) {
        print("inner caught: " + inner);
        throw "rethrown";  // rethrow works
    }
} catch (outer) {
    print("outer caught: " + outer);
}
```

Exception semantics compile down to `setjmp`/`longjmp` at the C runtime level. Any runtime error — null dereference, division by zero, double-free, use-after-free — becomes a catchable exception when it occurs inside a `try` block.

### Operators

| Operator | Description | Notes |
|----------|-------------|-------|
| `+`, `-`, `*`, `/`, `%` | Arithmetic | Standard precedence |
| `==`, `!=`, `<`, `>`, `<=`, `>=` | Comparison | Returns `bool` |
| `&&`, `\|\|`, `!` | Logical | Short-circuit evaluation |
| `*p` | Pointer dereference | Prefix unary only |
| `"str" + 42` | String concatenation | Auto-converts non-string side |
| `[1,2] + [3,4]` | List concatenation | Produces `[1,2,3,4]` |
| `+=`, `-=`, `*=`, `/=`, `%=` | Compound assignment | Desugared by parser |
| `++`, `--` | Increment/decrement | Both prefix and postfix forms |

### Imports

```
// Import another .42AHH file
use "utils.42AHH";
use "path/to/math_helpers.42AHH";

// Quoted paths, relative to the importing file's directory
use "../../shared/constants.42AHH";
```

Tantrums uses a source-injection import model. Imported files are fully lexed, parsed, and their declarations injected inline into the current compilation context. Each imported file's `#mode`, `#autoFree`, and `#allowMemoryLeaks` directives apply independently to its own declarations — mixed-mode imports work correctly and with zero bleed between files.

Circular imports are detected and rejected. Duplicate imports of the same file are silently deduplicated.

### Built-in Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `print(value)` | `void` | Print any value to stdout |
| `input(prompt)` | `string` | Read line from stdin |
| `len(x)` | `int` | Length of string, list, or map |
| `range(n)` | range | Lazy range [0, n) |
| `range(start, end)` | range | Lazy range [start, end) |
| `type(x)` | `string` | Runtime type name of value |
| `append(list, val)` | `void` | Append value to list |
| `int(x)` | `int` | Convert to integer |
| `float(x)` | `float` | Convert to float |
| `string(x)` | `string` | Convert to string |
| `alloc T(val)` | `T*` | Allocate heap pointer |
| `free(p)` | `void` | Free heap pointer |

<br>

---

## 📚 Standard Libraries

### Math Library — `use math;`

Math functions are not looked up in a dictionary at runtime. The compiler identifies `math.*` calls during AST traversal and fuses them **directly into native LLVM math intrinsics** — `llvm.sin.f64`, `llvm.cos.f64`, `llvm.floor.f64`, etc. There is no map lookup, no function dispatch overhead, no hash collision to resolve. It's a direct hardware instruction.

```
use math;

float angle = 1.5707963;  // π/2
float s = math.sin(angle);
float c = math.cos(angle);
float t = math.tan(angle);
float f = math.floor(3.7);   // 3.0
float cl = math.ceil(3.2);   // 4.0
int r = math.random_int(1, 100);
float rf = math.random_float(0.0, 1.0);
```

| Function | Returns | Description |
|----------|---------|-------------|
| `math.sin(x)` | `float` | Sine (radians) |
| `math.cos(x)` | `float` | Cosine (radians) |
| `math.tan(x)` | `float` | Tangent (radians) |
| `math.sec(x)` | `float` | Secant (radians) |
| `math.cosec(x)` | `float` | Cosecant (radians) |
| `math.cot(x)` | `float` | Cotangent (radians) |
| `math.floor(x)` | `float` | Floor |
| `math.ceil(x)` | `float` | Ceiling |
| `math.random_int(min, max)` | `int` | Random integer in [min, max] |
| `math.random_float(min, max)` | `float` | Random float in [min, max] |

### Filesystem Library — `use filesystem;`

Full cross-platform file and directory I/O. All 17 functions compile to native LLVM calls with the same zero-dispatch pattern as the math library. All path arguments support the `${USERHOME}` prefix, resolved at runtime to `USERPROFILE` on Windows and `HOME` on macOS/Linux.

Every filesystem function throws a catchable Tantrums runtime error on failure — wrap any call in `try`/`catch` for robust I/O error handling.

```
use filesystem;

// Read and write
string content = filesystem.read("data.txt");
filesystem.write("output.txt", "hello world");
filesystem.append("log.txt", "new entry\n");

// Query
bool exists = filesystem.exists("config.txt");
bool isFile = filesystem.isfile("readme.md");
bool isDir  = filesystem.isdir("src/");
int  sz     = filesystem.size("data.bin");

// Directory operations
filesystem.mkdir("new_folder");
filesystem.mkfile("new_folder/file.txt");
list entries = filesystem.listdir("src/");
list lines   = filesystem.readlines("data.txt");

// File management
filesystem.copy("src.txt", "dst.txt");
filesystem.move("old.txt", "new.txt");
filesystem.delete("temp.txt");

// Path utilities
string cwd  = filesystem.cwd();
string abs  = filesystem.abspath("relative/path");

// ${USERHOME} resolution
string cfg = filesystem.read("${USERHOME}/config.42AHH");
```

| Function | Returns | Description |
|----------|---------|-------------|
| `filesystem.read(path)` | `string` | Read entire file as string |
| `filesystem.write(path, content)` | `void` | Write (overwrite) file |
| `filesystem.append(path, content)` | `void` | Append to file |
| `filesystem.exists(path)` | `bool` | Check if path exists |
| `filesystem.delete(path)` | `void` | Delete file |
| `filesystem.mkdir(path)` | `void` | Create directory |
| `filesystem.mkfile(path)` | `void` | Create empty file |
| `filesystem.listdir(path)` | `list` | List directory entries |
| `filesystem.isfile(path)` | `bool` | Check if path is a file |
| `filesystem.isdir(path)` | `bool` | Check if path is a directory |
| `filesystem.copy(src, dst)` | `void` | Copy file |
| `filesystem.move(src, dst)` | `void` | Move/rename file |
| `filesystem.size(path)` | `int` | File size in bytes |
| `filesystem.readlines(path)` | `list` | Read file as list of lines |
| `filesystem.writelines(path, lines)` | `void` | Write list of lines to file |
| `filesystem.cwd()` | `string` | Current working directory |
| `filesystem.abspath(path)` | `string` | Resolve to absolute path |

<br>

---

## 📊 Profiling & Memory APIs

Tantrums ships a first-class profiling API baked directly into the language. No external libraries. No `#include` chains. No configuration. Just call the functions.

### Timing

```
int start = getCurrentTime();

// ... your code ...

int end = getCurrentTime();
float elapsed = toSeconds(end - start);
print("elapsed: " + elapsed + "s");
```

| Function | Returns | Description |
|----------|---------|-------------|
| `getCurrentTime()` | `int` | Unix epoch time in milliseconds |
| `toSeconds(ms)` | `float` | Convert ms → seconds |
| `toMilliseconds(ms)` | `float` | Normalize ms as float |
| `toMinutes(ms)` | `float` | Convert ms → minutes |
| `toHours(ms)` | `float` | Convert ms → hours |

### Memory

```
int before = getProcessMemory();

list big = [];
for i in range(1000000) {
    append(big, i);
}

int after = getProcessMemory();
float mb = bytesToMB(after - before);
print("allocated: " + mb + " MB");
```

| Function | Returns | Description |
|----------|---------|-------------|
| `getProcessMemory()` | `int` | Process RSS in bytes (OS-level) |
| `getHeapMemory()` | `int` | Current Tantrums-managed heap in bytes |
| `getHeapPeakMemory()` | `int` | Peak heap usage since program start |
| `bytesToKB(n)` | `float` | Bytes → kilobytes |
| `bytesToMB(n)` | `float` | Bytes → megabytes |
| `bytesToGB(n)` | `float` | Bytes → gigabytes |

`getProcessMemory()` is platform-aware — `GetProcessMemoryInfo` on Windows, `mach_task_basic_info` on macOS, `/proc/self/statm` on Linux.

<br>

---

## 🎨 VS Code Tooling

Tantrums ships an officially bundled VS Code extension in the `tantrums-vscode/` directory.

**Installation:**

```bash
# Windows
cp -r tantrums-vscode %USERPROFILE%\.vscode\extensions\tantrums-vscode

# Linux / macOS
cp -r tantrums-vscode ~/.vscode/extensions/tantrums-vscode
```

Restart VS Code and open any `.42AHH` file. The extension activates automatically.

**What you get:**

- **Syntax highlighting** — Full semantic coverage: keywords, directives, type annotations, control flow constructs, math and filesystem library calls, built-in functions, string escape sequences, comments. Every layer of the language has a distinct color.

- **IntelliSense & Snippets** — 30+ snippet definitions for loop structures, mode directives, pointer patterns, switch blocks, try/catch, memory directives, and directive combinations. Tab-completeable. Parameter-aware.

- **Hover Documentation** — Hover any keyword, built-in function, directive, or memory management command to see its full return type signature and inline usage documentation without leaving the editor.

- **Live Diagnostics** — On-save analysis that surfaces memory leak patterns, type checking violations, unresolved identifiers, and dead-branch logic directly in the editor gutter. Know what's wrong before you compile.

<br>

---

## 🛠️ Building from Source

### Requirements

| Requirement | Version |
|-------------|---------|
| CMake | 3.15+ |
| C++ Compiler | GCC 13+ or Clang 16+ (**do not use MSVC**) |
| LLVM | Bundled in `external/llvm-backend/` |
| llvm-link | Bundled in `external/llvm-backend/bin/` |

### Build

```bash
# Clone
git clone https://github.com/Plexescor/tantrums.git
cd tantrums

# Configure
# Release strongly recommended — Debug can be ~2x slower at runtime
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

Output locations:

| Platform | Binary |
|----------|--------|
| Windows | `.\build\Release\tantrums.exe` |
| Linux / macOS | `./build/tantrums` |

### CLI Usage

```bash
# Compile source to native executable
tantrums build main.42AHH

# Compile and run immediately
tantrums run main.42AHH

# Suppress auto-free notes during execution
tantrums run --no-autofree-notes main.42AHH
```

`--no-autofree-notes` suppresses inline `[Tantrums] note: auto-freed 'x'` messages. The `autoFree.txt` report written at exit is unaffected.

On startup, the runtime prints:

```
[Tantrums] Mode: both (typed + dynamic)
[Tantrums] Built: main.exe
```

### File Extensions

Source files must use the `.42AHH` extension. Any other extension is rejected at the CLI level before compilation begins.

```
error: only .42AHH files are compiled.
Did you really think you can get away with the .42AHH extension just by changing names 🥀🥀🥀
```

> There is one additional accepted extension. Finding it is left as an exercise for the sufficiently determined.

<br>

---

## 🗺️ Roadmap

Tantrums just completed the most significant architectural shift in its history: ripping out the bytecode VM entirely and replacing it with a full LLVM native compilation pipeline with embedded LTO runtime. That transition is complete and the foundation is solid. Here is where it goes next.

### v1.0 — Current ✅

Full LLVM native codegen with embedded bitcode LTO. `try`/`catch` exception handling via `setjmp`/`longjmp`. Math library with direct LLVM intrinsic fusion. Filesystem standard library with 17 cross-platform I/O functions. File-level mode enforcement (`#mode static/dynamic/both`). 7-layer memory safety architecture (escape analysis, runtime safety net, use-after-free, double-free, null dereference, leak detection, auto-free reporting). VS Code extension. Comprehensive per-file directive system with zero bleed between imported files.

### v2.0 — The Standard Integration

Full restoration of compile-time escape analysis in the LLVM IR path. Slab allocation optimizations for repeated alloc/free cycles. String standard library: `split`, `trim`, `replace`, `indexOf`, `substring`, `toLower`, `toUpper`, `startsWith`, `endsWith`. IO module abstractions for stdin/stdout/stderr.

### v3.0 — The Graphical Standard

Win32 windowing module accessible from typed Tantrums code. Native OpenGL pipeline bindings. This is the version that makes Tantrums viable as a game scripting layer and a serious tool for graphics programming.

### v4.0 — Deep Optimization

Link-Time Optimization across the full module. Aggressive static branch elimination. Automatic SIMD vectorization for recognized loop patterns. Compiler-driven escape-analysis-informed loop unrolling. The version where Tantrums stops being fast and starts being unreasonable.

### Not Yet Implemented

```
C/C++ FFI            · Networking module      · Audio module
Package manager      · Address-of operator &  · String module (v2.0)
IO module (v2.0)     · Closures               · Anonymous functions
```

<br>

---

## 📐 Hard Limits Reference

Every compiler has walls. Here are Tantrums's walls, exactly:

| Limit | Value | Defined In |
|-------|-------|------------|
| Max imports per file | 64 | `compile_source` |
| Max tracked globals | 512 | `MAX_GLOBALS` |
| Max function signatures | 256 | `MAX_FUNC_SIGS` |
| Max typed params checked per call | 16 | Compiler |
| Max `break`/`continue` jumps per loop | 64 | Compiler |
| Max constants per function | 65,535 | `uint16_t` index |
| Max unique leak groups in exit report | 100 | `memleaklog.txt` |
| Max auto-free entries before file output | 20 | `autoFree.txt` |
| Max leak entries before file output | 5 | `memoryLeakLog.txt` |

<br>

---

## 📁 Repository Structure

```
tantrums/
│
├── src/
│   ├── main.cpp              CLI entry point — build/run dispatch,
│   │                         directive preprocessing, import resolution
│   ├── lexer.cpp             Source text → token stream
│   ├── lexer.h
│   ├── parser.cpp            Token stream → Abstract Syntax Tree
│   │                         Recursive descent, 9 precedence levels
│   ├── parser.h
│   ├── ast.cpp               AST node types, construction, freeing
│   ├── ast.h
│   ├── compiler.cpp          AST → semantic validation, type inference,
│   │                         escape analysis, arity checking, scope mgmt
│   ├── compiler.h
│   ├── LLVMCodegen.cpp       AST → LLVM IR → native binary
│   │                         NaN-boxing, embedded LTO, LLD linking
│   ├── LLVMCodegen.h
│   ├── runtime.cpp           Exception handling, scope tracking,
│   │                         memory safety, profiling API, leak reporting
│   ├── runtime.h
│   ├── value.cpp             NaN-boxed value structs, object types,
│   │                         string interning, list/map internals
│   ├── value.h
│   └── stdlib/
│       ├── maths.cpp         Math standard library  (use math;)
│       ├── maths.h
│       ├── filesystem.cpp    Filesystem standard library  (use filesystem;)
│       └── filesystem.h
│
├── include/                  System headers and runtime interface
│
├── external/
│   └── llvm-backend/         LLVM v16 static libraries + llvm-link binary
│
├── cmake/
│   └── embed_binary.cmake    Converts .bc bitcode → C header array
│                             Pure CMake, no external tools required
│
├── tantrums-vscode/          Official VS Code extension
│   ├── syntaxes/             TextMate grammar for syntax highlighting
│   ├── snippets/             30+ snippet definitions
│   └── package.json
│
├── tests/                    Test suite
│   └── memory_test.42AHH     Memory safety verification tests
│
├── REFERENCE.txt             Complete language specification (23 sections)
├── TANTRUMS_PLAN.txt         Internal architectural roadmap
├── CMakeLists.txt            Build configuration
└── .gitignore
```

<br>

---

## 🤝 Contributing

Tantrums is a passion project with a clear identity: fast execution, honest design, and zero tolerance for unnecessary complexity. Pull Requests are welcome.

**Before contributing:**

1. Read `REFERENCE.txt` completely. It is 23 sections long and covers every aspect of the language. Do not submit changes that contradict it.
2. Understand the NaN-boxing value representation. Every new type or operation touches this.
3. Understand the escape analysis system. Memory safety changes require extreme care.
4. Run the test suite. `tests/memory_test.42AHH` must pass completely.

**The rules:**

- Match the existing code style — C++23, no unnecessary abstractions, comments that explain *why* not *what*
- If you break the compiler, you get to keep both pieces
- If something is faster than it has any right to be, open an issue — we want to document it
- If something crashes in a way that isn't a descriptive error, that's a bug worth fixing

<br>

---

## 📜 License

GNU General Public License v3.0.

Copy it, modify it, distribute it — but anything that includes or hooks into this codebase must remain fully open under the same license. The full license text is available at [gnu.org/licenses/gpl-3.0](https://www.gnu.org/licenses/gpl-3.0).

<br>

---

<div align="center">

<br>

```
  No Dragon Books were opened in the making of this compiler.
  No formal language theory was harmed.
  One bytecode VM was deleted with the commit message "removed vm.cpp, fuck"
  and nobody mourned it.
```

<br>

*Built on chaos, LLVM pipelines, and the deeply irrational conviction*
*that a student could write a real compiler.*

<br>

[![Stars](https://img.shields.io/github/stars/Plexescor/tantrums?style=social)](https://github.com/Plexescor/tantrums)
[![Forks](https://img.shields.io/github/forks/Plexescor/tantrums?style=social)](https://github.com/Plexescor/tantrums)

<br>

**`.42AHH` only. no exceptions. 🥀**

<br>

</div>