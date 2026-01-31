# About this project

CD25 (Compiler Design 25) is a toy programming language implemented in COMP3290 Compiler Design at the University of Newcastle in 2025. It is statically typed and compiled to bytecode.

It is a C-like language, with syntax similar to Pascal, and it is parsed using a context-free grammar. For simplicity so that it could be done in one semester by one person, it has no heap, its structs can only be used in arrays and the arrays can only consist of structs, and must have their size known at compile-time. There is no char or string type, and variables can only be declared in their own section at the start of the block, and there is no preprocessor. However the '=' operator for arrays and structs performs a deep copy of all component data for arrays of the same type, representing a higher-level programming feature (not one I'm fond of myself, as now '=' can represent 100 operations).

There are two target architectures, the first is x86_64 Linux, the other target is SM25 (Stack Machine 25), which is interpreted by the simulator in /simulator/SM25.jar. SM25 bytecode programs are remarkably similar to assembly, except for having registers only for the positions of the current instruction, the top of the stack and the call frame, all other data passing being done using the stack and memory addresses to previous words inside it. The stack contains call frames for procedures, enabling recursion.

# Implementation of language features

The language implementation for SM25 is almost complete, the two missing features are function-local arrays and const arrays (const arrays are gramatically valid, but my implementation doesn't make semantic checks for preventing their modification). My implementation also has a non-standard extension where 3-1-1-1 parses as expected, the standard grammar stops at 3-1 then throws a syntax error for a dangling -1-1.

The X86 language implementation has the same feature set.

# Usage

Run the makefile in /src with "make" in a terminal to build the compiler executable.

The compiler takes a source file as argument, prints any errors or warnings for the compilation and creates in the working directory a .mod SM25 bytecode module and optionally a .lst listing file for the source code and any warnings/errors.

A few precompiled modules that can be run by the interpreter are provided, as is their source code in /cd25_programs. For SM25, the simulator is started using "java -jar SM25.jar", entering the filename for the .mod, clicking load and then running either until halt or per instruction. Input and output text file can also be customised, but the files have to exist, so I just used the defaults to save keystrokes.

For x86_64 Linux, a script to assemble and link the produced .asm is provided, using NASM to assemble the program into an object file, and a C compiler for convenience in linking the object with libc. Manually linking using ld with a path to Linux's loader can also be done if preferred.
