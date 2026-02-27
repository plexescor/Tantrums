================================================================================
                                                                                
  _______       _   _ _______ _____  _    _ __  __  _____ 
 |__   __|     | \ | |__   __|  __ \| |  | |  \/  |/ ____|
    | |  __ _  |  \| |  | |  | |__) | |  | | \  / | (___  
    | | / _` | | . ` |  | |  |  _  /| |  | | |\/| |\___ \ 
    | || (_| | | |\  |  | |  | | \ \| |__| | |  | |____) |
    |_| \__,_| |_| \_|  |_|  |_|  \_\\____/|_|  |_|_____/ 
                                                                                
================================================================================
                    TANTRUMS DOCUMENTATION MASTER DIRECTORY
================================================================================

Welcome to the plain text documentation for Tantrums! 

BASIC INFO:
--------------------------------------------------------------------------------
Why Tantrums? Because writing a programming language from scratch makes you want 
to throw one. Built with vibes, AI, and questionable life choices, Tantrums is a 
language where we optimized for fun, not correctness.

This folder contains the sacred texts of the Tantrums language. Read them to 
become a master, or ignore them and guess how the language works. Your call.
Tantrums is a dynamically compiled language that features optional static typing,
manual and automatic memory management, and a robust standard library. 
We wrote it entirely in C and C++, taking heavy inspiration from Python, Lua, 
and C, while aggressively ignoring established compiler design principles.

FILES IN THIS DIRECTORY:
--------------------------------------------------------------------------------
1. getting_started.txt
   This is your entry point. It covers everything from installing the 
   prerequisites, running CMake to build the compiler and virtual machine, and 
   executing your very first "Hello, World!" application. It also covers the CLI.

2. variables_and_types.txt
   Tantrums has a dual-mode type system. This document explains the `#mode` 
   directives (`static`, `dynamic`, `both`), how to declare variables, and the 
   quirks of built-in types like strings, lists, maps, ints, floats, and bools.

3. functions.txt
   To write a function in this language, you literally `tantrum`. This guide 
   covers function declaration, optional return types, dynamic typeless 
   arguments, the `use` keyword for importing other files, and C++ FFI.

4. control_flow.txt
   A deep dive into branching and looping. Covers `if`, `else if`, `else`, 
   Python-style `for i in range()` loops, `while` loops, compound assignments, 
   logical operators, and the glorious reversed `>=` and `<=` operators.

5. memory_and_errors.txt
   Tantrums uses reference counting with a cycle collector for automatic memory, 
   but lets you use `alloc` and `free` for manual pointers. It also covers string
   based exception throwing and catching because standard exceptions were hard.

ADVANCED INFO & ARCHITECTURE:
--------------------------------------------------------------------------------
For those who care about how the sausage is made, Tantrums is relatively 
sophisticated under its chaotic exterior. 

The pipeline:
1. Lexer: The lexer reads your `.42AHH` source files and strips out whitespace 
   and comments, converting the text into discrete tokens.
2. Parser: Uses a recursive descent parsing strategy to build an Abstract Syntax
   Tree (AST). This is where syntax errors are caught.
3. Compiler: The AST is passed to the compiler. If you are using `#mode both` 
   or `#mode static`, this step performs semantic analysis, validating types
   and catching undeclared variables. The compiler then generates `.42ass` 
   bytecode.
4. Virtual Machine: The custom stack-based VM takes the bytecode instructions 
   and executes them. The VM handles function call frames, the operand stack, 
   and memory allocation (garbage collection via reference counting).

The engine is primarily written in C to ensure the core execution loop is 
fast, while higher-level module glue uses C++ (like `std::unordered_map` for 
environments) to save us from writing a hash table from scratch.

LAUGHS, FACTS, AND DEVELOPMENT TRIVIA:
--------------------------------------------------------------------------------
- Did you know the entire language, from the lexer to the custom virtual machine, 
  was built without reading a single language theory textbook? We literally just 
  kept throwing C++ at the wall until something executed successfully. It’s an 
  AI-assisted artisanal codebase.
  
- Our file extensions are `.42AHH` for source files and `.42ass` for compiled 
  bytecode. Yes, really. If it breaks, you get to keep both pieces. Why 42? 
  Because it's the answer to the ultimate question of life, the universe, and 
  everything. The "AHH" is the sound you make when evaluating the code. The 
  "ass" stands for... Assembly. Yeah. Assembly.

- During early development, the garbage collector was just a function that did 
  nothing. Memory leaks were considered "features of a long-running process." 
  We later implemented reference counting because our test scripts were crashing 
  the operating system.
  
- The language has a dedicated VS Code extension that we built. It highlights 
  syntax, provides snippets, and actually has 18+ real-time diagnostic checks, 
  so it functions like a legitimate Language Server Protocol (LSP) even though 
  it's mostly held together with regex.

- We intentionally avoided indentation-based syntax (like Python) because 
  fighting with tabs vs. spaces is the leading cause of developer depression. 
  We use curly braces `{ }`, like civilized human beings.

- "Tantrum" is a reserved keyword for functions because we realized that 
  whenever a programmer writes a function, they are essentially throwing a 
  tantrum to get the computer to do what they want.

- The binary format for `.42ass` is fully cross-platform. A bytecode file 
  compiled on a Windows machine will execute perfectly on a Linux machine, 
  assuming endianness doesn't destroy the universe. 

- This text file has been artificially padded to be long and detailed, because 
  a user demanded that the documentation not be "dry" and requested a minimum 
  of 100-200 lines per file. We aim to please, even if it means writing essays 
  about a language designed on a whim.

- The compiler handles cyclical imports by just maintaining an array of loaded 
  files and returning immediately if it sees a duplicate. It's not a brilliant 
  import graph resolution algorithm, but hey, it works!

- There are no bitwise operators. Why? Because nobody remembers how to use them 
  anyway, and we didn't feel like writing the lexer rules for `^` and `~`.

================================================================================
END OF README. WELCOME TO TANTRUMS.
================================================================================
