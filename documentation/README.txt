================================================================================

  ████████╗ █████╗ ███╗   ██╗████████╗██████╗ ██╗   ██╗███╗   ███╗███████╗
  ╚══██╔══╝██╔══██╗████╗  ██║╚══██╔══╝██╔══██╗██║   ██║████╗ ████║██╔════╝
     ██║   ███████║██╔██╗ ██║   ██║   ██████╔╝██║   ██║██╔████╔██║███████╗
     ██║   ██╔══██║██║╚██╗██║   ██║   ██╔══██╗██║   ██║██║╚██╔╝██║╚════██║
     ██║   ██║  ██║██║ ╚████║   ██║   ██║  ██║╚██████╔╝██║ ╚═╝ ██║███████║
     ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝   ╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝╚══════╝

                   A language built from scratch. With vibes.

================================================================================
                      WELCOME TO THE TANTRUMS DOCUMENTATION
================================================================================

  You found the docs. Congratulations. You're already doing better than most
  people who just start typing and hope for the best.

  Tantrums is a custom programming language written entirely in C++. It compiles
  .42AHH source files into .42ass bytecode, which runs on a custom-built
  stack-based virtual machine. Yes, those are the real file extensions. No,
  we are absolutely not changing them. They're perfect.

  This documentation will take you from zero to actually writing real Tantrums
  code. Each file in this folder covers a different part of the language in
  detail — including the fun parts, the weird parts, and the parts where we
  made questionable engineering decisions and are very proud of them anyway.

================================================================================
  WHY DOES THIS LANGUAGE EXIST?
================================================================================

  Great question. The honest answer: because writing a programming language from
  scratch makes you want to throw one. Hence the name — Tantrums.

  The slightly less honest answer: because every language out there makes you
  choose. Want safety? Use Rust, but good luck understanding the borrow checker.
  Want speed? Use C, but good luck not segfaulting yourself into oblivion.
  Want ease? Use Python, but good luck running it faster than a particularly
  motivated snail.

  Tantrums says: what if you didn't have to choose?

  With Tantrums, you pick your philosophy per file. One directive at the top and
  you go from "safe and automatic" to "C-level manual control" to "arena
  allocation, I know exactly what I'm doing." You can write Python-style dynamic
  code in one file and strict statically-typed systems code in the next. Same
  language. Same compiler. Same runtime.

  Also, the file extension is .42AHH, which is extremely funny and that matters.

================================================================================
  THE NUMBERS (BECAUSE NUMBERS ARE COOL)
================================================================================

  Tantrums v0.2.0 benchmark results vs CPython on the same hardware:

  ┌─────────────────────────────┬───────────┬────────────┬─────────────┐
  │ Benchmark                   │ Tantrums  │ Python     │ Winner      │
  ├─────────────────────────────┼───────────┼────────────┼─────────────┤
  │ Raw integer loop (100M)     │ 4.26s     │ 46.86s     │ Tantrums 11x│
  │ List append (200k)          │ 0.018s    │ 0.075s     │ Tantrums 4x │
  │ List iterate (200k)         │ 0.013s    │ 0.106s     │ Tantrums 8x │
  │ Map string insert (10k)     │ 0.004s    │ 0.035s     │ Tantrums 8x │
  │ Baseline RAM usage          │ 3.96 MB   │ 30.18 MB   │ Tantrums 7x │
  └─────────────────────────────┴───────────┴────────────┴─────────────┘

  Overall on the full benchmark suite: 8.75x faster than Python.
  No JIT. No native compilation. Just a well-built bytecode VM.

  (There is one benchmark where Tantrums loses — sequential integer map reads.
  That's a known bug in the hash function. It's being fixed. We're not hiding it.
  The bug is documented. This is what honest engineering looks like.)

================================================================================
  THE FILE EXTENSIONS — AN EXPLANATION
================================================================================

  .42AHH — Source files
  ─────────────────────
  42 comes from The Hitchhiker's Guide to the Galaxy. It's the answer to the
  ultimate question of life, the universe, and everything. We felt that was
  appropriate for a file containing code that is supposed to do something useful.

  AHH is the sound you make when you look at your own code three days after
  writing it and have absolutely no idea what's happening.

  .42ass — Compiled bytecode files
  ─────────────────────────────────
  The .42ass bytecode format is cross-platform. Compile on Windows, run on
  Linux. Compile on Linux, run on Mac. The VM is entirely abstract — it doesn't
  know or care what CPU architecture is underneath. You can literally email a
  .42ass file to a friend on a completely different OS and it will just run.

  "ass" officially stands for "Assembly." That is our story and we're sticking
  to it. Tantrums-ass. .42ass. We are adults.

================================================================================
  WHAT'S IN THIS DOCUMENTATION FOLDER
================================================================================

  ┌──────────────────────────┬──────────────────────────────────────────────┐
  │ File                     │ What it covers                               │
  ├──────────────────────────┼──────────────────────────────────────────────┤
  │ README.txt               │ You are here. Start here.                    │
  │ getting_started.txt      │ Build the compiler, write Hello World,       │
  │                          │ understand the full compilation pipeline      │
  │ variables_and_types.txt  │ Type system, mode directives, built-in types,│
  │                          │ strings, lists, maps, default initialization  │
  │ functions.txt            │ Function declaration, void, pointer returns,  │
  │                          │ imports, built-in functions, the call stack   │
  │ control_flow.txt         │ if/else, for loops, while, break, continue,  │
  │                          │ operators, short-circuit evaluation           │
  │ memory_and_errors.txt    │ The full memory system — #autoFree,          │
  │                          │ #allowMemoryLeaks, escape analysis, leak      │
  │                          │ detection, error handling, try/catch          │
  └──────────────────────────┴──────────────────────────────────────────────┘

  Read them in order if you're new. Jump around if you're not.

================================================================================
  THE ARCHITECTURE IN ONE GLANCE
================================================================================

  When you run `tantrums run yourfile.42AHH`, this is what happens:

  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
  │  your .42AHH │────▶│    LEXER     │────▶│   PARSER     │
  │  source file │     │  tokenizes   │     │ builds AST   │
  └──────────────┘     └──────────────┘     └──────────────┘
                                                    │
                                                    ▼
  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
  │   .42ass     │◀────│   COMPILER   │◀────│  SEMANTIC    │
  │  bytecode    │     │  generates   │     │  ANALYSIS    │
  └──────────────┘     │  + escape    │     │ type checks  │
                       │  analysis    │     └──────────────┘
                       └──────────────┘
                               │
                               ▼
                       ┌──────────────┐
                       │   VIRTUAL    │
                       │   MACHINE    │
                       │  executes    │
                       │  bytecode    │
                       └──────────────┘
                               │
                               ▼
                          your output

  The VM is a classic stack-based interpreter — the same fundamental design
  used by the JVM, the Python interpreter, and Lua. Values get pushed onto
  an operand stack, operations pop them off, process them, and push the result
  back. It's elegantly simple and surprisingly fast.

================================================================================
  THE FEATURE OVERVIEW
================================================================================

  Here's everything Tantrums currently supports:

  TYPING & MODES
  ──────────────
  • Dynamic typing by default — no type annotations needed
  • Optional static types — annotate any variable or parameter
  • Three mode directives: #mode static, #mode dynamic, #mode both
  • Compile-time type checking for annotated variables and parameters
  • #mode static enforces return types, void, all-paths-return rules

  MEMORY MANAGEMENT (the star of the show)
  ─────────────────────────────────────────
  • Manual memory with alloc and free keywords
  • #autoFree true (default) — compiler + runtime auto-free pointers
  • Escape analysis — 10 conditions, conservative rule, no false positives
  • Compile-time auto-free with notes for provably local pointers
  • Runtime auto-free as safety net for ambiguous cases
  • #autoFree false — full C-style manual control
  • #allowMemoryLeaks true — arena/region allocation pattern support
  • Seven-layer memory safety stack (see memory_and_errors.txt)
  • Exit leak detector with memleaklog.txt for large leak counts
  • Grouped deduplication [x2000000] for repeated leaks
  • Runtime use-after-free detection
  • Runtime double-free detection
  • Runtime null dereference detection

  LANGUAGE FEATURES
  ──────────────────
  • Functions with tantrum keyword
  • void return type
  • Pointer return type (tantrum int* foo())
  • if / else if / else
  • for-in loops (range, list, string)
  • while loops
  • break and continue
  • All standard operators + reversed >= and <= aliases (=> and =<)
  • Compound assignment: += -= *= /= %=
  • Increment/decrement: i++ ++i i-- --i
  • String auto-concat with any type
  • Lists with dynamic append
  • Maps with proper key typing (1 and "1" are different keys)
  • Imports with use keyword, circular import prevention
  • throw for errors
  • try / catch with optional error variable
  • Nested try/catch with rethrow

  BUILT-IN FUNCTIONS
  ───────────────────
  • print, input, len, range, type, append (always available, no import)
  • Time API: getCurrentTime, toSeconds, toMinutes, toHours, toMilliseconds
  • Memory API: getProcessMemory, getVmMemory, getVmPeakMemory
  • Conversion: bytesToKB, bytesToMB, bytesToGB

  TOOLING
  ────────
  • VS Code extension with syntax highlighting, IntelliSense, hover docs
  • 40+ code snippets
  • 20+ real-time diagnostic checks in the editor
  • Custom file icons for .42AHH and .42ass

================================================================================
  THE KEYWORD THAT STARTED IT ALL
================================================================================

  In most languages, you declare a function like this:
    Python:     def my_function():
    JavaScript: function myFunction() {
    Rust:       fn my_function() {
    Go:         func myFunction() {
    C++:        void myFunction() {

  In Tantrums, you do it like this:
    tantrum myFunction() {

  Why? Because when a programmer writes a function, they are essentially throwing
  a tantrum at the computer. "DO THIS THING. IN THIS ORDER. WITH THESE INPUTS.
  AND GIVE ME BACK EXACTLY WHAT I EXPECT." That's a tantrum. Every function
  you've ever written in any language has been a tantrum. We're just honest
  about it.

================================================================================
  THE PHILOSOPHY
================================================================================

  Tantrums was built on four principles:

  1. You should be able to choose how strict your code is.
     Not globally. Per file. Different problems need different tools.

  2. Memory bugs should be caught, not silently corrupted.
     If you free something twice, you should get an error message,
     not a segfault that takes three hours to debug.

  3. The developer experience matters.
     File extensions should be funny. Error messages should be clear.
     The compiler should tell you exactly what went wrong and where.

  4. Fun is a valid engineering goal.
     A language that is fun to use gets used. A language that is miserable
     gets abandoned. We optimized for fun first and correctness second
     and somehow ended up with both.

================================================================================
  TRIVIA CORNER — THINGS THAT HAPPENED DURING DEVELOPMENT
================================================================================

  🔥 The garbage collector was originally a function that did nothing.
     Memory leaks were internally classified as "features of a long-running
     process." This lasted approximately two test runs before scripts started
     consuming all available system RAM.

  🔥 During Lexer development, a bug caused the EOF token to be treated as
     an actual variable name. The VM tried to allocate memory for a variable
     literally named "EOF" and promptly segfaulted. It took 3 hours to figure
     out why empty files were destroying the machine.

  🔥 A self-referencing map inside a while(true) loop consumed 32GB of RAM
     (16GB physical + 16GB page file) and forced a hard system reboot. The
     reference cycle collector was implemented immediately after.

  🔥 The .42AHH extension breaks syntax highlighting in basically every editor
     that exists. The entire VS Code extension was built specifically to fix this.
     It now has hover documentation, real-time diagnostics, and 40+ snippets.
     All because of a file extension decision that nobody was willing to undo.

  🔥 The main() function enforcement was added very late. Originally you could
     write code floating in global scope like Python. It was removed because
     managing local variables in global scope made the compiler logic a nightmare.
     A VS Code diagnostic literally named "NO MAIN FUNC missing wrapper detection"
     was added because users kept wondering why their code wasn't running.

  🔥 The language was built without reading a single compiler design textbook.
     The developers just kept writing C++ until something executed successfully.
     It is, in the most literal sense, artisanal handcrafted AI-assisted code.

  🔥 Bitwise operators do not exist. Nobody remembers how to use them anyway.

================================================================================
  HOW TO READ THESE DOCS
================================================================================

  Each documentation file is split into three sections:

  BASIC INFO
  ───────────
  The fundamentals. If you read nothing else, read this. It tells you how to
  actually use the feature without getting into the guts of the implementation.
  Start here if you're learning.

  ADVANCED INFO
  ──────────────
  The deep stuff. Implementation details, edge cases, performance characteristics,
  quirks you'll hit when building real programs. Read this when the basics aren't
  enough or when something is behaving unexpectedly.

  LAUGHS & FACTS
  ───────────────
  The fun part. Real stories from development, weird quirks, implementation
  trivia, and the occasional terrible decision explained in full. These sections
  exist because documentation doesn't have to be boring.

================================================================================
  QUICK REFERENCE — THE SHORTEST VALID TANTRUMS PROGRAM
================================================================================

  tantrum main()
  {
      print("Hello, World!");
  }

  Save as hello.42AHH
  Run with: tantrums run hello.42AHH
  Marvel at your creation.

  Now go read getting_started.txt. It gets better from here.

================================================================================
  END OF README. WELCOME TO TANTRUMS. WE'RE GLAD YOU'RE HERE.
================================================================================