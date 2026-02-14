# Alkyl as Business for Requirements Analysis and Design Definition

## 1. Introduction

This knowledge area describes how the high-level business needs identified in the Strategy Analysis are elaborated into specific requirements and then translated into design definitions that guide the software construction.

## 2. Specify and Model Requirements

### 2.1. Functional Requirements Modeling

Requirements for Alkyl are modeled using a combination of formal grammar definitions and behavior specifications.

* Syntax Specification modeled via tree-sitter grammar (JavaScript/JSON) to ensure formal parsing rules.
* Strong typing where implicit conversions are disallowed except for safe integer promotions.
* Union types by modelling as tagged unions or raw memory overlaps depending on compilation flags.
* Must support x86_64 and ARM64 architectures via LLVM.

### 2.2. Non-Functional Requirements (NFRs)

Speed by design.

Compiler throughput must at least faster than JVM through LLVM-IR optimization.

Moreover, the compiler itself must build on Linux, macOS, and Windows.

Futhermore, error messages must contain file path, line number, column, and a suggested fix (Diagnostics).

## 3. Verify and Validate Requirements

### 3.1. Verification (Are we building the product right?)

Unit Testing: Each language feature (Loops, Classes, Arrays) is verified by specific .aky test files in code/test/.

Integration Testing: The driver is tested against full programs to ensure linking and execution correctness.

### 3.2. Validation (Are we building the right product?)

User Acceptance: Validation occurs via "Dogfooding"—using Alkyl to write its own standard library (lib/std).

Community Feedback: GitHub Issues and Discussions serve as the validation loop for syntax ergonomics.

## 4. Define Requirements Architecture

The requirements are structured hierarchically to trace business goals to technical implementations:

Business Goal: Seamless C Interoperability.

User Requirement: Ability to include standard C headers.

Functional Requirement: Parser must handle .hky files acting as headers.

Functional Requirement: Codegen must support externC linkage conventions.

Technical Design: lib/llvm-c mappings in the compiler source.

Business Goal: Modern Control Flow.

User Requirement: Eliminate goto spaghetti code.

Functional Requirement: Support defer or RAII-like constructs (if planned).

Functional Requirement: Support Pattern Matching (case/switch).

Technical Design: src/codegen/flow.c implementation of switch tables.

## 5. Design Options and Selection

### 5.1. Backend Selection

Option A: Write custom machine code generation. (Rejected: High effort, low portability).

Option B: Transpile to C. (Rejected: Slow compilation, debugging difficulty).

Option C: Use LLVM API. (Selected: Industry standard, high optimization, immediate multi-platform support).

### 5.2. Memory Management Design

Option A: Garbage Collection. (Rejected: Violates systems programming performance goals).

Option B: Ownership/Borrow Checker (Rust-style). (Deferred: High complexity for MVP).

Option C: Manual + Defer (Zig/Odin-style). (Selected: Fits the "Better C" strategic vision). (Focus on this first)
